#include <QtTest>

#include "Database.h"
#include "RouteGhost.h"

using namespace watt;

// The ghost of the GPX Ride / Treadmill Run (RouteGhost): traces, places on
// the route, recording, choices and the gap.
class TestRouteGhost : public QObject
{
    Q_OBJECT

    qint64 profileId(Database &db) { return db.profiles().first().id; }

    qint64 addRide(Database &db)
    {
        RideRecord r;
        r.profileId = profileId(db);
        r.startedAt = QDateTime::currentDateTime();
        r.durationSec = 60;
        QVector<Sample> samples(60);
        for (int i = 0; i < samples.size(); ++i)
            samples[i].t = i;
        return db.saveRide(r, samples);
    }

    // a ride at a constant speed (m/s) for seconds, saved
    qint64 ride(Database &db, RouteGhost &g, double speed, int seconds, bool completed)
    {
        g.begin();
        for (int t = 1; t <= seconds; ++t)
            g.advance(t, speed * t);
        return g.saveRecording(addRide(db), completed);
    }

    RouteGhost::Context context(Database &db)
    {
        RouteGhost::Context c;
        c.profileId = profileId(db);
        c.routeKey = QStringLiteral("abc");
        c.routeName = QStringLiteral("Test");
        return c;
    }

private slots:
    void traceLookups()
    {
        const QVector<TracePoint> t { { 0, 0 }, { 1, 5 }, { 2, 10 }, { 4, 10 }, { 5, 20 } };
        QCOMPARE(RouteGhost::distanceAt(t, -1), 0.0);
        QCOMPARE(RouteGhost::distanceAt(t, 0.5), 2.5);
        QCOMPARE(RouteGhost::distanceAt(t, 3), 10.0);   // standing still
        QCOMPARE(RouteGhost::distanceAt(t, 99), 20.0);  // done
        QCOMPARE(RouteGhost::timeAt(t, 7.5), 1.5);
        QCOMPARE(RouteGhost::timeAt(t, 10), 2.0);       // first reached
        QCOMPARE(RouteGhost::timeAt(t, 15), 4.5);
        QCOMPARE(RouteGhost::timeAt(t, 21), -1.0);      // never
    }

    void gpxPositions()
    {
        // not looping: from the start point to the end
        auto p = RouteGhost::gpxPosition(300, 200, 1000, false, false);
        QCOMPARE(p.distance, 500.0);
        QCOMPARE(RouteGhost::gpxPosition(5000, 200, 1000, false, false).distance, 1000.0);
        // round trip: lap 1 from 200 m, then round from 0
        QCOMPARE(RouteGhost::gpxPosition(900, 200, 1000, true, true).distance, 100.0);
        QCOMPARE(RouteGhost::gpxPosition(2050, 200, 1000, true, true).distance, 250.0);
        // there and back: out to the end, back to 0, out again
        p = RouteGhost::gpxPosition(1100, 200, 1000, true, false);
        QCOMPARE(p.distance, 700.0);
        QVERIFY(p.returning);
        p = RouteGhost::gpxPosition(1900, 200, 1000, true, false);
        QCOMPARE(p.distance, 100.0);
        QVERIFY(!p.returning);
    }

    void runPositions()
    {
        QCOMPARE(RouteGhost::runPosition(1500, 1000, true).distance, 500.0);
        QCOMPARE(RouteGhost::runPosition(1000, 1000, false).distance, 1000.0);   // the end
        QCOMPARE(RouteGhost::runPosition(1300, 1000, false).distance, 300.0);    // started over (workout)
    }

    void recordsSavesAndRaces()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        RouteGhost g(&db, QStringLiteral("gpx"));
        g.setContext(context(db));
        QCOMPARE(g.rideCount(), 0);
        QVERIFY(!g.available());

        // too short: not saved
        QCOMPARE(ride(db, g, 5, 10, true), -1);
        QCOMPARE(g.rideCount(), 0);

        const qint64 slow = ride(db, g, 5, 120, true);    // 600 m
        QVERIFY(slow > 0);
        const qint64 fast = ride(db, g, 6, 100, true);    // 600 m faster
        QVERIFY(fast > 0);
        QCOMPARE(g.rideCount(), 2);
        QCOMPARE(g.choices().size(), 5);   // off, best, latest + two rides

        // "best" (the default): the fastest to the end
        g.reset();
        QVERIFY(g.available());
        QCOMPARE(g.trace().id, fast);
        QCOMPARE(g.trace().trace.size(), 101);   // [0,0] + one per second

        // racing it: after 10 s you (5 m/s) are 10 m behind, 2 s
        g.begin();
        g.advance(10, 50);
        QCOMPARE(g.coveredM(), 60.0);
        QCOMPARE(g.gapM(), -10.0);
        QVERIFY(g.hasGapSec());
        QVERIFY(std::abs(g.gapSec() - (50.0 / 6 - 10)) < 1e-9);
        QVERIFY(std::abs(g.speedKmh() - 21.6) < 1e-6);
        QVERIFY(g.shown());
        g.discardRecording();

        // a chosen ride; "latest"; off
        g.setChoice(QStringLiteral("id:%1").arg(slow));
        QCOMPARE(g.trace().id, slow);
        g.setChoice(QStringLiteral("latest"));
        QCOMPARE(g.trace().id, fast);
        g.setChoice(QStringLiteral("off"));
        QVERIFY(!g.available());
        QCOMPARE(db.setting(QStringLiteral("gpx/ghost")).toString(), QStringLiteral("off"));
        g.setChoice(QStringLiteral("best"));

        // another start / loop / route: other ghosts
        RouteGhost::Context c = context(db);
        c.startM = 100;
        g.setContext(c);
        QCOMPARE(g.rideCount(), 0);
        c.startM = 3;   // (within 5 m)
        g.setContext(c);
        QCOMPARE(g.rideCount(), 2);
        c.loop = true;
        g.setContext(c);
        QCOMPARE(g.rideCount(), 0);
        g.setContext(context(db));

        // the ghost goes with its ride
        QVERIFY(db.deleteRide(db.routeTrace(fast)->rideId));
        g.refreshChoices();
        QCOMPARE(g.rideCount(), 1);
        QVERIFY(!db.routeTrace(fast));
    }

    void quietWithoutAGhost()
    {
        // every tick of a ride without a ghost: no signals (no page / 3D work)
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        RouteGhost g(&db, QStringLiteral("gpx"));
        g.setContext(context(db));
        g.begin();
        QSignalSpy spy(&g, &RouteGhost::changed);
        for (int t = 1; t <= 20; ++t)
            g.advance(t, 5.0 * t);
        QCOMPARE(spy.count(), 0);
    }

    void bestOfLoopsIsTheFastestAverage()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        RouteGhost g(&db, QStringLiteral("run"));
        RouteGhost::Context c = context(db);
        c.loop = true;
        g.setContext(c);
        ride(db, g, 3, 600, false);
        const qint64 quick = ride(db, g, 3.5, 60, false);
        ride(db, g, 3.2, 300, true);
        g.reset();
        QCOMPARE(g.trace().id, quick);
        // after its end the ghost of a loop is gone (no route end to wait at)
        g.begin();
        g.advance(60 + RouteGhost::kLingerSec + 1, 100);
        QVERIFY(!g.shown());
        QVERIFY(g.ghostDone());
    }
};

QTEST_GUILESS_MAIN(TestRouteGhost)
#include "tst_routeghost.moc"
