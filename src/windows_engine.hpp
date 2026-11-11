#pragma once
#include "dsp.hpp"
#include <QObject>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QIODevice>
#include <memory>
#include <atomic>

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject* parent=nullptr);
    ~Engine() override;
    bool connectServer();
    bool start(const QString& source, bool monitor);
    void stop();
    murmur::Controls controls;
    std::atomic<float> inputPeak{0}, outputPeak{0};
    std::atomic<uint64_t> dropped{0}, underruns{0};
    std::atomic<bool> limited{false};
    bool isReady() const { return ready; }
    bool isSystemMuteKnown() const { return false; }
    bool isSystemMuted() const { return false; }
    bool unmuteSystem(QString& error) { error="System mute cannot be changed by Murmur on Windows."; return false; }
signals:
    void deviceAdded(uint32_t,QString,QString);
    void deviceRemoved(uint32_t);
    void serverReady();
    void status(QString);
    void failed(QString);
    void runningChanged(bool);
    void systemMuteChanged();
private:
    void enumerate();
    void readAudio();
    QMediaDevices devices;
    QList<QAudioDevice> inputs;
    std::unique_ptr<QAudioSource> capture;
    std::unique_ptr<QAudioSink> output, preview;
    QIODevice *captureIO=nullptr,*outputIO=nullptr,*previewIO=nullptr;
    QAudioFormat inputFormat, outputFormat, previewFormat;
    murmur::Processor processor;
    QByteArray pending;
    QString selected;
    bool ready=false, active=false;
};
