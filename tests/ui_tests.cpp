#include "window.hpp"
#include "model.hpp"
#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLineEdit>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTabBar>
#include <QtTest>

namespace {
QJsonObject state(murmur::Parameters params={},bool running=true) {
    return {{"audioReady",true},{"running",running},{"requested",running},
        {"source","synthetic.mic"},{"systemMuteKnown",running},{"systemMuted",false},
        {"parameters",parametersJson(params)},
        {"sources",QJsonArray{QJsonObject{{"name","synthetic.mic"},{"description","Synthetic microphone"}}}}};
}
}
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void modulesSupportKeyboardNavigation() {
        Client client;Window window(client,true);window.show();QTest::qWait(30);
        auto* tabs=window.findChild<QTabWidget*>("compactTabs");QVERIFY(tabs);
        auto* bar=tabs->tabBar();bar->setFocus();QVERIFY(bar->hasFocus());
        QTest::keyClick(bar,Qt::Key_Right);QCOMPARE(tabs->currentIndex(),1);
        QTest::keyClick(bar,Qt::Key_Right);QTest::keyClick(bar,Qt::Key_Right);QCOMPARE(tabs->currentIndex(),3);
        QTest::keyClick(bar,Qt::Key_Left);QCOMPARE(tabs->currentIndex(),2);
        QVERIFY(window.findChild<QDoubleSpinBox*>("gateValue")->isVisible());
    }
    void offlineRecovery() {
        Client client; Window window(client); window.show();
        auto* import=window.findChild<QAction*>("importPreset");
        auto* bass=window.findChild<QDoubleSpinBox*>("bassValue");
        QVERIFY(import && bass); QVERIFY(!import->isEnabled()); QVERIFY(!bass->isEnabled());
        client.stateReceived(state()); QVERIFY(import->isEnabled()); QVERIFY(bass->isEnabled());
        client.connectionChanged(false); QVERIFY(!import->isEnabled()); QVERIFY(!bass->isEnabled());
        bool guidance=false;
        for(auto* label:window.findChildren<QLabel*>())guidance|=label->text().contains("reconnect",Qt::CaseInsensitive)||label->text().contains("Start service");
        QVERIFY(guidance);
    }
    void presetPreservesSessionSwitches() {
        Client client; Window window(client); murmur::Parameters params;params.mute=true;params.bypass=true;
        client.stateReceived(state(params)); window.usePreset("Telephone");
        auto* mute=window.findChild<QAbstractButton*>("mute");auto* bypass=window.findChild<QAbstractButton*>("bypass");
        QVERIFY(mute && bypass); QVERIFY(mute->isChecked()); QVERIFY(bypass->isChecked());
        QCOMPARE(window.findChild<QDoubleSpinBox*>("rateValue")->value(),double(presetParameters("Telephone").rate));
    }
    void pendingNumericEntrySurvivesSnapshot() {
        Client client;Window window(client);window.show();murmur::Parameters params;params.bass=4;
        client.stateReceived(state(params));auto* spin=window.findChild<QDoubleSpinBox*>("bassValue");QVERIFY(spin);
        auto* edit=spin->findChild<QLineEdit*>();QVERIFY(edit);edit->setFocus();edit->selectAll();QTest::keyClicks(edit,"6");
        QCOMPARE(spin->value(),4.0);QVERIFY(edit->text().startsWith("6"));
        params.gain=-4;client.stateReceived(state(params));QVERIFY(edit->text().startsWith("6"));
        QTest::keyClick(edit,Qt::Key_Return);QCOMPARE(spin->value(),6.0);
    }
    void compactControlsRemainReachable() {
        Client client;Window window(client,true);window.resize(520,680);window.show();QTest::qWait(30);
        QVERIFY(window.width()<=520);QVERIFY(window.height()<=680);
        auto* tabs=window.findChild<QTabWidget*>("compactTabs");QVERIFY(tabs && tabs->isVisible());QCOMPARE(tabs->count(),4);
        tabs->setCurrentIndex(1);QCoreApplication::processEvents();
        for(const auto& name:{"rateValue","bitsValue","pitchValue","driveValue"}){
            auto* spin=window.findChild<QDoubleSpinBox*>(name);QVERIFY(spin && spin->isVisible());
            QWidget* parent=spin->parentWidget();while(parent&&!qobject_cast<QScrollArea*>(parent))parent=parent->parentWidget();
            if(auto* scroll=qobject_cast<QScrollArea*>(parent))scroll->ensureWidgetVisible(spin);
            QVERIFY(window.rect().contains(spin->mapTo(&window,spin->rect().center())));
        }
        for(int page=2;page<4;++page){
            tabs->setCurrentIndex(page);QCoreApplication::processEvents();
            const auto keys=page==2?QStringList{"highpass","gate","compression"}:QStringList{"echo","echoMs"};
            for(const auto& key:keys){auto* value=window.findChild<QDoubleSpinBox*>(key+"Value");QVERIFY(value&&value->isVisible());
                auto* parent=value->parentWidget();while(parent&&!qobject_cast<QScrollArea*>(parent))parent=parent->parentWidget();
                auto* scroll=qobject_cast<QScrollArea*>(parent);QVERIFY(scroll);scroll->ensureWidgetVisible(value);QCoreApplication::processEvents();
                QVERIFY(scroll->viewport()->rect().contains(QRect(value->mapTo(scroll->viewport(),QPoint()),value->size())));}
        }
        tabs->setCurrentIndex(0);auto* frequency=window.findChild<QAbstractButton*>("frequencyToggle");QVERIFY(frequency);frequency->click();
        auto* controls=window.findChild<QWidget*>("frequencyControls");QVERIFY(controls && controls->isVisible());
        window.resize(1220,840);QCoreApplication::processEvents();QVERIFY(tabs->isVisible());
        window.resize(640,720);QCoreApplication::processEvents();QVERIFY(tabs->isVisible());
        QVERIFY(window.findChild<QDoubleSpinBox*>("bassValue")->isVisible());
    }
    void individualResetAndNativeSliderInput() {
        Client client;Window window(client,true);window.usePreset("Warm");window.show();QTest::qWait(30);
        auto* bass=window.findChild<QSlider*>("bassSlider");auto* value=window.findChild<QDoubleSpinBox*>("bassValue");
        auto* treble=window.findChild<QDoubleSpinBox*>("trebleValue");QVERIFY(bass&&value&&treble);
        QTest::mouseDClick(bass,Qt::LeftButton);QCOMPARE(value->value(),0.0);QCOMPARE(treble->value(),-2.0);
        bass->setFocus();QTest::keyClick(bass,Qt::Key_Up);QCOMPARE(value->value(),.5);
        window.resize(520,680);QCoreApplication::processEvents();
        QCOMPARE(bass->orientation(),Qt::Horizontal);QTest::keyClick(bass,Qt::Key_Right);QCOMPARE(value->value(),1.0);
        auto* bits=window.findChild<QSlider*>("bitsSlider");QVERIFY(bits);
        window.findChild<QTabWidget*>("compactTabs")->setCurrentIndex(1);bits->setValue(4);QTest::mouseDClick(bits,Qt::LeftButton);
        QCOMPARE(window.findChild<QDoubleSpinBox*>("bitsValue")->value(),24.0);QCOMPARE(value->value(),1.0);
    }
    void systemMuteIsDistinct() {
        Client client;Window window(client);window.show();auto snapshot=state();snapshot["systemMuted"]=true;client.stateReceived(snapshot);
        auto* warning=window.findChild<QWidget*>("systemMuteWarning");auto* unmute=window.findChild<QAbstractButton*>("unmuteSystem");
        auto* mute=window.findChild<QAbstractButton*>("mute");QVERIFY(warning && unmute && mute);
        QVERIFY(warning->isVisible());QVERIFY(unmute->isEnabled());QVERIFY(!mute->isChecked());
        window.resize(520,680);QCoreApplication::processEvents();
        auto* treble=window.findChild<QDoubleSpinBox*>("trebleValue");auto* scroll=window.findChild<QScrollArea*>("toneScroll");
        QVERIFY(treble&&scroll);QVERIFY(scroll->viewport()->rect().contains(QRect(treble->mapTo(scroll->viewport(),QPoint()),treble->size())));
        snapshot["systemMuteKnown"]=false;client.stateReceived(snapshot);QVERIFY(!warning->isVisible());
        QVERIFY(window.findChild<QWidget*>("responsePlot")->isVisible());
        client.connectionChanged(false);QVERIFY(!unmute->isEnabled()||!unmute->isVisible());
    }
    void unmuteActionUsesIndependentCommand() {
        QTemporaryDir runtime;QVERIFY(runtime.isValid());
        struct RestoreEnvironment {
            QByteArray previous=qgetenv("XDG_RUNTIME_DIR");
            bool existed=qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
            ~RestoreEnvironment(){if(existed)qputenv("XDG_RUNTIME_DIR",previous);else qunsetenv("XDG_RUNTIME_DIR");}
        } restore;
        qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
        QVERIFY(QDir().mkpath(QFileInfo(controlSocketPath()).absolutePath()));
        QLocalServer server;QVERIFY(server.listen(controlSocketPath()));
        Client client;Window window(client);window.show();client.open(false);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(),1000);
        auto* peer=server.nextPendingConnection();QVERIFY(peer);
        murmur::Parameters params;params.mute=true;
        auto snapshot=state(params);snapshot["type"]="state";snapshot["protocol"]=1;snapshot["systemMuted"]=true;
        peer->write(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)+'\n');
        auto* unmute=window.findChild<QAbstractButton*>("unmuteSystem");QVERIFY(unmute);
        QTRY_VERIFY_WITH_TIMEOUT(unmute->isVisible(),1000);QVERIFY(unmute->isEnabled());
        unmute->click();QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(),1000);
        auto command=QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(command.value("command").toString(),QString("unmute-system"));
        QVERIFY(!command.contains("parameters"));QVERIFY(window.findChild<QAbstractButton*>("mute")->isChecked());
    }
    void newControlsSendValidatedPartialUpdates() {
        QTemporaryDir runtime;QVERIFY(runtime.isValid());
        const auto previous=qgetenv("XDG_RUNTIME_DIR");const bool existed=qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
        struct Restore {QByteArray value;bool existed;~Restore(){if(existed)qputenv("XDG_RUNTIME_DIR",value);else qunsetenv("XDG_RUNTIME_DIR");}} restore{previous,existed};
        qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());QVERIFY(QDir().mkpath(QFileInfo(controlSocketPath()).absolutePath()));
        QLocalServer server;QVERIFY(server.listen(controlSocketPath()));Client client;Window window(client);window.show();client.open(false);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(),1000);auto* peer=server.nextPendingConnection();QVERIFY(peer);
        auto snapshot=state();snapshot["type"]="state";snapshot["protocol"]=1;
        peer->write(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)+'\n');
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QDoubleSpinBox*>("echoValue")->isEnabled(),1000);
        const QMap<QString,double> edits{{"highpass",100},{"gate",-45},{"compression",55},{"drive",25},{"echo",30},{"echoMs",250}};
        for(auto it=edits.begin();it!=edits.end();++it){
            auto* spin=window.findChild<QDoubleSpinBox*>(it.key()+"Value");QVERIFY(spin);spin->setValue(it.value());
            QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(),1000);const auto command=QJsonDocument::fromJson(peer->readLine()).object();
            QCOMPARE(command.value("command").toString(),QString("set"));
            const auto patch=command.value("parameters").toObject();QCOMPARE(patch.size(),1);QCOMPARE(patch.value(it.key()).toDouble(),it.value());
            murmur::Parameters checked;QString error;QVERIFY(applyParameterPatch(patch,murmur::Parameters{},checked,error));
        }
        client.connectionChanged(false);
        for(auto it=edits.begin();it!=edits.end();++it)QVERIFY(!window.findChild<QDoubleSpinBox*>(it.key()+"Value")->isEnabled());
    }
    void deniedBackgroundAccessGivesHonestGuidance() {
        Client client;Window window(client);window.resize(520,680);window.show();auto snapshot=state();
        snapshot["backgroundState"]="denied";snapshot["backgroundWarning"]="Background access was not granted. Keep Murmur open to keep your microphone available.";
        client.stateReceived(snapshot);bool guidance=false,falsePromise=false;
        for(auto* label:window.findChildren<QLabel*>()){guidance|=label->text().contains("Keep Murmur open");falsePromise|=label->text().contains("Closing this window keeps your microphone running");}
        QVERIFY(guidance);QVERIFY(!falsePromise);
        QVERIFY(!window.findChild<QWidget*>("responsePlot")->isVisible());
        auto* treble=window.findChild<QDoubleSpinBox*>("trebleValue");auto* scroll=window.findChild<QScrollArea*>("toneScroll");
        QVERIFY(scroll->viewport()->rect().contains(QRect(treble->mapTo(scroll->viewport(),QPoint()),treble->size())));
        snapshot["backgroundState"]="granted";snapshot["backgroundWarning"]="";client.stateReceived(snapshot);
        bool granted=false;for(auto* label:window.findChildren<QLabel*>())granted|=label->text().contains("Closing this window keeps your microphone running");QVERIFY(granted);
        QVERIFY(window.findChild<QWidget*>("responsePlot")->isVisible());
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
