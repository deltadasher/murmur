#include "window.hpp"
#include "client.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <cstdio>
#include <csignal>

static volatile std::sig_atomic_t stopping=0;
static void signalHandler(int) {stopping=1;}
static bool request(QLocalSocket& socket,const QString& command,const QJsonObject& fields,QJsonObject& reply) {
    auto obj=fields;obj["command"]=command;obj["id"]=1;
    socket.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)+'\n');socket.flush();
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<5000) {
        while(socket.canReadLine()) {
            auto json=QJsonDocument::fromJson(socket.readLine()).object();
            if(json.value("type")=="reply"&&json.value("id").toInt()==1) {
                reply=json;
                if(!json.value("ok").toBool())std::fprintf(stderr,"%s\n",qPrintable(json.value("error").toString()));
                return json.value("ok").toBool();
            }
        }
        if(!socket.waitForReadyRead(500)&&socket.state()!=QLocalSocket::ConnectedState)break;
    }
    std::fprintf(stderr,"Background service did not reply.\n");return false;
}
int main(int argc,char** argv) {
    bool cli=false;
    for(int i=1;i<argc;++i) {
        const auto arg=QString::fromLocal8Bit(argv[i]);
        if(QStringList{"--status","--list-devices","--start","--stop","--quit-daemon","--headless","--set","--help","--help-all","-h","--version","-v"}.contains(arg))cli=true;
    }
    std::unique_ptr<QCoreApplication> app;
    if(cli)app=std::make_unique<QCoreApplication>(argc,argv);else app=std::make_unique<QApplication>(argc,argv);
    app->setApplicationName("murmur");app->setOrganizationName("Murmur");app->setApplicationVersion("0.3.0");
    QCommandLineParser parser;parser.setApplicationDescription("Murmur — microphone controls with independent background audio");
    parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"status","Print daemon and audio state; never launches the daemon."});
    parser.addOption({"list-devices","Print microphones discovered by the running daemon."});
    parser.addOption({"start","Start the virtual microphone; launches the daemon if needed."});
    parser.addOption({"stop","Stop the virtual microphone and remember that it is stopped."});
    parser.addOption({"quit-daemon","Stop audio and shut down the background service."});
    parser.addOption({"headless","Alias for --start."});
    parser.addOption({"source","Microphone device identifier from --list-devices.","node"});
    parser.addOption({"preset","Built-in name or JSON preset path.","preset"});
    parser.addOption({"set","Set one control, for example bits=8 or mute=true; repeatable.","key=value"});
    parser.addOption({"monitor","Enable headphone preview when starting the microphone."});
    parser.addOption({"seconds","Close the window, or stop a CLI-started mic, after this duration.","seconds"});
    parser.addOption({"screenshot","Save a disconnected interface preview and exit.","path"});
    parser.addOption({"size","Preview/window size, e.g. 1180x780.","widthxheight"});
    parser.process(*app);
    double duration=0;
    if(parser.isSet("seconds")) {
        bool ok;duration=parser.value("seconds").toDouble(&ok);
        if(!ok||duration<=0||duration>86400){std::fprintf(stderr,"Invalid duration.\n");return 2;}
    }
    if(cli) {
        QLocalSocket socket;socket.connectToServer(controlSocketPath());
        const bool mayLaunch=parser.isSet("start")||parser.isSet("headless");
        if(!socket.waitForConnected(400)&&mayLaunch) {
            auto launched=std::make_shared<int>(-1);QEventLoop launchWait;QPointer<QEventLoop> weakWait(&launchWait);
            QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&launchWait,&QEventLoop::quit);timeout.start(5000);
            launchDaemon(app.get(),[launched,weakWait](bool ok){*launched=ok?1:0;if(weakWait)weakWait->quit();});
            if(*launched<0)launchWait.exec();
            if(*launched==1) {
                QElapsedTimer elapsed;elapsed.start();
                while(elapsed.elapsed()<5000&&socket.state()!=QLocalSocket::ConnectedState) {
                    socket.abort();socket.connectToServer(controlSocketPath());socket.waitForConnected(200);
                    if(socket.state()!=QLocalSocket::ConnectedState){QEventLoop pause;QTimer::singleShot(100,&pause,&QEventLoop::quit);pause.exec();}
                }
            }
        }
        if(socket.state()!=QLocalSocket::ConnectedState){std::fprintf(stderr,"Murmur background service is offline. Open Murmur or use --start.\n");return 1;}
        QJsonObject reply;
        if(parser.isSet("quit-daemon"))return request(socket,"shutdown",{},reply)?0:1;
        if(parser.isSet("stop"))return request(socket,"stop",{},reply)?0:1;
        QJsonObject changes;
        if(parser.isSet("preset")) {
            murmur::Parameters params;const auto name=parser.value("preset");QString error;
            if(presetNames().contains(name))params=presetParameters(name);
            else if(!readParameters(name,params,error)){std::fprintf(stderr,"%s\n",qPrintable(error));return 2;}
            changes=parametersJson(params,false);
        }
        for(const auto& assignment:parser.values("set")) {
            const auto split=assignment.indexOf('=');if(split<=0){std::fprintf(stderr,"Use --set key=value.\n");return 2;}
            const auto key=assignment.left(split),value=assignment.mid(split+1);
            if(value=="true"||value=="false")changes[key]=(value=="true");
            else {bool ok;const auto number=value.toDouble(&ok);if(!ok){std::fprintf(stderr,"Invalid numeric control.\n");return 2;}changes[key]=number;}
        }
        if(!changes.isEmpty()&&!request(socket,"set",{{"parameters",changes}},reply))return 1;
        if(mayLaunch) {
            QJsonObject fields{{"monitor",parser.isSet("monitor")}};
            if(parser.isSet("source"))fields["source"]=parser.value("source");
            if(!request(socket,"start",fields,reply))return 1;
            if(duration>0) {
                std::signal(SIGINT,signalHandler);std::signal(SIGTERM,signalHandler);
                QEventLoop delay;QTimer durationTimer;durationTimer.setSingleShot(true);QTimer signalTimer;signalTimer.setInterval(50);
                QObject::connect(&durationTimer,&QTimer::timeout,&delay,&QEventLoop::quit);
                QObject::connect(&signalTimer,&QTimer::timeout,&delay,[&]{if(stopping)delay.quit();});signalTimer.start();durationTimer.start(static_cast<int>(duration*1000));delay.exec();
                return request(socket,"stop",{},reply)?0:1;
            }
        }
        if(!request(socket,"get",{},reply))return 1;
        const auto state=reply.value("state").toObject();
        if(parser.isSet("list-devices")) {
            for(const auto& value:state.value("sources").toArray()) {
                const auto device=value.toObject();std::printf("%s\t%s\n",qPrintable(device.value("name").toString()),qPrintable(device.value("description").toString()));
            }
        } else std::printf("%s\n",QJsonDocument(state).toJson(QJsonDocument::Indented).constData());
        return 0;
    }
    auto* gui=static_cast<QApplication*>(app.get());
    const QString desktopId=qEnvironmentVariable("FLATPAK_ID").isEmpty()?QString("murmur"):qEnvironmentVariable("FLATPAK_ID");
    gui->setDesktopFileName(desktopId);gui->setWindowIcon(QIcon::fromTheme(desktopId));
    Client client;const bool demo=parser.isSet("screenshot");Window window(client,demo);
    if(parser.isSet("size")) {
        const auto parts=parser.value("size").split('x');bool widthOk=false,heightOk=false;
        const int width=parts.value(0).toInt(&widthOk),height=parts.value(1).toInt(&heightOk);
        if(parts.size()!=2||!widthOk||!heightOk||width<420||height<440||width>3840||height>2160){std::fprintf(stderr,"Invalid window size.\n");return 2;}
        window.resize(width,height);
    }
    if(parser.isSet("preset")) {
        const auto preset=parser.value("preset");
        const auto apply=[&window,preset] {if(presetNames().contains(preset))window.usePreset(preset);else window.loadFile(preset);};
        if(demo)apply();
        else {
            auto applied=std::make_shared<bool>(false);
            QObject::connect(&client,&Client::stateReceived,app.get(),[applied,apply](const QJsonObject&) {
                if(!*applied){*applied=true;apply();}
            });
        }
    }
    window.show();
    if(demo)QTimer::singleShot(350,app.get(),[&]{if(!window.grab().save(parser.value("screenshot")))app->exit(1);else app->quit();});
    else client.open();
    if(duration>0)QTimer::singleShot(static_cast<int>(duration*1000),app.get(),&QCoreApplication::quit);
    std::signal(SIGINT,signalHandler);std::signal(SIGTERM,signalHandler);
    QTimer signalTimer;signalTimer.setInterval(50);QObject::connect(&signalTimer,&QTimer::timeout,app.get(),[&]{if(stopping)app->quit();});signalTimer.start();
    return app->exec();
}
