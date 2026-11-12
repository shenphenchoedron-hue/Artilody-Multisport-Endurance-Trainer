// Is the server there? (ServerConnection, docs/server-plan.md step 2) - against
// a small local HTTP server standing in for the Pi's api/ping.php.
#include "Database.h"
#include "ServerConnection.h"
#include "ServerSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace watt;

namespace {

// Answers every request with `status` and `body`; keeps the last request.
class FakeHttp : public QObject
{
public:
    QTcpServer server;
    int status = 200;
    QByteArray body;
    QByteArray lastRequest;
    int requests = 0;

    FakeHttp()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    QByteArray &buf = pending[s];
                    buf += s->readAll();
                    if (!buf.contains("\r\n\r\n"))
                        return;
                    lastRequest = buf;
                    ++requests;
                    pending.remove(s);
                    s->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error")
                             + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                             + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString address() const { return QStringLiteral("127.0.0.1:%1").arg(server.serverPort()); }

private:
    QHash<QTcpSocket *, QByteArray> pending;
};

const QByteArray kPing = R"({"app":"artilody-server","api":1,"version":"0.1.0","name":"Artilody server",)"
                         R"("time":"2026-11-12T18:00:00+00:00","db":true,"station":true})";

} // namespace

class TestServerConnection : public QObject
{
    Q_OBJECT
private slots:
    void offUntilSwitchedOn()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ServerSettings settings(&db);
        ServerConnection c(&settings, QStringLiteral("9.9.9"));
        QCOMPARE(c.state(), QStringLiteral("off"));
        c.checkNow();   // (off: nothing asked)
        settings.setAddress(QStringLiteral("127.0.0.1:9"));
        QCOMPARE(c.state(), QStringLiteral("off"));   // (an address, but switched off)
    }

    void connectsAndIntroducesTheStation()
    {
        FakeHttp http;
        http.body = kPing;
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ServerSettings settings(&db);
        settings.setAddress(http.address());
        ServerConnection c(&settings, QStringLiteral("9.9.9"));
        settings.setEnabled(true);
        QCOMPARE(c.state(), QStringLiteral("checking"));
        QTRY_COMPARE(c.state(), QStringLiteral("connected"));
        QVERIFY(c.connected());
        QCOMPARE(c.serverName(), QStringLiteral("Artilody server"));
        QCOMPARE(c.serverVersion(), QStringLiteral("0.1.0"));
        QVERIFY(c.databaseOk());
        QVERIFY(c.message().isEmpty());
        QVERIFY(c.lastContact().isValid());
        // the ping: its path and who is asking
        QVERIFY(http.lastRequest.startsWith("GET /api/ping.php HTTP/1.1"));
        const QByteArray req = http.lastRequest.toLower();
        QVERIFY(req.contains("x-artilody-station: " + settings.stationId().toLatin1()));
        QVERIFY(req.contains("x-artilody-version: 9.9.9"));
        QVERIFY(req.contains("x-artilody-name: "));

        // asked again on the interval
        c.setIntervalMs(50);
        const int before = http.requests;
        QTRY_VERIFY(http.requests >= before + 2);
        QCOMPARE(c.state(), QStringLiteral("connected"));

        // the station not recorded (headers / sizes disagree): connected, with a warning
        http.body = QByteArray(kPing).replace(R"("station":true)", R"("station":false)");
        QTRY_VERIFY(c.message().contains(QStringLiteral("record")));
        QVERIFY(c.connected());
        QVERIFY(c.databaseOk());

        // the server's database down: connected, with a warning
        http.body = QByteArray(kPing).replace(R"("db":true)", R"("db":false)");
        QTRY_VERIFY(!c.databaseOk());
        QVERIFY(c.connected());
        QVERIFY(!c.message().isEmpty());

        // a fault on the server (PHP error): not "not an Artilody server"
        http.status = 500;
        http.body = "<b>Fatal error</b>";
        QTRY_COMPARE(c.state(), QStringLiteral("unreachable"));
        QVERIFY(c.message().contains(QStringLiteral("500")));
        QVERIFY(c.message().contains(QStringLiteral("problem")));
        QVERIFY(c.lastContact().isValid());   // (kept: shown as the last contact)
    }

    void serverTextIsCut()
    {
        // the server's name and version are shown as they come: cut, one line
        FakeHttp http;
        // (built with QJsonDocument: moc misreads some raw string literals)
        QJsonObject o;
        o.insert(QStringLiteral("app"), QStringLiteral("artilody-server"));
        o.insert(QStringLiteral("api"), 1);
        o.insert(QStringLiteral("version"), QStringLiteral("1.0\n<b>x</b>                         long"));
        o.insert(QStringLiteral("name"), QString(300, QLatin1Char('n')));
        o.insert(QStringLiteral("db"), true);
        o.insert(QStringLiteral("station"), true);
        http.body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ServerSettings settings(&db);
        settings.setAddress(http.address());
        settings.setEnabled(true);
        ServerConnection c(&settings, QStringLiteral("9.9.9"));
        QTRY_VERIFY(c.connected());
        QCOMPARE(c.serverName().size(), 80);
        QVERIFY(c.serverVersion().size() <= 20);
        QVERIFY(!c.serverVersion().contains(QLatin1Char('\n')));
    }

    void notAnArtilodyServer()
    {
        FakeHttp http;
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ServerSettings settings(&db);
        settings.setAddress(http.address());
        settings.setEnabled(true);
        // a web server, but no Artilody API there
        http.status = 404;
        http.body = "not found";
        ServerConnection c(&settings, QStringLiteral("9.9.9"));
        QTRY_COMPARE(c.state(), QStringLiteral("invalid"));
        QVERIFY(c.message().contains(QStringLiteral("404")));
        // something else answering with JSON
        http.status = 200;
        http.body = R"({"hello":"world"})";
        c.checkNow();
        QTRY_VERIFY(http.requests >= 2);
        QTRY_COMPARE(c.state(), QStringLiteral("invalid"));
        QVERIFY(!c.connected());
    }

    void unreachableThenBackAndOff()
    {
        // a port nobody listens on (the server switched off)
        quint16 port;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost));
            port = probe.serverPort();
        }
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ServerSettings settings(&db);
        settings.setAddress(QStringLiteral("127.0.0.1:%1").arg(port));
        settings.setEnabled(true);
        ServerConnection c(&settings, QStringLiteral("9.9.9"));
        // (refused at once, or - on some systems - the 5 s timeout)
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QStringLiteral("unreachable"), ServerConnection::kTimeoutMs + 3000);
        QVERIFY(!c.message().isEmpty());

        // the server switched on: found at the next try
        FakeHttp http;
        http.body = kPing;
        c.setIntervalMs(50);
        settings.setAddress(http.address());
        QTRY_COMPARE(c.state(), QStringLiteral("connected"));

        // switched off in the settings: off, not asked any more
        settings.setEnabled(false);
        QCOMPARE(c.state(), QStringLiteral("off"));
        QVERIFY(c.serverName().isEmpty());
        QTest::qWait(100);   // (one already on its way when switched off may still arrive)
        const int asked = http.requests;
        QTest::qWait(300);
        QCOMPARE(http.requests, asked);
        QCOMPARE(c.state(), QStringLiteral("off"));
    }
};

QTEST_GUILESS_MAIN(TestServerConnection)
#include "tst_serverconnection.moc"
