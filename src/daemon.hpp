#pragma once
#include "engine.hpp"
#include "model.hpp"
#include "background_portal.hpp"
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonArray>
#include <QTimer>
#include <QMap>
#include <memory>
#ifdef Q_OS_WIN
#include <QLockFile>
#endif

class Daemon : public QObject {
    Q_OBJECT
public:
    explicit Daemon(QObject* parent=nullptr);
    ~Daemon() override;
    bool listen();
    void persist();
private:
    QLocalServer server;
    BackgroundPortal background;
#ifdef Q_OS_WIN
    std::unique_ptr<QLockFile> lock;
#else
    int lockFd=-1;
#endif
    std::unique_ptr<Engine> engine;
    QList<QLocalSocket*> clients;
    QMap<uint32_t,QJsonObject> devices;
    QTimer retry,saveTimer,meters,startTimeout;
    murmur::Parameters params;
    QString source,message="Connecting to audio…",error;
    bool audioReady=false,running=false,requested=false,starting=false,monitor=false;
    bool reconnect=true;
    bool ownsSession=false;
    uint64_t revision=0;
    int guardHold=0;
    void createEngine();
    void attemptStart();
    void receive(QLocalSocket*);
    QJsonObject snapshot() const;
    void broadcast();
    static void send(QLocalSocket*,const QJsonObject&);
    void command(QLocalSocket*,const QJsonObject&);
    void stopAudio();
};
