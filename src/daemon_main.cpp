#include "daemon.hpp"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <csignal>
static volatile std::sig_atomic_t interrupted=0;
static void handleSignal(int) {interrupted=1;}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); app.setApplicationName("murmur"); app.setOrganizationName("Murmur"); app.setApplicationVersion("0.3.0");
    QCommandLineParser parser; parser.setApplicationDescription("Murmur background audio service");parser.addHelpOption();parser.addVersionOption();parser.process(app);
    Daemon daemon; if (!daemon.listen()) return 1;
    std::signal(SIGINT,handleSignal);std::signal(SIGTERM,handleSignal);
    QTimer signalsTimer;signalsTimer.setInterval(50);QObject::connect(&signalsTimer,&QTimer::timeout,&app,[&]{if(interrupted)app.quit();});signalsTimer.start();
    return app.exec();
}
