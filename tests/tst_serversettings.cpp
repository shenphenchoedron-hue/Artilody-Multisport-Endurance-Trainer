// The network server setting (ServerSettings, docs/server-plan.md step 1):
// off by default, the address kept and turned into a base URL, the station's uuid.
#include "Database.h"
#include "ServerSettings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

class TestServerSettings : public QObject
{
    Q_OBJECT
private slots:
    void offByDefaultAndKept()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("s.sqlite"));
        QString station;
        {
            Database db;
            QVERIFY(db.open(path));
            ServerSettings s(&db);
            // off: the station works on its own, as before
            QVERIFY(!s.enabled());
            QVERIFY(s.address().isEmpty());
            QVERIFY(!s.addressValid());
            QVERIFY(!s.active());
            QVERIFY(!QUuid(s.stationId()).isNull());
            station = s.stationId();

            QSignalSpy spy(&s, &ServerSettings::changed);
            s.setEnabled(true);
            QVERIFY(!s.active());   // (no address yet)
            s.setAddress(QStringLiteral("  Artilody-Server.local  "));
            QCOMPARE(s.address(), QStringLiteral("Artilody-Server.local"));
            QCOMPARE(s.url(), QStringLiteral("http://artilody-server.local/"));
            QVERIFY(s.active());
            s.setAddress(QStringLiteral("Artilody-Server.local"));   // (unchanged: no signal)
            s.setEnabled(true);
            QCOMPARE(spy.count(), 2);
        }
        // kept: the next start has them (and the same station)
        Database db;
        QVERIFY(db.open(path));
        ServerSettings s(&db);
        QVERIFY(s.enabled());
        QCOMPARE(s.address(), QStringLiteral("Artilody-Server.local"));
        QVERIFY(s.active());
        QCOMPARE(s.stationId(), station);
        s.setEnabled(false);
        QVERIFY(!s.active());
        QCOMPARE(s.address(), QStringLiteral("Artilody-Server.local"));   // (kept for switching on again)
    }

    void addressToUrl_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("url");
        QTest::newRow("name") << "artilody-server.local" << "http://artilody-server.local/";
        QTest::newRow("ip") << "192.168.1.20" << "http://192.168.1.20/";
        QTest::newRow("ip:port") << "192.168.1.20:8080" << "http://192.168.1.20:8080/";
        QTest::newRow("http") << "http://pi500.local" << "http://pi500.local/";
        QTest::newRow("https+path") << "HTTPS://Pi500.local:8443/artilody" << "https://pi500.local:8443/artilody/";
        QTest::newRow("path/") << "pi500/artilody/" << "http://pi500/artilody/";
        QTest::newRow("empty") << "" << "";
        QTest::newRow("spaces") << "pi 500" << "";
        QTest::newRow("ftp") << "ftp://pi500" << "";
        QTest::newRow("user") << "http://lone:secret@pi500" << "";
        QTest::newRow("query") << "pi500/?x=1" << "";
        QTest::newRow("bad port") << "pi500:99999" << "";
        QTest::newRow("no host") << "http://" << "";
    }
    void addressToUrl()
    {
        QFETCH(QString, address);
        QFETCH(QString, url);
        QCOMPARE(ServerSettings::normalizedUrl(address), url);
    }
};

QTEST_GUILESS_MAIN(TestServerSettings)
#include "tst_serversettings.moc"
