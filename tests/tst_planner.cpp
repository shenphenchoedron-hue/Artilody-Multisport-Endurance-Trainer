#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvSession.h"
#include "SensorHub.h"
#include "TestRoutes.h"
#include "TrainingSession.h"
#include "VirtualGears.h"
#include "WorkoutPlanner.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <memory>

using namespace watt;

namespace {

double flat(double) { return 0.0; }

// Database + all sessions the planner can start (wired like main.cpp).
struct Sessions
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<TrainingSession> training;
    std::unique_ptr<VirtualGears> gears;
    std::unique_ptr<FreeRideSession> freeRide;
    std::unique_ptr<RlvLibrary> videos;
    std::unique_ptr<RlvSession> rlv;
    std::unique_ptr<WorkoutPlanner> planner;
    QList<qint64> rowingStarts;
    bool rowingResult = true;

    bool init(bool withSessions = true)
    {
        if (!dir.isValid() || !db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        hub->setStaleTimeoutMs(600000);
        hub->setTrainerConnected(true);
        training = std::make_unique<TrainingSession>(&db, profiles.get(), hub.get());
        training->setManualClock(true);
        gears = std::make_unique<VirtualGears>(&db, profiles.get());
        gears->setManualSave(true);
        freeRide = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get(), gears.get());
        freeRide->setManualClock(true);
        freeRide->setMinimumSaveSeconds(5);
        freeRide->setTrainingSession(training.get());
        videos = std::make_unique<RlvLibrary>(&db);
        videos->setLibraryFolder(dir.filePath(QStringLiteral("library")));
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), videos.get());
        rlv->setManualClock(true);
        rlv->setMinimumSaveSeconds(5);
        rlv->setFreeRideSession(freeRide.get());
        rlv->setTrainingSession(training.get());
        rlv->setStartBlocker(QStringLiteral("freeRide"), [this] { return freeRide->active(); });
        freeRide->setStartBlocker(QStringLiteral("rlv"), [this] { return rlv->active() || rlv->waiting(); });
        gears->setSessions(freeRide.get(), rlv.get());
        planner = std::make_unique<WorkoutPlanner>(&db, profiles.get());
        if (withSessions) {
            planner->setSessions(training.get(), freeRide.get(), rlv.get(), videos.get());
            planner->setRowingStarter([this](qint64 id) {
                rowingStarts << id;
                return rowingResult;
            });
            // completion wiring of main.cpp
            QObject::connect(freeRide.get(), &FreeRideSession::rideSaved, planner.get(), [this](qint64 rideId) {
                if (const auto ride = db.ride(rideId))
                    planner->completeFreeRide(rideId, ride->durationSec, ride->profileId);
            });
            QObject::connect(rlv.get(), &RlvSession::rideSaved, planner.get(), [this](qint64 rideId) {
                const double total = rlv->totalDistanceM();
                if (const auto ride = db.ride(rideId))
                    planner->completeVideoRide(rideId, rlv->routePath(), rlv->completed(),
                                               total > 0 ? rlv->riddenMeters() / total : 0.0, ride->profileId);
            });
        }
        return true;
    }
    // a route folder in the library: "<name>/<name>.xml" (title "Title <name>")
    QString addRoute(const QString &name, double lengthM)
    {
        const QString folder = dir.filePath(QStringLiteral("library/") + name);
        if (!QDir().mkpath(folder)
            || !testroutes::writeRoute(folder, name, lengthM, 10, flat, QStringLiteral("Title ") + name))
            return {};
        videos->rescanNow();
        return folder;
    }
    qint64 cyclingWorkout(const QString &name) const
    {
        for (const Workout &w : db.workouts())
            if (w.name == name)
                return w.id;
        return -1;
    }
    qint64 rowingWorkout()
    {
        Workout w;
        w.name = QStringLiteral("Row 2 x 5");
        w.sport = sport::rowing();
        Interval i;
        i.durationSec = 300;
        i.startPercent = i.endPercent = 0.8;
        w.intervals << i << i;
        return db.saveWorkout(w);
    }
    qint64 ride(int seconds)
    {
        RideRecord r;
        r.profileId = profiles->profileId();
        r.workoutName = QStringLiteral("x");
        r.startedAt = QDateTime::currentDateTime();
        r.durationSec = seconds;
        return db.saveRide(r, {});
    }
};

} // namespace

class TestPlanner : public QObject
{
    Q_OBJECT

    static qint64 workoutId(Database &db, int index)
    {
        return db.workouts().at(index).id;
    }

private slots:
    void crudAndMonthQuery()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        WorkoutPlanner planner(&db, &profiles);
        QSignalSpy changed(&planner, &WorkoutPlanner::planChanged);

