// Riding together (LiveRide, docs/server-plan.md step 6) - two stations
// against a small stand-in for api/live.php.
#include "LiveRide.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace watt;

namespace {

class FakeServer : public QObject
{
public:
    QTcpServer tcp;
    QMap<QString, QJsonObject> live;   // station -> its last report (with "at")
    int asks = 0;
    qint64 extraAgeMs = 0;             // reported as older than it is

    FakeServer()
    {
        tcp.listen(QHostAddress::LocalHost);
        connect(&tcp, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = tcp.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] { onData(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/").arg(tcp.serverPort()); }

private:
    QHash<QTcpSocket *, QByteArray> buf;
    void onData(QTcpSocket *s)
    {
        QByteArray &b = buf[s];
        b += s->readAll();
        const int end = b.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        qint64 length = 0;
        for (const QByteArray &line : b.left(end).split('\n'))
            if (line.trimmed().toLower().startsWith("content-length:"))
                length = line.trimmed().mid(15).trimmed().toLongLong();
        if (b.size() < end + 4 + length)
            return;
        const QJsonObject in = QJsonDocument::fromJson(b.mid(end + 4, length)).object();
        buf.remove(s);
        ++asks;
        const QString station = in.value(QStringLiteral("station")).toString();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (in.value(QStringLiteral("riding")).toBool()) {
            QJsonObject r = in;
            r.insert(QStringLiteral("at"), now);
            live.insert(station, r);
        } else {
            live.remove(station);
        }
        QJsonArray riders;
        for (const QJsonObject &r : live) {
            if (r.value(QStringLiteral("station")).toString() == station
                || r.value(QStringLiteral("sport")) != in.value(QStringLiteral("sport"))
                || r.value(QStringLiteral("routeKey")) != in.value(QStringLiteral("routeKey")))
                continue;
            QJsonObject o = r;
            o.insert(QStringLiteral("ageMs"), now - r.value(QStringLiteral("at")).toInteger() + extraAgeMs);
            riders.append(o);
        }
        const QByteArray out = QJsonDocument(QJsonObject { { QStringLiteral("riders"), riders } }).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size())
                 + "\r\nConnection: close\r\n\r\n" + out);
        s->disconnectFromHost();
    }
};

// one station's LiveRide with a rider it can move
struct Station
{
    LiveRide live;
    LiveRide::Me me;
    explicit Station(const QString &station, const QString &name)
        : live(QStringLiteral("gpx"), station, nullptr)
    {
        me.route = true;
        me.routeKey = QStringLiteral("route-1");
        me.name = name;
        me.routeLength = 10000;
        live.setSource([this] { return me; });
    }
    void ask(FakeServer &server)
    {
        live.setServer(server.url());
        const int before = server.asks;
        live.askNow();
        QTRY_VERIFY(server.asks > before);
        QTest::qWait(30);   // (the answer)
    }
};

} // namespace

class TestLiveRide : public QObject
{
    Q_OBJECT
private slots:
    void seeEachOtherOnTheSameRoute()
    {
        FakeServer server;
        Station a(QStringLiteral("a1111111-0000-4000-8000-000000000001"), QStringLiteral("Lone"));
        Station b(QStringLiteral("b2222222-0000-4000-8000-000000000002"), QStringLiteral("Anne"));
        // A rides at 1000 m, B only looks: B sees A, A sees nobody
        a.me.riding = true;
        a.me.distanceM = 1000;
        a.me.speedKmh = 0;
        a.me.watts = 180;
        a.ask(server);
        b.me.distanceM = 0;
        b.ask(server);
        QCOMPARE(b.live.count(), 1);
        QCOMPARE(b.live.list().first().name, QStringLiteral("Lone"));
        QCOMPARE(b.live.list().first().watts, 180);
        QCOMPARE(b.live.list().first().gapM, 1000.0);   // (ahead)
        QTRY_COMPARE(a.live.count(), 0);
        a.ask(server);
        QCOMPARE(a.live.count(), 0);   // (B does not ride: not shown)

        // B rides at 1200: A sees B 200 m ahead, B sees A 200 m behind
        b.me.riding = true;
        b.me.distanceM = 1200;
        b.ask(server);
        a.ask(server);
        QCOMPARE(a.live.count(), 1);
        QCOMPARE(a.live.list().first().gapM, 200.0);
        QCOMPARE(b.live.list().first().gapM, -200.0);
        QCOMPARE(a.live.riders().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Anne"));

        // another route: not seen
        b.me.routeKey = QStringLiteral("route-2");
        b.ask(server);
        a.ask(server);
        QCOMPARE(a.live.count(), 0);
        QCOMPARE(b.live.count(), 0);

        // B finishes (back on route 1, not riding): A no longer sees B
        b.me.routeKey = QStringLiteral("route-1");
        b.me.riding = false;
        b.ask(server);
        a.ask(server);
        QCOMPARE(a.live.count(), 0);

        // no server: nobody
        b.live.setServer(QString());
        QCOMPARE(b.live.count(), 0);
    }

    void movedOnByTheReportsAgeAndTheShortWayRound()
    {
        FakeServer server;
        Station a(QStringLiteral("a1111111-0000-4000-8000-000000000001"), QStringLiteral("Lone"));
        Station b(QStringLiteral("b2222222-0000-4000-8000-000000000002"), QStringLiteral("Anne"));
        a.me.riding = true;
        a.me.distanceM = 500;
        a.me.speedKmh = 36;   // 10 m/s
        a.ask(server);
        server.extraAgeMs = 2000;   // the report 2 s old: 20 m further on
        b.me.riding = true;
        b.me.distanceM = 0;
        b.ask(server);
        QCOMPARE(b.live.count(), 1);
        // (20 m for the 2 s, a little more for the real time between the two asks)
        const double d = b.live.list().first().distanceM;
        QVERIFY2(d >= 519 && d < 545, qPrintable(QString::number(d)));
        server.extraAgeMs = 0;

        // a loop: 9900 m behind on a 10 km lap is 100 m ahead the short way round
        a.me.loop = b.me.loop = true;
        a.me.distanceM = 9950;
        a.me.speedKmh = 0;
        a.ask(server);
        b.me.distanceM = 50;
        b.ask(server);
        QVERIFY(std::abs(b.live.list().first().gapM - (-100)) < 1);
    }
};

QTEST_GUILESS_MAIN(TestLiveRide)
#include "tst_liveride.moc"
