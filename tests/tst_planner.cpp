#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutPlanner.h"

#include <QTimeZone>
#include <QtTest>

using namespace watt;

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
};

QTEST_GUILESS_MAIN(TestPlanner)
#include "tst_planner.moc"
