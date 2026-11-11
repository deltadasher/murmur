#pragma once
#include "dsp.hpp"
#include <QObject>
#include <QString>
#include <pipewire/pipewire.h>
#include <array>
#include <atomic>
#include <map>

// A bounded single-producer/single-consumer queue. Each stream may run on
// a different PipeWire data thread. Neither audio callback locks the GUI.
class AudioQueue {
public:
    static constexpr uint32_t capacity=16384;
    bool push(float v) {
        auto w=write.load(std::memory_order_relaxed);
        if (w-read.load(std::memory_order_acquire)>=capacity) return false;
        data[w%capacity]=v; write.store(w+1,std::memory_order_release); return true;
    }
    bool pop(float& v) {
        auto r=read.load(std::memory_order_relaxed);
        if (r==write.load(std::memory_order_acquire)) return false;
        v=data[r%capacity]; read.store(r+1,std::memory_order_release); return true;
    }
    uint32_t size() const { return write.load(std::memory_order_acquire)-read.load(std::memory_order_acquire); }
    void reset() { read=0; write=0; }
private:
    std::array<float,capacity> data{};
    std::atomic<uint32_t> read{0},write{0};
};

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject* parent=nullptr);
    ~Engine() override;
    bool connectServer();
    bool start(const QString& source, bool monitor);
    void stop();
    murmur::Controls controls;
    std::atomic<float> inputPeak{0},outputPeak{0};
    std::atomic<uint64_t> dropped{0},underruns{0};
    std::atomic<bool> limited{false};
    const QString& virtualName() const { return sourceName; }
    bool isReady() const { return ready.load(); }
    bool isSystemMuteKnown() const { return systemMuteKnown.load(); }
    bool isSystemMuted() const { return systemMuted.load(); }
    bool unmuteSystem(QString& error);
signals:
    void deviceAdded(uint32_t id,QString name,QString description);
    void deviceRemoved(uint32_t id);
    void serverReady();
    void status(QString message);
    void failed(QString message);
    void runningChanged(bool running);
    void systemMuteChanged();
private:
    pw_thread_loop* loop=nullptr;
    pw_context* context=nullptr;
    pw_core* core=nullptr;
    pw_registry* registry=nullptr;
    pw_stream* capture=nullptr;
    pw_stream* output=nullptr;
    pw_stream* monitorStream=nullptr;
    spa_hook registryHook{},coreHook{},captureHook{},outputHook{},monitorHook{};
    murmur::Processor dsp;
    AudioQueue queue,monitorQueue;
    QString sourceName,selected;
    std::map<uint32_t,QString> sources;
    std::atomic<bool> ready{false},active{false};
    std::atomic<bool> systemMuteKnown{false},systemMuted{false};
    bool monitoring=false;
    bool announced=false;
    int syncSeq=0;
    static const pw_registry_events registryEvents;
    static const pw_core_events coreEvents;
    static const pw_stream_events captureEvents,outputEvents,monitorEvents;
    static void global(void*,uint32_t,uint32_t,const char*,uint32_t,const spa_dict*);
    static void removed(void*,uint32_t);
    static void coreDone(void*,uint32_t,int);
    static void coreError(void*,uint32_t,int,int,const char*);
    static void streamState(void*,pw_stream_state,pw_stream_state,const char*);
    static void outputControl(void*,uint32_t,const pw_stream_control*);
    static void captureProcess(void*);
    static void outputProcess(void*);
    static void monitorProcess(void*);
    void fill(pw_stream*,AudioQueue&,bool primary);
    void releaseStreams();
};
