#include "daemon.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

Daemon::Daemon(QObject* parent):QObject(parent) {
    connect(&background,&BackgroundPortal::changed,this,[this]{broadcast();});
    QSettings settings; source=settings.value("microphone").toString();
    requested=settings.value("processingRequested",false).toBool();
    QString ignored; auto doc=QJsonDocument::fromJson(settings.value("parameters").toByteArray());
    if (doc.isObject()) applyParameterPatch(doc.object(),murmur::Parameters{},params,ignored);
    saveTimer.setSingleShot(true); saveTimer.setInterval(150);
    connect(&saveTimer,&QTimer::timeout,this,&Daemon::persist);
    startTimeout.setSingleShot(true); startTimeout.setInterval(5000);
    connect(&startTimeout,&QTimer::timeout,this,[this] {
        if (!starting) return;
        starting=false; engine->stop(); error="Could not connect the microphone. Check your audio session and try again."; broadcast();
    });
    connect(&server,&QLocalServer::newConnection,this,[this] {
        while (auto* socket=server.nextPendingConnection()) {
            socket->setReadBufferSize(65537); clients.append(socket);
            connect(socket,&QLocalSocket::readyRead,this,[this,socket]{receive(socket);});
            connect(socket,&QLocalSocket::disconnected,this,[this,socket]{clients.removeAll(socket);socket->deleteLater();});
            send(socket,snapshot());
        }
    });
    retry.setInterval(2000);
    connect(&retry,&QTimer::timeout,this,[this]{ if (reconnect) createEngine(); else attemptStart(); });
    meters.setInterval(50);
    connect(&meters,&QTimer::timeout,this,[this] {
        if (!engine || clients.isEmpty()) return;
        if (engine->limited.exchange(false)) guardHold=20; else if (guardHold>0) --guardHold;
        const auto db=[](float value) { return std::clamp(20.f*std::log10(std::max(value,1e-6f)),-60.f,0.f); };
        QJsonObject update{{"type","meters"},{"input",db(engine->inputPeak.load())},{"output",db(engine->outputPeak.load())},{"limited",guardHold>0}};
        for (auto* socket:clients) send(socket,update);
    });
}
Daemon::~Daemon() {
    persist();
    if (engine) { disconnect(engine.get(),nullptr,this,nullptr); engine.reset(); }
    // Delete sockets while clients and callbacks still exist; QObject child
    // destruction happens after member destruction otherwise.
    for (auto* socket:std::as_const(clients)) { disconnect(socket,nullptr,this,nullptr); delete socket; }
    clients.clear(); server.close();
#ifndef Q_OS_WIN
    if(lockFd>=0)close(lockFd);
#endif
}
bool Daemon::listen() {
#ifdef Q_OS_WIN
    const QString dir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/murmur";
#else
    const QString dir=QFileInfo(controlSocketPath()).absolutePath();
#endif
    if (!QDir().mkpath(dir)) { std::fprintf(stderr,"Could not create the runtime directory.\n"); return false; }
    QFile::setPermissions(dir,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
#ifdef Q_OS_WIN
    lock=std::make_unique<QLockFile>(dir+"/daemon.lock");
    lock->setStaleLockTime(0);
    if(!lock->tryLock()) { std::fprintf(stderr,"Murmur daemon is already running, or its lock is unavailable.\n"); return false; }
#else
    // Kernel locks work across Flatpak PID namespaces and release on crashes.
    // Keep the inode in place: unlinking it would allow a second independent lock.
    const auto path=QFile::encodeName(dir+"/daemon.lock");
    lockFd=open(path.constData(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    struct stat info{};
    if(lockFd<0||fstat(lockFd,&info)!=0||!S_ISREG(info.st_mode)||info.st_uid!=geteuid()||flock(lockFd,LOCK_EX|LOCK_NB)!=0) {
        std::fprintf(stderr,"Murmur daemon is already running, or its lock is unavailable.\n");return false;
    }
    if(fchmod(lockFd,0600)!=0){std::fprintf(stderr,"Could not secure the daemon lock.\n");return false;}
#endif
    // Only the lock holder may remove a stale socket from a previous crash.
    QLocalServer::removeServer(controlSocketPath()); server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(controlSocketPath())) { std::fprintf(stderr,"%s\n",qPrintable(server.errorString())); return false; }
    ownsSession=true; createEngine(); retry.start(); meters.start(); return true;
}
void Daemon::createEngine() {
    reconnect=false; audioReady=false; running=false; starting=false; devices.clear();
    if (engine) disconnect(engine.get(),nullptr,this,nullptr);
    engine=std::make_unique<Engine>(); engine->controls.store(params);
    connect(engine.get(),&Engine::deviceAdded,this,[this](uint32_t id,QString name,QString description) {
        devices[id]={{"id",static_cast<double>(id)},{"name",name},{"description",description}}; broadcast(); attemptStart();
    });
    connect(engine.get(),&Engine::deviceRemoved,this,[this](uint32_t id){devices.remove(id);broadcast();});
    connect(engine.get(),&Engine::serverReady,this,[this] { audioReady=true; reconnect=false; error.clear(); broadcast(); attemptStart(); });
    connect(engine.get(),&Engine::status,this,[this](QString text){message=text;broadcast();});
    connect(engine.get(),&Engine::systemMuteChanged,this,[this]{broadcast();});
    connect(engine.get(),&Engine::runningChanged,this,[this](bool active) {
        running=active;
        if (active) { starting=false; startTimeout.stop(); error.clear();background.beginRequest(); }
        broadcast();
    });
    connect(engine.get(),&Engine::failed,this,[this](QString text) {
        starting=false; running=false; startTimeout.stop(); error=text;
        if (!engine->isReady()) { audioReady=false; reconnect=true; }
        broadcast();
    });
    if (!engine->connectServer()) reconnect=true;
    broadcast();
}
void Daemon::attemptStart() {
    if (!audioReady || !requested || running || starting || source.isEmpty()) return;
    bool present=false; for (const auto& device:devices) if (device.value("name").toString()==source) present=true;
    if (!present) { error="Waiting for your selected microphone to reconnect."; broadcast(); return; }
    starting=true; error.clear(); broadcast();
    if (engine->start(source,monitor)) startTimeout.start(); else {starting=false;broadcast();}
}
void Daemon::persist() {
    if (!ownsSession) return;
    QSettings settings; settings.setValue("microphone",source); settings.setValue("processingRequested",requested);
    settings.setValue("parameters",QJsonDocument(parametersJson(params)).toJson(QJsonDocument::Compact)); settings.sync();
}
QJsonObject Daemon::snapshot() const {
    QJsonArray sources; for (const auto& device:devices) sources.append(device);
    return {{"type","state"},{"protocol",1},{"revision",static_cast<double>(revision)},
        {"applicationVersion",QCoreApplication::applicationVersion()},
        {"backgroundState",background.state()},{"backgroundWarning",background.warning()},
        {"daemonPid",static_cast<double>(QCoreApplication::applicationPid())},{"audioReady",audioReady},
        {"running",running},{"requested",requested},{"starting",starting},{"source",source},
        {"systemMuteKnown",engine && engine->isSystemMuteKnown()},{"systemMuted",engine && engine->isSystemMuted()},
        {"monitor",monitor},{"message",message},{"error",error},{"sources",sources},{"parameters",parametersJson(params)}};
}
void Daemon::send(QLocalSocket* socket,const QJsonObject& obj) {
    if (socket->state()!=QLocalSocket::ConnectedState) return;
    if (socket->bytesToWrite()>1024*1024) { socket->abort(); return; }
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact)+'\n');
}
void Daemon::broadcast() {
    ++revision; auto state=snapshot(); for (auto* socket:clients) send(socket,state);
}
void Daemon::receive(QLocalSocket* socket) {
    while (socket->canReadLine()) {
        auto line=socket->readLine(65537);
        if (line.size()>65536) { socket->abort(); return; }
        QJsonParseError parse; auto doc=QJsonDocument::fromJson(line,&parse);
        if (parse.error!=QJsonParseError::NoError || !doc.isObject()) { send(socket,{{"type","reply"},{"ok",false},{"error","Invalid JSON command."}}); continue; }
        command(socket,doc.object());
    }
    if (socket->bytesAvailable()>65536) socket->abort();
}
void Daemon::stopAudio() {
    requested=false; starting=false; startTimeout.stop();
    engine->stop(); running=false; error.clear(); persist(); broadcast();
}
void Daemon::command(QLocalSocket* socket,const QJsonObject& request) {
    const QString cmd=request.value("command").toString(); QString failure;
    bool quit=false;
    if (cmd=="get") { }
    else if (cmd=="set") {
        murmur::Parameters result;
        if (!request.value("parameters").isObject()) failure="Missing controls.";
        else if (applyParameterPatch(request.value("parameters").toObject(),params,result,failure)) {
            params=result; engine->controls.store(params); saveTimer.start(); broadcast();
        }
    } else if (cmd=="start") {
        const QString selected=request.value("source").toString(source);
        if (selected.isEmpty()) failure="Select a microphone first.";
        else {
            const bool preview=request.value("monitor").toBool(monitor);
            if (running || starting) { engine->stop(); running=false; starting=false; startTimeout.stop(); }
            source=selected; monitor=preview; requested=true; error.clear(); persist(); attemptStart(); broadcast();
        }
    } else if (cmd=="unmute-system") {
        if (!engine) failure="The virtual microphone is not running.";
        else engine->unmuteSystem(failure);
    } else if (cmd=="stop") stopAudio();
    else if (cmd=="monitor") {
        if (!request.value("enabled").isBool()) failure="Preview must be true or false.";
        else { monitor=request.value("enabled").toBool(); if (running || starting) {engine->stop();running=false;starting=false;startTimeout.stop();attemptStart();} broadcast(); }
    } else if (cmd=="shutdown") { stopAudio(); quit=true; }
    else failure="Unknown command.";
    send(socket,{{"type","reply"},{"id",request.value("id")},{"ok",failure.isEmpty()},{"error",failure},{"state",snapshot()}});
    if (quit) QTimer::singleShot(100,qApp,&QCoreApplication::quit);
}
