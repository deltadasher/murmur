#include "windows_engine.hpp"
#include <QTimer>
#include <algorithm>
#include <cstring>

namespace {
bool fatalAudioError(QtAudio::Error error) {
#if QT_VERSION < QT_VERSION_CHECK(6, 12, 0)
    return error!=QtAudio::NoError && error!=QtAudio::UnderrunError;
#else
    return error!=QtAudio::NoError;
#endif
}
QString deviceKey(const QAudioDevice& d) { return QString::fromLatin1(d.id().toHex()); }
QAudioFormat formatFor(const QAudioDevice& device) {
    QAudioFormat f; f.setSampleRate(48000); f.setSampleFormat(QAudioFormat::Float);
    for (int channels : {1,2}) { f.setChannelCount(channels); if (device.isFormatSupported(f)) return f; }
    return {};
}
}
Engine::Engine(QObject* parent):QObject(parent) {
    connect(&devices,&QMediaDevices::audioInputsChanged,this,[this]{
        const QString old=selected; bool wasActive=active;
        enumerate();
        if(wasActive && std::none_of(inputs.cbegin(),inputs.cend(),[&](const QAudioDevice& d){return deviceKey(d)==old;})) {
            stop(); emit failed("Microphone disconnected. Reconnect it to continue.");
        }
    });
    connect(&devices,&QMediaDevices::audioOutputsChanged,this,[this]{
        if(active) { stop(); emit failed("Audio outputs changed. Start the microphone again."); }
    });
}
Engine::~Engine() { stop(); }
void Engine::enumerate() {
    for (int i=0;i<inputs.size();++i) emit deviceRemoved(static_cast<uint32_t>(i+1));
    inputs.clear();
    for (const auto& d:QMediaDevices::audioInputs()) {
        if(d.description().contains("CABLE Output",Qt::CaseInsensitive)) continue;
        inputs.append(d);
        emit deviceAdded(static_cast<uint32_t>(inputs.size()),deviceKey(d),d.description());
    }
}
bool Engine::connectServer() {
    ready=true;
    QTimer::singleShot(0,this,[this]{enumerate(); emit serverReady();});
    return true;
}
bool Engine::start(const QString& source,bool monitor) {
    stop();
    QAudioDevice input, cable;
    for(const auto& d:inputs) if(deviceKey(d)==source) input=d;
    for(const auto& d:QMediaDevices::audioOutputs()) if(d.description().contains("CABLE Input",Qt::CaseInsensitive) && d.description().contains("VB-Audio",Qt::CaseInsensitive)) { cable=d; break; }
    if(input.isNull()) { emit failed("Select an available microphone first."); return false; }
    if(cable.isNull()) { emit failed("VB-CABLE was not found. Install it, then restart Murmur."); return false; }
    inputFormat=formatFor(input); outputFormat=formatFor(cable);
    if(!inputFormat.isValid()||!outputFormat.isValid()) { emit failed("The microphone and cable must support 48 kHz floating-point audio."); return false; }
    QAudioDevice speakers=QMediaDevices::defaultAudioOutput();
    if(monitor && (speakers.isNull() || speakers.id()==cable.id() || !(previewFormat=formatFor(speakers)).isValid())) {
        emit failed("Headphone preview needs a separate default playback device supporting 48 kHz audio."); return false;
    }
    selected=source; processor=murmur::Processor{}; pending.clear();
    output=std::make_unique<QAudioSink>(cable,outputFormat); output->setBufferSize(8192);
    outputIO=output->start();
    if(monitor) { preview=std::make_unique<QAudioSink>(speakers,previewFormat); preview->setBufferSize(8192); previewIO=preview->start(); }
    capture=std::make_unique<QAudioSource>(input,inputFormat); capture->setBufferSize(8192); captureIO=capture->start();
    if(!outputIO||!captureIO||(monitor&&!previewIO) || capture->error()!=QtAudio::NoError || output->error()!=QtAudio::NoError || (preview && preview->error()!=QtAudio::NoError)) { stop(); emit failed("Could not start the audio devices. Check microphone permissions and the cable."); return false; }
    active=true;
    auto reportError=[this] { QTimer::singleShot(0,this,[this] {
        if(active && ((capture && capture->error()!=QtAudio::NoError) || (output && fatalAudioError(output->error())) || (preview && fatalAudioError(preview->error())))) {
            stop(); emit failed("An audio device stopped unexpectedly. Check your microphone and VB-CABLE, then start again.");
        }
    }); };
    connect(capture.get(),&QAudioSource::stateChanged,this,[reportError](QtAudio::State){reportError();});
    connect(output.get(),&QAudioSink::stateChanged,this,[reportError](QtAudio::State){reportError();});
    if(preview) connect(preview.get(),&QAudioSink::stateChanged,this,[reportError](QtAudio::State){reportError();});
    connect(captureIO,&QIODevice::readyRead,this,&Engine::readAudio);
    QTimer::singleShot(0,this,[this]{if(active){emit runningChanged(true);emit status("Live · select CABLE Output in your calling app.");}});
    return true;
}
void Engine::readAudio() {
    if(!active || !captureIO) return;
    pending+=captureIO->readAll();
    const int inChannels=inputFormat.channelCount(), outChannels=outputFormat.channelCount();
    const qsizetype frameBytes=static_cast<qsizetype>(sizeof(float))*inChannels;
    const qsizetype frames=pending.size()/frameBytes;
    if(frames<=0) return;
    QByteArray out(frames*outChannels*static_cast<qsizetype>(sizeof(float)),Qt::Uninitialized);
    QByteArray monitor;
    if(previewIO) monitor.resize(frames*previewFormat.channelCount()*static_cast<qsizetype>(sizeof(float)));
    processor.configure(controls.load()); processor.inputPeak=processor.outputPeak=0; processor.limited=false;
    for(qsizetype i=0;i<frames;++i) {
        float sum=0;
        for(int c=0;c<inChannels;++c) { float v; std::memcpy(&v,pending.constData()+(i*inChannels+c)*sizeof(float),sizeof(float)); sum+=v; }
        const float v=processor.tick(sum/static_cast<float>(inChannels));
        for(int c=0;c<outChannels;++c) std::memcpy(out.data()+(i*outChannels+c)*sizeof(float),&v,sizeof(float));
        if(previewIO) for(int c=0;c<previewFormat.channelCount();++c) std::memcpy(monitor.data()+(i*previewFormat.channelCount()+c)*sizeof(float),&v,sizeof(float));
    }
    pending.remove(0,frames*frameBytes);
    const qint64 written=outputIO->write(out);
    if(written<out.size()) dropped.fetch_add(static_cast<uint64_t>((out.size()-std::max<qint64>(0,written))/(outChannels*sizeof(float))));
    if(previewIO) previewIO->write(monitor);
    inputPeak=processor.inputPeak; outputPeak=processor.outputPeak; if(processor.limited) limited=true;
}
void Engine::stop() {
    const bool wasActive=active; active=false;
    if(capture) capture->stop();
    if(output) output->stop();
    if(preview) preview->stop();
    capture.reset(); output.reset(); preview.reset(); captureIO=outputIO=previewIO=nullptr; pending.clear();
    inputPeak=0;outputPeak=0;limited=false;
    if(wasActive) emit runningChanged(false);
}
