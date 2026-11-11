#pragma once
#include <QObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QMap>
#include <QTimer>
#include <QVariantMap>
#include <optional>

// Requests permission for Flatpak audio to survive the last window closing.
// This helper reports permission only; it never starts or stops microphone audio.
class BackgroundPortal : public QObject {
    Q_OBJECT
public:
    explicit BackgroundPortal(QObject* parent=nullptr, int responseTimeoutMs=120000);
    ~BackgroundPortal() override;
    QString state() const { return currentState; }
    QString warning() const { return currentWarning; }
    void beginRequest();
signals:
    void changed();
private slots:
    void response(uint code, const QVariantMap& results, const QDBusMessage& message);
private:
    struct EarlyResponse { uint code; QVariantMap results; };
    QString currentState, currentWarning, requestPath;
    std::optional<QDBusConnection> bus;
    QTimer responseTimer;
    QMap<QString,EarlyResponse> earlyResponses;
    bool attempted=false, methodPending=false, subscribed=false;
    void finish(const QString& state, const QString& warning);
    void acceptResponse(uint code, const QVariantMap& results);
    void disconnectResponse();
    void closeRequest();
};
