#include "background_portal.hpp"
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>
#include <algorithm>

namespace {
const QString service="org.freedesktop.portal.Desktop";
const QString desktopPath="/org/freedesktop/portal/desktop";
const QString requestInterface="org.freedesktop.portal.Request";
const QString keepOpen="Keep Murmur open to keep your microphone available.";
}

BackgroundPortal::BackgroundPortal(QObject* parent,int responseTimeoutMs) : QObject(parent) {
    currentState=qEnvironmentVariable("FLATPAK_ID").isEmpty() ? "not-required":"idle";
    responseTimer.setSingleShot(true);
    responseTimer.setInterval(std::clamp(responseTimeoutMs,10,120000));
    connect(&responseTimer,&QTimer::timeout,this,[this] {
        closeRequest();
        finish("timed-out","Background permission has not been confirmed. "+keepOpen);
    });
}
BackgroundPortal::~BackgroundPortal() {
    if(currentState=="pending") closeRequest();
    disconnectResponse();
}
void BackgroundPortal::beginRequest() {
    if(attempted || currentState=="not-required") return;
    attempted=true;
    bus.emplace(QDBusConnection::sessionBus());
    if(!bus->isConnected()) {
        finish("unavailable","Background permission is unavailable. "+keepOpen);
        return;
    }
    QString sender=bus->baseService();
    if(sender.startsWith(':')) sender.remove(0,1);
    sender.replace('.','_');
    const auto token="murmur_"+QUuid::createUuid().toString(QUuid::Id128);
    requestPath=desktopPath+"/request/"+sender+"/"+token;
    // Subscribe before the call so a portal which replies immediately cannot
    // beat our subscription. Also tolerate an older portal returning a different
    // handle: early alternate responses are matched against the actual reply.
    subscribed=bus->connect(service,{},requestInterface,"Response",this,
                            SLOT(response(uint,QVariantMap,QDBusMessage)));
    if(!subscribed) {
        finish("unavailable","Background permission is unavailable. "+keepOpen);
        return;
    }
    currentState="pending";
    currentWarning="Requesting permission to keep your microphone available with Murmur closed…";
    emit changed();
    methodPending=true;
    responseTimer.start();
    QVariantMap options{{"handle_token",token},
        {"reason","Keep the processed microphone available to games and voice chat when Murmur's window is closed."},
        {"autostart",false}};
    auto message=QDBusMessage::createMethodCall(service,desktopPath,
                                               "org.freedesktop.portal.Background","RequestBackground");
    message<<QString()<<options;
    auto* watcher=new QDBusPendingCallWatcher(bus->asyncCall(message,30000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher] {
        const QDBusPendingReply<QDBusObjectPath> reply=*watcher;
        watcher->deleteLater();
        methodPending=false;
        if(currentState!="pending") return;
        if(reply.isError() || reply.value().path()=="/") {
            finish("unavailable","Background permission could not be requested. "+keepOpen);
            return;
        }
        requestPath=reply.value().path();
        const auto early=earlyResponses.constFind(requestPath);
        if(early!=earlyResponses.cend()) acceptResponse(early->code,early->results);
        earlyResponses.clear();
    });
}
void BackgroundPortal::response(uint code,const QVariantMap& results,const QDBusMessage& message) {
    if(currentState!="pending") return;
    if(message.path()==requestPath) {
        acceptResponse(code,results);
    } else if(methodPending && earlyResponses.size()<4 && message.path().startsWith(desktopPath+"/request/")) {
        earlyResponses.insert(message.path(),{code,results});
    }
}
void BackgroundPortal::acceptResponse(uint code,const QVariantMap& results) {
    const auto granted=results.value("background");
    if(code==0 && granted.metaType()==QMetaType::fromType<bool>() && granted.toBool())
        finish("granted",{});
    else
        finish("denied","Background access was not granted. "+keepOpen);
}
void BackgroundPortal::finish(const QString& state,const QString& warning) {
    responseTimer.stop();
    disconnectResponse();
    earlyResponses.clear();
    currentState=state;
    currentWarning=warning;
    emit changed();
}
void BackgroundPortal::disconnectResponse() {
    if(bus && subscribed)
        bus->disconnect(service,{},requestInterface,"Response",this,
                        SLOT(response(uint,QVariantMap,QDBusMessage)));
    subscribed=false;
}
void BackgroundPortal::closeRequest() {
    if(!bus || requestPath.isEmpty()) return;
    const auto message=QDBusMessage::createMethodCall(service,requestPath,requestInterface,"Close");
    bus->asyncCall(message);
}
