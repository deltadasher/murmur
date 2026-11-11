#include "background_portal.hpp"
#include <QCoreApplication>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QtTest>

class FakeBackgroundPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.freedesktop.portal.Background")
public:
    enum Mode { GrantEarly, Deny, Cancel, MissingResult, WrongType, Error, Silent, AlternateEarly, UnrelatedGrant };
    Mode mode=GrantEarly;
    int calls=0;
    QVariantMap lastOptions;
public slots:
    QDBusObjectPath RequestBackground(const QString& parent,const QVariantMap& options) {
        Q_UNUSED(parent);
        ++calls;lastOptions=options;
        QString sender=message().service();sender.remove(0,1);sender.replace('.','_');
        QString path="/org/freedesktop/portal/desktop/request/"+sender+"/"+options.value("handle_token").toString();
        if(mode==Error) { sendErrorReply("org.freedesktop.portal.Error.NotAvailable","No background backend");return QDBusObjectPath("/"); }
        if(mode==AlternateEarly) path+="_alternate";
        const auto sendResponse=[this](const QString& responsePath,uint code,const QVariantMap& results) {
            auto signal=QDBusMessage::createSignal(responsePath,"org.freedesktop.portal.Request","Response");
            signal<<code<<results;QDBusConnection::sessionBus().send(signal);
        };
        if(mode==GrantEarly || mode==AlternateEarly) sendResponse(path,0,{{"background",true}});
        else if(mode==UnrelatedGrant) {
            sendResponse(path+"_unrelated",0,{{"background",true}});
            QTimer::singleShot(10,this,[path,sendResponse]{sendResponse(path,0,{{"background",false}});});
        } else if(mode!=Silent) {
            const uint code=mode==Cancel ? 1u:0u;
            const QVariantMap results=mode==MissingResult ? QVariantMap{}:
                mode==WrongType ? QVariantMap{{"background","true"}}:
                QVariantMap{{"background",mode==Cancel}};
            QTimer::singleShot(0,this,[path,code,results,sendResponse]{sendResponse(path,code,results);});
        }
        return QDBusObjectPath(path);
    }
};

class BackgroundPortalTests : public QObject {
    Q_OBJECT
    FakeBackgroundPortal fake;
    QByteArray originalFlatpak;
    bool hadFlatpak=false;
private slots:
    void initTestCase() {
        // Run only inside dbus-run-session. No real desktop portal is contacted.
        QVERIFY2(qEnvironmentVariableIsSet("MURMUR_PRIVATE_PORTAL_TEST"),"Use dbus-run-session for this test");
        hadFlatpak=qEnvironmentVariableIsSet("FLATPAK_ID");originalFlatpak=qgetenv("FLATPAK_ID");
        auto bus=QDBusConnection::sessionBus();QVERIFY(bus.isConnected());
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop",&fake,QDBusConnection::ExportAllSlots));
    }
    void cleanupTestCase() {
        auto bus=QDBusConnection::sessionBus();bus.unregisterObject("/org/freedesktop/portal/desktop");
        bus.unregisterService("org.freedesktop.portal.Desktop");
        if(hadFlatpak)qputenv("FLATPAK_ID",originalFlatpak);else qunsetenv("FLATPAK_ID");
    }
    void init() { fake.calls=0;fake.mode=FakeBackgroundPortal::GrantEarly;qputenv("FLATPAK_ID","io.github.murmur_voice.Murmur"); }
    void nativeDoesNotRequest() {
        qunsetenv("FLATPAK_ID");BackgroundPortal helper;
        QCOMPARE(helper.state(),QString("not-required"));helper.beginRequest();
        QCOMPARE(fake.calls,0);QVERIFY(helper.warning().isEmpty());
    }
    void grantsOnlyActualPermission() {
        BackgroundPortal helper;QSignalSpy changed(&helper,&BackgroundPortal::changed);helper.beginRequest();
        QTRY_COMPARE(helper.state(),QString("granted"));QVERIFY(helper.warning().isEmpty());
        QCOMPARE(fake.calls,1);QVERIFY(changed.count()>=2);
        QCOMPARE(fake.lastOptions.value("autostart").toBool(),false);
        QVERIFY(!fake.lastOptions.contains("commandline"));
        QVERIFY(!fake.lastOptions.value("reason").toString().isEmpty());
        QVERIFY(QRegularExpression("^murmur_[a-f0-9]+$").match(fake.lastOptions.value("handle_token").toString()).hasMatch());
        helper.beginRequest();QCOMPARE(fake.calls,1);
    }
    void denial_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("background-false")<<int(FakeBackgroundPortal::Deny);
        QTest::newRow("cancelled")<<int(FakeBackgroundPortal::Cancel);
        QTest::newRow("missing-result")<<int(FakeBackgroundPortal::MissingResult);
        QTest::newRow("wrong-type")<<int(FakeBackgroundPortal::WrongType);
        QTest::newRow("unrelated-grant")<<int(FakeBackgroundPortal::UnrelatedGrant);
    }
    void denial() {
        QFETCH(int,mode);fake.mode=static_cast<FakeBackgroundPortal::Mode>(mode);
        BackgroundPortal helper;helper.beginRequest();QTRY_COMPARE(helper.state(),QString("denied"));
        QVERIFY(helper.warning().contains("Keep Murmur open"));
        helper.beginRequest();QCOMPARE(fake.calls,1);
    }
    void alternateEarlyHandle() {
        fake.mode=FakeBackgroundPortal::AlternateEarly;BackgroundPortal helper;helper.beginRequest();
        QTRY_COMPARE(helper.state(),QString("granted"));
    }
    void methodError() {
        fake.mode=FakeBackgroundPortal::Error;BackgroundPortal helper;helper.beginRequest();
        QTRY_COMPARE(helper.state(),QString("unavailable"));QVERIFY(!helper.warning().isEmpty());
    }
    void timeout() {
        fake.mode=FakeBackgroundPortal::Silent;BackgroundPortal helper(nullptr,30);helper.beginRequest();
        QTRY_COMPARE(helper.state(),QString("timed-out"));QVERIFY(helper.warning().contains("not been confirmed"));
    }
};
QTEST_GUILESS_MAIN(BackgroundPortalTests)
#include "background_portal_tests.moc"