        const qint64 w0 = workoutId(db, 0), w1 = workoutId(db, 1);
        const qint64 a = planner.addWorkout(QStringLiteral("2026-10-05"), w0);
        const qint64 b = planner.addWorkout(QDate(2026, 10, 5), w1, QStringLiteral("easy"));
        const qint64 c = planner.addWorkout(QDateTime(QDate(2026, 10, 31), QTime(18, 0)), w0);
        const qint64 d = planner.addWorkout(QStringLiteral("2026-11-01"), w1);
        QVERIFY(a > 0 && b > 0 && c > 0 && d > 0);
        QCOMPARE(changed.count(), 4);
        QCOMPARE(planner.addWorkout(QStringLiteral("not a date"), w0), qint64(-1));
        QCOMPARE(planner.addWorkout(QStringLiteral("2026-10-05"), 99999), qint64(-1));

        QVariantList oct = planner.entriesForMonth(2026, 10);
        QCOMPARE(oct.size(), 3);
        QVariantMap first = oct.at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("id")).toLongLong(), a);
        QCOMPARE(first.value(QStringLiteral("date")).toString(), QStringLiteral("2026-10-05"));
        QCOMPARE(first.value(QStringLiteral("workoutId")).toLongLong(), w0);
        QCOMPARE(first.value(QStringLiteral("name")).toString(), db.workout(w0)->name);
        QCOMPARE(first.value(QStringLiteral("sport")).toString(), QStringLiteral("cycling"));
        QCOMPARE(first.value(QStringLiteral("category")).toString(), db.workout(w0)->category);
        QCOMPARE(first.value(QStringLiteral("durationSec")).toInt(), db.workout(w0)->totalDurationSec());
        QCOMPARE(first.value(QStringLiteral("completed")).toBool(), false);
        QCOMPARE(oct.at(1).toMap().value(QStringLiteral("position")).toInt(), 1);
        QCOMPARE(oct.at(1).toMap().value(QStringLiteral("notes")).toString(), QStringLiteral("easy"));
        QCOMPARE(oct.at(2).toMap().value(QStringLiteral("date")).toString(), QStringLiteral("2026-10-31"));
        QCOMPARE(planner.entriesForMonth(2026, 11).size(), 1);
        QCOMPARE(planner.entriesForMonth(2026, 13).size(), 0);
        QCOMPARE(planner.entriesForDate(QStringLiteral("2026-10-05")).size(), 2);
        QCOMPARE(planner.entriesBetween(QStringLiteral("2026-10-30"), QStringLiteral("2026-11-02")).size(), 2);

        // move: appended at the end of the target day
        QVERIFY(planner.move(a, QStringLiteral("2026-11-01")));
        QCOMPARE(planner.entriesForDate(QStringLiteral("2026-10-05")).size(), 1);
        const QVariantList nov1 = planner.entriesForDate(QStringLiteral("2026-11-01"));
        QCOMPARE(nov1.size(), 2);
        QCOMPARE(nov1.at(1).toMap().value(QStringLiteral("id")).toLongLong(), a);

        // completion
        QVERIFY(planner.setCompleted(b, true));
        QVERIFY(planner.entry(b).value(QStringLiteral("completed")).toBool());
        QVERIFY(planner.setCompleted(b, false));
        QVERIFY(!planner.entry(b).value(QStringLiteral("completed")).toBool());

        // remove
        QVERIFY(planner.remove(c));
        QVERIFY(!planner.remove(c));
        QVERIFY(planner.entry(c).isEmpty());
        QCOMPARE(planner.entriesForMonth(2026, 10).size(), 1);
    }

    void upcomingAndAutoComplete()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        WorkoutPlanner planner(&db, &profiles);
        const QDate today = QDate::currentDate();
        const qint64 w0 = workoutId(db, 0), w1 = workoutId(db, 1);
        planner.addWorkout(today.addDays(-1), w0);                        // past: not upcoming
        const qint64 t = planner.addWorkout(today, w0);
        planner.addWorkout(today.addDays(2), w1);
        planner.addWorkout(today.addDays(5), w0);
        QVariantList up = planner.upcoming(2);
        QCOMPARE(up.size(), 2);
        QCOMPARE(up.at(0).toMap().value(QStringLiteral("id")).toLongLong(), t);
        QVERIFY(up.at(0).toMap().value(QStringLiteral("isToday")).toBool());

        RideRecord ride;
        ride.profileId = profiles.profileId();
        ride.workoutId = w0;
        ride.startedAt = QDateTime::currentDateTime();
        const qint64 rideId = db.saveRide(ride, {});
        QCOMPARE(planner.markCompletedForWorkout(w1, QVariant(), rideId), qint64(-1));   // not planned today
        QCOMPARE(planner.markCompletedForWorkout(w0, QVariant(), rideId), t);
        QCOMPARE(planner.entry(t).value(QStringLiteral("rideId")).toLongLong(), rideId);
        QCOMPARE(planner.upcoming(10).size(), 2);
        QCOMPARE(planner.markCompletedForWorkout(w0, today, rideId), qint64(-1));       // already done
    }

    void deletedWorkoutsAndProfiles()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        WorkoutPlanner planner(&db, &profiles);

        Workout custom = db.workouts().first();
        custom.id = -1;
        custom.builtIn = false;
        custom.name = QStringLiteral("Mine");
        const qint64 cid = db.saveWorkout(custom);
        planner.addWorkout(QStringLiteral("2026-10-10"), cid);
        planner.addWorkout(QStringLiteral("2026-10-11"), workoutId(db, 0));
        QCOMPARE(planner.entriesForMonth(2026, 10).size(), 2);
        QVERIFY(db.deleteWorkout(cid));
        QCOMPARE(planner.entriesForMonth(2026, 10).size(), 1);

        // entries belong to the active profile
        const qint64 first = profiles.profileId();
        const qint64 other = profiles.createProfile(QStringLiteral("Other"));
        QVERIFY(other > 0 && profiles.profileId() == other);
        QCOMPARE(planner.entriesForMonth(2026, 10).size(), 0);
        planner.addWorkout(QStringLiteral("2026-10-12"), workoutId(db, 0));
        QCOMPARE(planner.entriesForMonth(2026, 10).size(), 1);
        QVERIFY(profiles.selectProfile(first));
        const QVariantList mine = planner.entriesForMonth(2026, 10);
        QCOMPARE(mine.size(), 1);
        QCOMPARE(mine.first().toMap().value(QStringLiteral("date")).toString(), QStringLiteral("2026-10-11"));
        // other profile's entry cannot be modified from here
        QVERIFY(profiles.selectProfile(other));
        const qint64 otherEntry = planner.entriesForMonth(2026, 10).first().toMap().value(QStringLiteral("id")).toLongLong();
        QVERIFY(profiles.selectProfile(first));
        QVERIFY(!planner.remove(otherEntry));
    }

    void isoDateTimeUsesLocalDay()
    {
        // A moment in UTC names the local calendar day of that moment.
        const QDateTime utc(QDate(2026, 3, 14), QTime(23, 30), QTimeZone::utc());
        const QString text = utc.toString(Qt::ISODateWithMs);   // "2026-03-14T23:30:00.000Z"
        QCOMPARE(WorkoutPlanner::toDate(text), utc.toLocalTime().date());
        QCOMPARE(WorkoutPlanner::toDate(QStringLiteral("2026-03-14T23:30:00Z")), utc.toLocalTime().date());
        QCOMPARE(WorkoutPlanner::toDate(QStringLiteral("2026-03-14T10:00:00")), QDate(2026, 3, 14));
        QCOMPARE(WorkoutPlanner::toDate(QStringLiteral("2026-03-14")), QDate(2026, 3, 14));
        QVERIFY(!WorkoutPlanner::toDate(QStringLiteral("garbage")).isValid());
    }

    void activeEntryIsCompletedByItsWorkout()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        WorkoutPlanner planner(&db, &profiles);
        QSignalSpy activeChanged(&planner, &WorkoutPlanner::activeEntryChanged);
        const qint64 w0 = workoutId(db, 0), w1 = workoutId(db, 1);
        const QDate today = QDate::currentDate();
        const qint64 past = planner.addWorkout(today.addDays(-3), w0);
        const qint64 todays = planner.addWorkout(today, w0);
        const qint64 future = planner.addWorkout(today.addDays(5), w1);
        auto ride = [&](qint64 workout) {
            RideRecord r;
            r.profileId = profiles.profileId();
            r.workoutId = workout;
            r.workoutName = QStringLiteral("x");
            r.startedAt = QDateTime::currentDateTime();
            return db.saveRide(r, {});
        };
        auto done = [&](qint64 entry) { return planner.entry(entry).value(QStringLiteral("completed")).toBool(); };

        QCOMPARE(planner.activeEntryId(), qint64(-1));
        planner.setActiveEntry(999999);   // unknown: stays cleared
        QCOMPARE(planner.activeEntryId(), qint64(-1));
        QCOMPARE(activeChanged.count(), 0);

        // past entry started from the calendar: exactly that one is ticked off
        planner.setActiveEntry(past);
        QCOMPARE(planner.activeEntryId(), past);
        QCOMPARE(activeChanged.count(), 1);
        const qint64 r1 = ride(w0);
        QCOMPARE(planner.completeForRide(w0, today, r1, profiles.profileId()), past);
        QVERIFY(done(past));
        QCOMPARE(planner.entry(past).value(QStringLiteral("rideId")).toLongLong(), r1);
        QVERIFY(!done(todays));
        QCOMPARE(planner.activeEntryId(), qint64(-1));

        // a different workout never completes the active entry
        planner.setActiveEntry(future);
        const qint64 r2 = ride(w0);
        QCOMPARE(planner.completeForRide(w0, today, r2), todays);   // fallback: today's open entry of w0
        QVERIFY(!done(future));
        QCOMPARE(planner.activeEntryId(), future);
        const qint64 r3 = ride(w1);
        QCOMPARE(planner.completeForRide(w1, today, r3), future);   // future entry, ridden today
        QVERIFY(done(future));
        QCOMPARE(planner.activeEntryId(), qint64(-1));

        // no active entry, nothing planned today for w1: nothing changes
        QCOMPARE(planner.completeForRide(w1, today, ride(w1)), qint64(-1));
        planner.setActiveEntry(todays);
        planner.clearActiveEntry();
        QCOMPARE(planner.activeEntryId(), qint64(-1));

        // markCompletedForWorkout with an explicit rider
        const qint64 other = profiles.createProfile(QStringLiteral("Other"));
        QVERIFY(other > 0);
        const qint64 otherEntry = db.addPlannedWorkout(other, today, w1);
        QCOMPARE(planner.markCompletedForWorkout(w1, QVariant(), -1, other), otherEntry);
        QVERIFY(db.plannedWorkout(otherEntry)->completed);
    }

    void freeRideAndVideoEntries()
    {
        Sessions f;
        QVERIFY(f.init());
        WorkoutPlanner &planner = *f.planner;
        const QString folder = f.addRoute(QStringLiteral("alpe"), 3000);
        QVERIFY(!folder.isEmpty());
        QCOMPARE(f.videos->count(), 1);
        const RlvLibrary::Entry route = f.videos->entries().first();
        QSignalSpy changed(&planner, &WorkoutPlanner::planChanged);

        // Free Ride
        const qint64 fr = planner.addFreeRide(QStringLiteral("2026-10-05"), 45,
                                              { { QStringLiteral("mode"), QStringLiteral("erg") },
                                                { QStringLiteral("ergWatts"), 180 } },
                                              QStringLiteral("easy spin"));
        QVERIFY(fr > 0);
        QCOMPARE(changed.count(), 1);
        QVariantMap m = planner.entry(fr);
        QCOMPARE(m.value(QStringLiteral("kind")).toString(), QStringLiteral("freeride"));
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Free Ride · 45 min"));
        QCOMPARE(m.value(QStringLiteral("category")).toString(), QStringLiteral("Free Ride"));
        QCOMPARE(m.value(QStringLiteral("sport")).toString(), QStringLiteral("cycling"));
        QCOMPARE(m.value(QStringLiteral("workoutId")).toLongLong(), qint64(-1));
        QCOMPARE(m.value(QStringLiteral("durationSec")).toInt(), 2700);
        QCOMPARE(m.value(QStringLiteral("plannedDurationSec")).toInt(), 2700);
        QCOMPARE(m.value(QStringLiteral("notes")).toString(), QStringLiteral("easy spin"));
        QVariantMap opts = m.value(QStringLiteral("options")).toMap();
        QCOMPARE(opts.value(QStringLiteral("mode")).toString(), QStringLiteral("erg"));
        QCOMPARE(opts.value(QStringLiteral("ergWatts")).toInt(), 180);
        const qint64 open = planner.addFreeRide(QDate(2026, 10, 5), 0);   // open-ended, default options
        QCOMPARE(planner.entry(open).value(QStringLiteral("name")).toString(), QStringLiteral("Free Ride"));
        QCOMPARE(planner.entry(open).value(QStringLiteral("durationSec")).toInt(), 0);
        QCOMPARE(planner.entry(open).value(QStringLiteral("options")).toMap().value(QStringLiteral("mode")).toString(),
                 QStringLiteral("ride"));
        QCOMPARE(planner.addFreeRide(QStringLiteral("2026-10-05"), 60, { { QStringLiteral("mode"), QStringLiteral("fly") } }),
                 qint64(-1));
        QVERIFY(!planner.lastError().isEmpty());
        QCOMPARE(planner.addFreeRide(QStringLiteral("nope")), qint64(-1));

        // Video Ride: the folder resolves to the library's route file
        const qint64 v = planner.addVideoRide(QStringLiteral("2026-10-06"), QUrl::fromLocalFile(folder).toString(),
                                              { { QStringLiteral("mode"), QStringLiteral("sim") },
                                                { QStringLiteral("difficulty"), 70 } });
        QVERIFY2(v > 0, qPrintable(planner.lastError()));
        QVERIFY(planner.lastError().isEmpty());
        m = planner.entry(v);
        QCOMPARE(m.value(QStringLiteral("kind")).toString(), QStringLiteral("rlv"));
        QCOMPARE(m.value(QStringLiteral("routePath")).toString(), route.path);
        QCOMPARE(m.value(QStringLiteral("routeName")).toString(), QStringLiteral("Title alpe"));
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Video: Title alpe · 3.0 km"));
        QCOMPARE(m.value(QStringLiteral("category")).toString(), QStringLiteral("Video Ride"));
        QCOMPARE(m.value(QStringLiteral("sport")).toString(), QStringLiteral("cycling"));
        QCOMPARE(m.value(QStringLiteral("routeDistanceM")).toDouble(), route.distanceM);
        QVERIFY(m.value(QStringLiteral("routeAvailable")).toBool());
        QCOMPARE(m.value(QStringLiteral("durationSec")).toInt(), route.durationSec);
        QCOMPARE(m.value(QStringLiteral("workoutId")).toLongLong(), qint64(-1));
        opts = m.value(QStringLiteral("options")).toMap();
        QCOMPARE(opts.value(QStringLiteral("mode")).toString(), QStringLiteral("sim"));
        QCOMPARE(opts.value(QStringLiteral("difficulty")).toInt(), 70);
        QCOMPARE(opts.value(QStringLiteral("workoutId")).toLongLong(), qint64(-1));
        QVERIFY(opts.value(QStringLiteral("workoutErg")).toBool());

        // with a workout during the video
        const qint64 ss = f.cyclingWorkout(QStringLiteral("Sweet Spot 3 × 12"));
        QVERIFY(ss > 0);
        const qint64 vw = planner.addVideoRide(QStringLiteral("2026-10-06"), route.path,
                                               { { QStringLiteral("mode"), QStringLiteral("erg") },
                                                 { QStringLiteral("workoutId"), ss } });
        QVERIFY(vw > 0);
        m = planner.entry(vw);
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Video: Title alpe + Sweet Spot 3 × 12"));
        QCOMPARE(m.value(QStringLiteral("workoutId")).toLongLong(), ss);
        QCOMPARE(m.value(QStringLiteral("workoutName")).toString(), QStringLiteral("Sweet Spot 3 × 12"));
        QCOMPARE(m.value(QStringLiteral("durationSec")).toInt(), f.db.workout(ss)->totalDurationSec());

        // invalid: rowing workout, unknown workout, unknown route
        const qint64 row = f.rowingWorkout();
        QCOMPARE(planner.addVideoRide(QStringLiteral("2026-10-06"), route.path, { { QStringLiteral("workoutId"), row } }),
                 qint64(-1));
        QCOMPARE(planner.addVideoRide(QStringLiteral("2026-10-06"), route.path, { { QStringLiteral("workoutId"), 99999 } }),
                 qint64(-1));
        QCOMPARE(planner.addVideoRide(QStringLiteral("2026-10-06"), f.dir.filePath(QStringLiteral("nothing"))), qint64(-1));
        QVERIFY(planner.lastError().startsWith(QStringLiteral("Route not found")));

        // kinds and sports
        const qint64 w = planner.addWorkout(QStringLiteral("2026-10-07"), ss);
        const qint64 rw = planner.addWorkout(QStringLiteral("2026-10-07"), row);
        QCOMPARE(planner.kindFor(fr), QStringLiteral("freeride"));
        QCOMPARE(planner.kindFor(planner.entry(v)), QStringLiteral("rlv"));
        QCOMPARE(planner.kindFor(w), QStringLiteral("workout"));
        QCOMPARE(planner.kindFor(QVariantMap { { QStringLiteral("id"), vw } }), QStringLiteral("rlv"));
        QCOMPARE(planner.kindFor(123456), QStringLiteral("workout"));
        QVERIFY(!planner.isRowing(fr));
        QVERIFY(!planner.isRowing(v));
        QVERIFY(planner.isRowing(rw));
        QCOMPARE(planner.sportFor(vw), QStringLiteral("cycling"));

        // listing: all kinds, ordered by day and position
        const QVariantList oct = planner.entriesForMonth(2026, 10);
        QCOMPARE(oct.size(), 6);
        QStringList kinds;
        for (const QVariant &e : oct)
            kinds << e.toMap().value(QStringLiteral("kind")).toString();
        QCOMPARE(kinds, QStringList({ QStringLiteral("freeride"), QStringLiteral("freeride"), QStringLiteral("rlv"),
                                      QStringLiteral("rlv"), QStringLiteral("workout"), QStringLiteral("workout") }));
        QCOMPARE(planner.entriesForDate(QStringLiteral("2026-10-06")).size(), 2);

        // editing options / duration
        QVERIFY(planner.updateEntry(fr, { { QStringLiteral("mode"), QStringLiteral("ride") } }, 30));
        QCOMPARE(planner.entry(fr).value(QStringLiteral("name")).toString(), QStringLiteral("Free Ride · 30 min"));
        QCOMPARE(planner.entry(fr).value(QStringLiteral("options")).toMap().value(QStringLiteral("mode")).toString(),
                 QStringLiteral("ride"));
        QVERIFY(planner.updateEntry(v, { { QStringLiteral("difficulty"), 40 } }));
        opts = planner.entry(v).value(QStringLiteral("options")).toMap();
        QCOMPARE(opts.value(QStringLiteral("difficulty")).toInt(), 40);
        QCOMPARE(opts.value(QStringLiteral("mode")).toString(), QStringLiteral("sim"));   // kept
        QVERIFY(planner.updateEntry(v, { { QStringLiteral("workoutId"), ss } }));
        QCOMPARE(planner.entry(v).value(QStringLiteral("workoutId")).toLongLong(), ss);
        QVERIFY(planner.updateEntry(v, { { QStringLiteral("workoutId"), -1 } }));
        QCOMPARE(planner.entry(v).value(QStringLiteral("workoutId")).toLongLong(), qint64(-1));
        QVERIFY(!planner.updateEntry(w, { { QStringLiteral("mode"), QStringLiteral("erg") } }));
        QVERIFY(!planner.updateEntry(v, { { QStringLiteral("mode"), QStringLiteral("bogus") } }));

        // a deleted workout: its workout entry goes, the video entry keeps the route
        Workout copy = *f.db.workout(ss);
        copy.id = -1;
        copy.builtIn = false;
        const qint64 own = f.db.saveWorkout(copy);
        QVERIFY(planner.updateEntry(vw, { { QStringLiteral("workoutId"), own } }));
        const qint64 wOwn = planner.addWorkout(QStringLiteral("2026-10-07"), own);
        QVERIFY(f.db.deleteWorkout(own));
        QVERIFY(planner.entry(wOwn).isEmpty());
        m = planner.entry(vw);
        QCOMPARE(m.value(QStringLiteral("workoutId")).toLongLong(), qint64(-1));
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Video: Title alpe · 3.0 km"));

        // without the library: the cached name (route file still loadable)
        Sessions g;
        QVERIFY(g.init(false));
        const qint64 cached = g.planner->addVideoRide(QStringLiteral("2026-10-08"), route.path);
        QVERIFY2(cached > 0, qPrintable(g.planner->lastError()));
        m = g.planner->entry(cached);
        QCOMPARE(m.value(QStringLiteral("routeName")).toString(), QStringLiteral("Title alpe"));
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Video: Title alpe"));
        QVERIFY(!m.value(QStringLiteral("routeAvailable")).toBool());
        QCOMPARE(m.value(QStringLiteral("routeDistanceM")).toDouble(), 0.0);
        // ... and no sessions to start anything
        QCOMPARE(g.planner->startEntry(cached), QString());
        QVERIFY(!g.planner->lastError().isEmpty());
    }

    void startEntryEachKind()
    {
        Sessions f;
        QVERIFY(f.init());
        WorkoutPlanner &planner = *f.planner;
        const QString folder = f.addRoute(QStringLiteral("hill"), 2000);
        QVERIFY(!folder.isEmpty());
        const QString routeFile = f.videos->entries().first().path;
        const qint64 ss = f.cyclingWorkout(QStringLiteral("Sweet Spot 3 × 12"));
        const qint64 row = f.rowingWorkout();
        const QDate today = QDate::currentDate();

        // cycling workout -> Training
        const qint64 w = planner.addWorkout(today, ss);
        QCOMPARE(planner.startEntry(w), QStringLiteral("dashboard"));
        QCOMPARE(f.training->workoutId(), ss);
        QCOMPARE(planner.activeEntryId(), w);

        // rowing workout -> Rowing.setupPlan (injected)
        const qint64 r = planner.addWorkout(today, row);
        QCOMPARE(planner.startEntry(planner.entry(r)), QStringLiteral("rowing"));
        QCOMPARE(f.rowingStarts, QList<qint64>({ row }));
        QCOMPARE(planner.activeEntryId(), r);
        f.rowingResult = false;
        planner.clearActiveEntry();
        QCOMPARE(planner.startEntry(r), QString());
        QVERIFY(planner.lastError().contains(QStringLiteral("rowing")));
        QCOMPARE(planner.activeEntryId(), qint64(-1));

        // free ride -> FreeRide mode / ERG watts / planned duration
        const qint64 fr = planner.addFreeRide(today, 45, { { QStringLiteral("mode"), QStringLiteral("erg") },
                                                           { QStringLiteral("ergWatts"), 185 } });
        QCOMPARE(planner.startEntry(fr), QStringLiteral("freeride"));
        QCOMPARE(f.freeRide->mode(), QStringLiteral("erg"));
        QCOMPARE(f.freeRide->targetWatts(), 185);
        QCOMPARE(f.freeRide->plannedDurationSec(), 2700);
        QCOMPARE(planner.activeEntryId(), fr);
        // another entry: the planned duration goes with the free ride entry
        QCOMPARE(planner.startEntry(w), QStringLiteral("dashboard"));
        QCOMPARE(f.freeRide->plannedDurationSec(), 0);
        QCOMPARE(planner.startEntry(fr), QStringLiteral("freeride"));
        planner.clearActiveEntry();
        QCOMPARE(f.freeRide->plannedDurationSec(), 0);

        // video ride with a workout, workout ERG off
        const qint64 v = planner.addVideoRide(today, folder,
                                              { { QStringLiteral("mode"), QStringLiteral("erg") },
                                                { QStringLiteral("difficulty"), 60 },
                                                { QStringLiteral("ergWatts"), 210 },
                                                { QStringLiteral("workoutId"), ss },
                                                { QStringLiteral("workoutErg"), false } });
        QVERIFY(v > 0);
        QCOMPARE(planner.startEntry(v), QStringLiteral("video"));
        QVERIFY(planner.lastError().isEmpty());
        QCOMPARE(f.rlv->routePath(), routeFile);
        QCOMPARE(f.rlv->mode(), QStringLiteral("erg"));
        QCOMPARE(f.rlv->difficultyPercent(), 60);
        QCOMPARE(f.rlv->targetWatts(), 210);
        QVERIFY(f.rlv->hasWorkout());
        QCOMPARE(f.rlv->workoutId(), ss);
        QVERIFY(!f.rlv->workoutErg());
        QCOMPARE(planner.activeEntryId(), v);
        // without a workout: a previously attached one is detached
        const qint64 v2 = planner.addVideoRide(today, routeFile);
        QCOMPARE(planner.startEntry(v2), QStringLiteral("video"));
        QVERIFY(!f.rlv->hasWorkout());
        QCOMPARE(f.rlv->mode(), QStringLiteral("sim"));
        QCOMPARE(f.rlv->difficultyPercent(), 100);

        // a running video ride: nothing is changed
        f.hub->updateCadence(90);
        f.hub->updatePower(150);
        QVERIFY(f.rlv->start());
        QCOMPARE(planner.startEntry(v), QString());
        QVERIFY(planner.lastError().contains(QStringLiteral("video ride")));
        QCOMPARE(planner.activeEntryId(), v2);
        f.rlv->discard();

        // a missing route
        QVERIFY(QDir(folder).removeRecursively());
        f.videos->rescanNow();
        QCOMPARE(planner.startEntry(v), QString());
        QCOMPARE(planner.lastError(), QStringLiteral("Route not found: Title hill"));
        QCOMPARE(planner.activeEntryId(), v2);
        // the entry is still listed (cached name)
        QCOMPARE(planner.entry(v).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Video: Title hill + Sweet Spot 3 × 12"));
        QVERIFY(!planner.entry(v).value(QStringLiteral("routeAvailable")).toBool());

        // unknown entries
        QCOMPARE(planner.startEntry(987654), QString());
        QCOMPARE(planner.lastError(), QStringLiteral("Plan entry not found"));
    }

    void completionRules()
    {
        QVERIFY(WorkoutPlanner::freeRideQualifies(1800, 3600));
        QVERIFY(!WorkoutPlanner::freeRideQualifies(1799, 3600));
        QVERIFY(WorkoutPlanner::freeRideQualifies(600, 0));
        QVERIFY(!WorkoutPlanner::freeRideQualifies(599, 0));
        QVERIFY(WorkoutPlanner::videoRideQualifies(true, 0.1));
        QVERIFY(WorkoutPlanner::videoRideQualifies(false, 0.9));
        QVERIFY(!WorkoutPlanner::videoRideQualifies(false, 0.89));

        Sessions f;
        QVERIFY(f.init());
        WorkoutPlanner &planner = *f.planner;
        const QString folder = f.addRoute(QStringLiteral("loop"), 2000);
        const QString routeFile = f.videos->entries().first().path;
        const QDate today = QDate::currentDate();
        const qint64 fr = planner.addFreeRide(today, 60);
        const qint64 v = planner.addVideoRide(today, routeFile);
        const qint64 ride1 = f.ride(1000), ride2 = f.ride(2000), ride3 = f.ride(3000);

        // nothing active: nothing completed
        QCOMPARE(planner.completeFreeRide(ride2, 2000), qint64(-1));
        planner.setActiveEntry(fr);
        QCOMPARE(planner.completeVideoRide(ride2, routeFile, true, 1.0), qint64(-1));   // wrong kind
        QCOMPARE(planner.completeFreeRide(ride1, 1000), qint64(-1));                   // < 50 % of 60 min
        QCOMPARE(planner.activeEntryId(), fr);                                         // still active
        QCOMPARE(planner.completeFreeRide(ride2, 2000, f.profiles->profileId() + 100), qint64(-1));   // other rider
        QCOMPARE(planner.completeFreeRide(ride2, 2000), fr);
        QVERIFY(f.db.plannedWorkout(fr)->completed);
        QCOMPARE(f.db.plannedWorkout(fr)->rideId, ride2);
        QCOMPARE(planner.activeEntryId(), qint64(-1));

        planner.setActiveEntry(v);
        QCOMPARE(planner.completeVideoRide(ride3, f.dir.filePath(QStringLiteral("other.xml")), true, 1.0), qint64(-1));
        QCOMPARE(planner.completeVideoRide(ride3, routeFile, false, 0.5), qint64(-1));
        QCOMPARE(planner.completeVideoRide(ride3, folder, false, 0.95), v);   // folder of the route file
        QVERIFY(f.db.plannedWorkout(v)->completed);
        QCOMPARE(f.db.plannedWorkout(v)->rideId, ride3);

        // deleting the ride reopens the entry (any kind)
        QVERIFY(f.db.deleteRide(ride2));
        QVERIFY(!f.db.plannedWorkout(fr)->completed);
        QVERIFY(f.db.deleteRide(ride3));
        QVERIFY(!f.db.plannedWorkout(v)->completed);
        // open-ended free ride: 10 minutes
        const qint64 open = planner.addFreeRide(today, 0);
        planner.setActiveEntry(open);
        QCOMPARE(planner.completeFreeRide(ride1, 599), qint64(-1));
        QCOMPARE(planner.completeFreeRide(ride1, 600), open);
        // a workout completion never ticks off a free ride / video entry
        planner.setActiveEntry(fr);
        const qint64 ss = f.cyclingWorkout(QStringLiteral("Sweet Spot 3 × 12"));
        QCOMPARE(planner.completeForRide(ss, today, ride1), qint64(-1));
        QCOMPARE(planner.activeEntryId(), fr);
    }

    void savedRidesCompleteTheirEntries()
    {
        // end to end through the sessions (main.cpp wiring)
        Sessions f;
        QVERIFY(f.init());
        WorkoutPlanner &planner = *f.planner;
        f.addRoute(QStringLiteral("short"), 400);
        const QString routeFile = f.videos->entries().first().path;
        const QDate today = QDate::currentDate();

        // free ride of 1 min planned: 30 s qualify
        const qint64 fr = planner.addFreeRide(today, 1);
        QCOMPARE(planner.startEntry(fr), QStringLiteral("freeride"));
        f.hub->updateCadence(90);
        f.hub->updatePower(180);
        QVERIFY(f.freeRide->start());
        for (int t = 0; t < 20000; t += 250)
            f.freeRide->advanceClock(250);
        QVERIFY(f.freeRide->finish(true) > 0);   // 20 s: saved, not enough
        QVERIFY(!f.db.plannedWorkout(fr)->completed);
        QCOMPARE(planner.activeEntryId(), fr);
        QVERIFY(f.freeRide->start());
        for (int t = 0; t < 35000; t += 250)
            f.freeRide->advanceClock(250);
        const qint64 frRide = f.freeRide->finish(true);
        QVERIFY(frRide > 0);
        QVERIFY(f.db.plannedWorkout(fr)->completed);
        QCOMPARE(f.db.plannedWorkout(fr)->rideId, frRide);
        QCOMPARE(planner.activeEntryId(), qint64(-1));

        // video ride with a workout: the route end completes it (workout unfinished)
        const qint64 ss = f.cyclingWorkout(QStringLiteral("Sweet Spot 3 × 12"));
        const qint64 v = planner.addVideoRide(today, routeFile, { { QStringLiteral("workoutId"), ss } });
        QCOMPARE(planner.startEntry(v), QStringLiteral("video"));
        QVERIFY(f.rlv->start());
        for (int t = 0; t < 600000 && f.rlv->running(); t += 250)
            f.rlv->advanceClock(250);
        QVERIFY(f.rlv->completed());
        QVERIFY(!f.rlv->workoutFinished());
        const qint64 vRide = f.rlv->lastRideId();
        QVERIFY(vRide > 0);
        QCOMPARE(f.db.ride(vRide)->workoutId, ss);
        QVERIFY(f.db.plannedWorkout(v)->completed);
        QCOMPARE(f.db.plannedWorkout(v)->rideId, vRide);
        // the workout's own entries are not ticked off by the video ride
        const qint64 w = planner.addWorkout(today, ss);
        QVERIFY(!f.db.plannedWorkout(w)->completed);
    }
};

QTEST_GUILESS_MAIN(TestPlanner)
#include "tst_planner.moc"
