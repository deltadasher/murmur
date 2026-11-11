#include "client.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

void launchDaemon(QObject* owner,std::function<void(bool)> callback) {
    const auto detached=[callback] {
        QProcess process;
        if(!qEnvironmentVariable("FLATPAK_ID").isEmpty()) {
            // A separate copy of this sandbox outlives the controller. Never spawn on the host.
            process.setProgram(QStandardPaths::findExecutable("flatpak-spawn"));
            process.setArguments({QCoreApplication::applicationDirPath()+"/murmur-daemon"});
        } else {
            process.setProgram(QCoreApplication::applicationDirPath()+"/murmur-daemon");
            if (!QFileInfo::exists(process.program())) process.setProgram(QStandardPaths::findExecutable("murmur-daemon"));
        }
        const auto logDir=QStandardPaths::writableLocation(QStandardPaths::StateLocation)+"/murmur";
        QDir().mkpath(logDir);process.setStandardOutputFile(logDir+"/daemon.log",QIODevice::Append);
        process.setStandardErrorFile(logDir+"/daemon.log",QIODevice::Append);
        callback(!process.program().isEmpty() && process.startDetached());
    };
    const QString userUnit=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/systemd/user/murmur.service";
    const bool unitPresent=QFileInfo::exists(userUnit) || QFileInfo::exists("/usr/lib/systemd/user/murmur.service");
    if (!qEnvironmentVariable("FLATPAK_ID").isEmpty() || !unitPresent || qEnvironmentVariableIsSet("MURMUR_NO_SYSTEMD")) { detached(); return; }
    auto* process=new QProcess(owner);
    QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,
        [process,callback,detached](int code,QProcess::ExitStatus status) {
            process->deleteLater(); if (code==0 && status==QProcess::NormalExit) callback(true); else detached();
        });
    QObject::connect(process,&QProcess::errorOccurred,owner,[process,detached](QProcess::ProcessError e) {
        if (e==QProcess::FailedToStart) {process->deleteLater();detached();}
    });
    process->start("systemctl",{"--user","start","murmur.service"});
}
Client::Client(QObject* parent):QObject(parent) {
    socket.setReadBufferSize(65537); retry.setInterval(400);
    connect(&retry,&QTimer::timeout,this,[this]{if(socket.state()==QLocalSocket::UnconnectedState)socket.connectToServer(controlSocketPath());});
    bootTimeout.setSingleShot(true);bootTimeout.setInterval(10000);
    connect(&bootTimeout,&QTimer::timeout,this,[this] {if(!connected){booting=false;emit problem("The background service could not start. Use Start service to try again.");}});
    connect(&socket,&QLocalSocket::connected,this,[this]{connected=true;booting=false;bootTimeout.stop();emit connectionChanged(true);});
    connect(&socket,&QLocalSocket::disconnected,this,[this]{connected=false;emit connectionChanged(false);});
    connect(&socket,&QLocalSocket::errorOccurred,this,[this](QLocalSocket::LocalSocketError) {
        if (booting && !launchAttempted) {
            launchAttempted=true;
            launchDaemon(this,[this](bool ok){if(!ok){booting=false;bootTimeout.stop();emit problem("Could not launch the background service.");}});
        }
    });
    connect(&socket,&QLocalSocket::readyRead,this,&Client::read);
}
void Client::open(bool launch) {
    booting=launch;launchAttempted=false;if(launch)bootTimeout.start();
    socket.connectToServer(controlSocketPath());retry.start();
}
void Client::startService() {
    if (connected) return;
    booting=true;launchAttempted=true;bootTimeout.start();
    launchDaemon(this,[this](bool ok){if(!ok){booting=false;emit problem("Could not launch the background service.");}});
}
void Client::command(const QString& cmd,QJsonObject fields) {
    if (!connected) {emit problem("Background service is offline.");return;}
    fields["command"]=cmd;fields["id"]=++nextId;
    socket.write(QJsonDocument(fields).toJson(QJsonDocument::Compact)+'\n');
}
void Client::read() {
    while (socket.canReadLine()) {
        const auto line=socket.readLine(65537);
        if (line.size()>65536) {socket.abort();return;}
        const auto doc=QJsonDocument::fromJson(line);if(!doc.isObject())continue;
        const auto obj=doc.object();const auto type=obj.value("type").toString();
        if (type=="state") {
            if (obj.value("protocol").toInt()!=1) {emit problem("The app and background service versions do not match.");socket.abort();return;}
            emit stateReceived(obj);
        } else if(type=="meters") emit metersReceived(static_cast<float>(obj.value("input").toDouble(-60)),static_cast<float>(obj.value("output").toDouble(-60)),obj.value("limited").toBool());
        else if(type=="reply"&&!obj.value("ok").toBool())emit problem(obj.value("error").toString());
    }
    if(socket.bytesAvailable()>65536)socket.abort();
}
