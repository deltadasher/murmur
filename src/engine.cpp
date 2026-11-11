#include "engine.hpp"
#include <QCoreApplication>
#include <QMetaObject>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

const pw_registry_events Engine::registryEvents=[] {
    pw_registry_events e{}; e.version=PW_VERSION_REGISTRY_EVENTS;
    e.global=global; e.global_remove=removed; return e;
}();
const pw_core_events Engine::coreEvents=[] {
    pw_core_events e{}; e.version=PW_VERSION_CORE_EVENTS;
    e.done=coreDone; e.error=coreError; return e;
}();
const pw_stream_events Engine::captureEvents=[] {
    pw_stream_events e{}; e.version=PW_VERSION_STREAM_EVENTS;
    e.state_changed=streamState; e.process=captureProcess; return e;
}();
const pw_stream_events Engine::outputEvents=[] {
    pw_stream_events e{}; e.version=PW_VERSION_STREAM_EVENTS;
    e.state_changed=streamState; e.control_info=outputControl; e.process=outputProcess; return e;
}();
const pw_stream_events Engine::monitorEvents=[] {
    pw_stream_events e{}; e.version=PW_VERSION_STREAM_EVENTS;
    e.state_changed=streamState; e.process=monitorProcess; return e;
}();

Engine::Engine(QObject* parent):QObject(parent) {
    pw_init(nullptr,nullptr);
    sourceName=QStringLiteral("murmur.microphone");
}
Engine::~Engine() {
    if (loop) {
        stop();
        pw_thread_loop_stop(loop);
        if (registry) { spa_hook_remove(&registryHook); pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry)); }
        if (core) { spa_hook_remove(&coreHook); pw_core_disconnect(core); }
        if (context) pw_context_destroy(context);
        pw_thread_loop_destroy(loop);
    }
    pw_deinit();
}
bool Engine::connectServer() {
    if (loop) return ready.load();
    loop=pw_thread_loop_new("murmur-control",nullptr);
    if (!loop) { emit failed("Could not create the audio thread."); return false; }
    context=pw_context_new(pw_thread_loop_get_loop(loop),nullptr,0);
    if (!context) { emit failed("Could not create the PipeWire context."); return false; }
    core=pw_context_connect(context,pw_properties_new(PW_KEY_APP_NAME,"Murmur",
        PW_KEY_MEDIA_CATEGORY,"Manager",nullptr),0);
    if (!core) {
        emit failed("Cannot connect to PipeWire. Check that pipewire and wireplumber are running, then reopen Murmur.");
        return false;
    }
    pw_core_add_listener(core,&coreHook,&coreEvents,this);
    registry=pw_core_get_registry(core,PW_VERSION_REGISTRY,0);
    if (!registry) { emit failed("Could not discover audio devices."); return false; }
    pw_registry_add_listener(registry,&registryHook,&registryEvents,this);
    syncSeq=pw_core_sync(core,PW_ID_CORE,0);
    if (pw_thread_loop_start(loop)<0) { emit failed("Could not start the audio thread."); return false; }
    emit status("Connecting to PipeWire…");
    return true;
}
void Engine::global(void* data,uint32_t id,uint32_t,const char* type,uint32_t,const spa_dict* props) {
    auto* self=static_cast<Engine*>(data);
    if (!props || std::strcmp(type,PW_TYPE_INTERFACE_Node)) return;
    const char* media=spa_dict_lookup(props,PW_KEY_MEDIA_CLASS);
    const char* name=spa_dict_lookup(props,PW_KEY_NODE_NAME);
    if (!media || !name || std::strcmp(media,"Audio/Source") || std::strncmp(name,"murmur.",7)==0) return;
    const char* desc=spa_dict_lookup(props,PW_KEY_NODE_DESCRIPTION);
    self->sources[id]=QString::fromUtf8(name);
    emit self->deviceAdded(id,QString::fromUtf8(name),QString::fromUtf8(desc ? desc:name));
}
void Engine::removed(void* data,uint32_t id) {
    auto* self=static_cast<Engine*>(data);
    auto it=self->sources.find(id);
    if (it==self->sources.end()) return;
    if (self->active && it->second==self->selected) {
        self->active=false;
        QMetaObject::invokeMethod(self,[self] {
            self->stop(); emit self->failed("Microphone disconnected. Reconnect it, select it, and start again.");
        },Qt::QueuedConnection);
    }
    self->sources.erase(it); emit self->deviceRemoved(id);
}
void Engine::coreDone(void* data,uint32_t id,int seq) {
    auto* self=static_cast<Engine*>(data);
    if (id==PW_ID_CORE && seq==self->syncSeq) {
        self->ready=true; emit self->serverReady(); emit self->status("Ready · choose your microphone and start.");
    }
}
void Engine::coreError(void* data,uint32_t id,int,int res,const char* message) {
    auto* self=static_cast<Engine*>(data);
    if (id==PW_ID_CORE || res==-EPIPE) {
        self->ready=false; self->active=false;
        QMetaObject::invokeMethod(self,[self,msg=QString::fromUtf8(message ? message:"Connection lost")] {
            self->stop(); emit self->failed("PipeWire connection lost: "+msg+". Reopen Murmur after audio is restored.");
        },Qt::QueuedConnection);
    }
}
bool Engine::start(const QString& source,bool monitor) {
    if (!ready || source.isEmpty()) { emit failed("Select an available microphone first."); return false; }
    pw_thread_loop_lock(loop);
    releaseStreams();
    bool exists=false;
    for (const auto& [id,name]:sources) { (void)id; if (name==source) exists=true; }
    if (!exists) { pw_thread_loop_unlock(loop); emit failed("That microphone is no longer connected."); return false; }
    selected=source; monitoring=monitor; announced=false;
    queue.reset(); monitorQueue.reset(); dropped=0; underruns=0;
    dsp=murmur::Processor{};
    const QByteArray target=source.toUtf8(),name=sourceName.toUtf8();
    auto* inProps=pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Capture",
        PW_KEY_MEDIA_ROLE,"Communication",PW_KEY_NODE_NAME,"murmur.capture",
        PW_KEY_TARGET_OBJECT,target.constData(),PW_KEY_NODE_DONT_RECONNECT,"true",
        "node.dont-fallback","true",PW_KEY_NODE_LATENCY,"256/48000",
        PW_KEY_NODE_ALWAYS_PROCESS,"true",nullptr);
    auto* outProps=pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Capture",
        PW_KEY_MEDIA_ROLE,"Communication",PW_KEY_MEDIA_CLASS,"Audio/Source",
        PW_KEY_NODE_NAME,name.constData(),PW_KEY_NODE_DESCRIPTION,"Murmur Virtual Microphone",
        PW_KEY_NODE_VIRTUAL,"true",PW_KEY_NODE_LATENCY,"256/48000",
        "priority.session","0",nullptr);
    capture=pw_stream_new(core,"Murmur microphone input",inProps);
    output=pw_stream_new(core,"Murmur Virtual Microphone",outProps);
    if (!capture || !output) {
        releaseStreams(); pw_thread_loop_unlock(loop); emit failed("Could not create audio streams."); return false;
    }
    pw_stream_add_listener(capture,&captureHook,&captureEvents,this);
    pw_stream_add_listener(output,&outputHook,&outputEvents,this);
    uint8_t podBuffer[1024]; spa_pod_builder builder=SPA_POD_BUILDER_INIT(podBuffer,sizeof(podBuffer));
    spa_audio_info_raw format{}; format.format=SPA_AUDIO_FORMAT_F32; format.rate=48000;
    format.channels=1; format.position[0]=SPA_AUDIO_CHANNEL_MONO;
    const spa_pod* param=spa_format_audio_raw_build(&builder,SPA_PARAM_EnumFormat,&format);
    constexpr auto flags=static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS|PW_STREAM_FLAG_RT_PROCESS);
    int result=pw_stream_connect(output,PW_DIRECTION_OUTPUT,PW_ID_ANY,flags,&param,1);
    if (result>=0) result=pw_stream_connect(capture,PW_DIRECTION_INPUT,PW_ID_ANY,
        static_cast<pw_stream_flags>(flags|PW_STREAM_FLAG_AUTOCONNECT),&param,1);
    if (monitor && result>=0) {
        monitorStream=pw_stream_new(core,"Murmur headphone preview",pw_properties_new(
            PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Playback",PW_KEY_MEDIA_ROLE,"Communication",
            PW_KEY_NODE_NAME,"murmur.preview",PW_KEY_NODE_LATENCY,"256/48000",nullptr));
        if (!monitorStream) result=-ENOMEM;
        else {
            pw_stream_add_listener(monitorStream,&monitorHook,&monitorEvents,this);
            result=pw_stream_connect(monitorStream,PW_DIRECTION_OUTPUT,PW_ID_ANY,
                static_cast<pw_stream_flags>(flags|PW_STREAM_FLAG_AUTOCONNECT),&param,1);
        }
    }
    if (result<0) releaseStreams(); else active=true;
    pw_thread_loop_unlock(loop);
    if (result<0) { emit failed(QString("Audio setup failed: %1").arg(std::strerror(-result))); return false; }
    emit status("Connecting microphone…"); return true;
}
void Engine::releaseStreams() {
    active=false;
    systemMuteKnown=false; systemMuted=false;
    // Disconnect/destroy synchronizes with each stream's data callback before
    // resetting queues or the DSP. Stream objects belong to this client only.
    if (capture) { spa_hook_remove(&captureHook); pw_stream_destroy(capture); capture=nullptr; }
    if (output) { spa_hook_remove(&outputHook); pw_stream_destroy(output); output=nullptr; }
    if (monitorStream) { spa_hook_remove(&monitorHook); pw_stream_destroy(monitorStream); monitorStream=nullptr; }
    inputPeak=0; outputPeak=0; limited=false;
    emit systemMuteChanged();
}
void Engine::outputControl(void* data,uint32_t id,const pw_stream_control* control) {
    if (id!=SPA_PROP_mute) return;
    auto* self=static_cast<Engine*>(data);
    const bool known=control && control->n_values>0 && control->values;
    const bool muted=known && control->values[0]!=0.f;
    const bool oldKnown=self->systemMuteKnown.exchange(known);
    const bool oldMuted=self->systemMuted.exchange(muted);
    if (oldKnown!=known || oldMuted!=muted) emit self->systemMuteChanged();
}
bool Engine::unmuteSystem(QString& error) {
    if (!loop) { error="The virtual microphone is not running."; return false; }
    pw_thread_loop_lock(loop);
    if (!output || !systemMuteKnown.load()) {
        pw_thread_loop_unlock(loop); error="The virtual microphone's system mute is unavailable."; return false;
    }
    float value=0.f;
    const int result=pw_stream_set_control(output,SPA_PROP_mute,1,&value,0);
    pw_thread_loop_unlock(loop);
    if (result<0) { error=QString("Could not unmute the virtual microphone: %1").arg(std::strerror(-result)); return false; }
    // The observed control callback is authoritative; do not optimistically
    // report an unmute before PipeWire confirms the new state.
    return true;
}
void Engine::stop() {
    if (!loop) return;
    pw_thread_loop_lock(loop); releaseStreams(); pw_thread_loop_unlock(loop);
    emit runningChanged(false); emit status("Stopped · your virtual microphone has been removed.");
}
void Engine::streamState(void* data,pw_stream_state,pw_stream_state state,const char* error) {
    auto* self=static_cast<Engine*>(data);
    if (state==PW_STREAM_STATE_ERROR) {
        QMetaObject::invokeMethod(self,[self,msg=QString::fromUtf8(error ? error:"Unknown stream error")] {
            self->stop(); emit self->failed("Audio stream failed: "+msg);
        },Qt::QueuedConnection);
    } else if (self->capture && self->output && !self->announced &&
        pw_stream_get_state(self->capture,nullptr)==PW_STREAM_STATE_STREAMING &&
        pw_stream_get_state(self->output,nullptr)>=PW_STREAM_STATE_PAUSED) {
        self->announced=true; emit self->runningChanged(true);
        emit self->status("Live · select Murmur Virtual Microphone in your calling or recording app.");
    }
}
void Engine::captureProcess(void* data) {
    auto* self=static_cast<Engine*>(data);
    pw_buffer* b;
    self->dsp.configure(self->controls.load());
    self->dsp.inputPeak=self->dsp.outputPeak=0; self->dsp.limited=false;
    while ((b=pw_stream_dequeue_buffer(self->capture))) {
        auto& d=b->buffer->datas[0];
        if (d.data && d.chunk && d.maxsize) {
            const uint32_t offset=std::min(d.chunk->offset,d.maxsize);
            const auto count=std::min(d.chunk->size,d.maxsize-offset)/sizeof(float);
            const float* input=reinterpret_cast<const float*>(static_cast<const char*>(d.data)+offset);
            for (std::size_t i=0;i<count;++i) {
                const float v=self->dsp.tick(input[i]);
                if (!self->queue.push(v)) self->dropped.fetch_add(1,std::memory_order_relaxed);
                if (self->monitoring) self->monitorQueue.push(v);
            }
        }
        pw_stream_queue_buffer(self->capture,b);
    }
    self->inputPeak.store(self->dsp.inputPeak,std::memory_order_relaxed);
    self->outputPeak.store(self->dsp.outputPeak,std::memory_order_relaxed);
    if (self->dsp.limited) self->limited=true;
}
void Engine::fill(pw_stream* stream,AudioQueue& q,bool primary) {
    auto* b=pw_stream_dequeue_buffer(stream);
    if (!b) return;
    auto& d=b->buffer->datas[0];
    if (d.data && d.chunk) {
        auto frames=d.maxsize/sizeof(float);
        if (b->requested) frames=std::min<std::size_t>(frames,b->requested);
        auto* out=static_cast<float*>(d.data);
        // When no app is consuming the source, discard old audio when a new
        // consumer arrives. Never replay a backlog of earlier microphone audio.
        float discarded;
        while (q.size()>frames*2) q.pop(discarded);
        uint64_t missing=0;
        for (std::size_t i=0;i<frames;++i) if (!q.pop(out[i])) { out[i]=0; ++missing; }
        if (primary) underruns.fetch_add(missing,std::memory_order_relaxed);
        d.chunk->offset=0; d.chunk->stride=sizeof(float);
        d.chunk->size=static_cast<uint32_t>(frames*sizeof(float)); b->size=frames;
    }
    pw_stream_queue_buffer(stream,b);
}
void Engine::outputProcess(void* data) {
    auto* self=static_cast<Engine*>(data); self->fill(self->output,self->queue,true);
}
void Engine::monitorProcess(void* data) {
    auto* self=static_cast<Engine*>(data); self->fill(self->monitorStream,self->monitorQueue,false);
}
