#pragma once
#include "model.hpp"
#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QJsonObject>
#include <functional>

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject* parent=nullptr);
    void open(bool launch=true);
    void startService();
    bool isConnected() const { return connected; }
    void command(const QString&,QJsonObject fields={});
signals:
    void stateReceived(QJsonObject);
    void metersReceived(float input,float output,bool limited);
    void connectionChanged(bool);
    void problem(QString);
private:
    QLocalSocket socket;
    QTimer retry,bootTimeout;
    bool connected=false,booting=false,launchAttempted=false;
    int nextId=0;
    void read();
};
void launchDaemon(QObject* owner,std::function<void(bool)> callback);
