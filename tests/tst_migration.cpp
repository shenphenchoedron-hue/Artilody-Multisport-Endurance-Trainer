// Schema migrations v1 -> v2 -> v3 -> v4 -> v5 (PRAGMA user_version) without data loss.
#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutBuilder.h"
#include "WorkoutLibrary.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

namespace {

// Exactly the schema of version 1 (as shipped before v2).
const char *kV1Schema[] = {
    "CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE profiles (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, weight REAL, ftp INTEGER,"
    " max_hr INTEGER, rest_hr INTEGER, cadence INTEGER, default_erg INTEGER, avatar TEXT, zones TEXT)",
    "CREATE TABLE workouts (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, category TEXT, focus TEXT,"
    " description TEXT, favorite INTEGER DEFAULT 0, built_in INTEGER DEFAULT 0,"
    " times_used INTEGER DEFAULT 0, intervals TEXT, created_at INTEGER)",
    "CREATE TABLE rides (id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " profile_id INTEGER REFERENCES profiles(id) ON DELETE CASCADE,"
    " workout_id INTEGER, workout_name TEXT, started_at INTEGER, duration INTEGER, blocks INTEGER,"
    " erg INTEGER, ftp INTEGER, avg_power REAL, max_power INTEGER, np REAL, avg_hr REAL,"
    " max_hr INTEGER, avg_cadence REAL, kj REAL, if_value REAL, tss REAL, zones TEXT)",
    "CREATE INDEX idx_rides_profile ON rides(profile_id, started_at)",
    "CREATE TABLE ride_samples (ride_id INTEGER REFERENCES rides(id) ON DELETE CASCADE, t INTEGER, power INTEGER,"
    " target INTEGER, cadence INTEGER, hr INTEGER, PRIMARY KEY (ride_id, t)) WITHOUT ROWID",
    "CREATE TABLE playlists (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)",
    "CREATE TABLE playlist_tracks (id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " playlist_id INTEGER REFERENCES playlists(id) ON DELETE CASCADE,"
    " position INTEGER, path TEXT NOT NULL, title TEXT, artist TEXT, duration_ms INTEGER DEFAULT 0)",
    "INSERT INTO settings VALUES ('currentProfile', '1')",
    "INSERT INTO settings VALUES ('workoutsSeeded', 'true')",
    "INSERT INTO profiles VALUES (1, 'Lone', 68, 262, 175, 58, 85, 1, '', '[0.55,0.75,0.87,0.94,1.05]')",
    "INSERT INTO workouts VALUES (7, 'My Sweet Spot', 'Intervals', 'SS', 'desc', 1, 0, 4,"
    " '[{\"type\":\"steady\",\"name\":\"SS\",\"duration\":600,\"start\":0.9,\"end\":0.9}]', 1700000000)",
    "INSERT INTO rides VALUES (3, 1, 7, 'My Sweet Spot', 1700000000, 600, 1, 1, 262, 230.5, 400, 236, 140, 170,"
    " 90, 138, 0.9, 13.5, '[0,0,600,0,0,0]')",
    "INSERT INTO ride_samples VALUES (3, 0, 220, 236, 90, 120)",
    "INSERT INTO playlists VALUES (1, 'Mix')",
    "INSERT INTO playlist_tracks VALUES (1, 1, 0, '/music/a.mp3', 'A', 'B', 1000)",
    "PRAGMA user_version = 1",
};

QStringList columns(const QString &dbPath, const QString &table)
{
    QStringList out;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("inspect"));
        d.setDatabaseName(dbPath);
        d.open();
        QSqlQuery q(d);
        q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
        while (q.next())
            out.append(q.value(1).toString());
    }
    QSqlDatabase::removeDatabase(QStringLiteral("inspect"));
    return out;
}

int pragmaVersion(const QString &dbPath)
{
    int v = -1;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("inspect"));
        d.setDatabaseName(dbPath);
        d.open();
        QSqlQuery q(d);
        if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
            v = q.value(0).toInt();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("inspect"));
    return v;
}

} // namespace

class TestMigration : public QObject
{
    Q_OBJECT
private slots:
    void freshDatabaseIsCurrent()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("fresh.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
            QCOMPARE(db.profiles().first().rowingFtp, 150);
            for (const Workout &w : db.workouts())
                QCOMPARE(w.sport, QStringLiteral("cycling"));
        }
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QCOMPARE(Database::kSchemaVersion, 5);
        for (const char *c : { "kind", "route_path", "route_name", "planned_duration_sec", "options" })
            QVERIFY(columns(path, QStringLiteral("planned_workouts")).contains(QString::fromLatin1(c)));
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("gear_setup")));
        QVERIFY(columns(path, QStringLiteral("ride_samples")).contains(QStringLiteral("speed")));
        QVERIFY(columns(path, QStringLiteral("workouts")).contains(QStringLiteral("sport")));
        QVERIFY(columns(path, QStringLiteral("planned_workouts")).contains(QStringLiteral("ride_id")));
        QVERIFY(columns(path, QStringLiteral("rowing_records")).contains(QStringLiteral("trace")));
    }

    void upgradesV1WithoutDataLoss()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v1.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));
        QCOMPARE(pragmaVersion(path), 1);

        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();

            // old data is intact and new fields have their defaults
            const auto p = db.profile(1);
            QVERIFY(p);
            QCOMPARE(p->name, QStringLiteral("Lone"));
            QCOMPARE(p->ftp, 262);
            QCOMPARE(p->rowingFtp, 150);
            const auto w = db.workout(7);
            QVERIFY(w);
            QCOMPARE(w->name, QStringLiteral("My Sweet Spot"));
            QCOMPARE(w->timesUsed, 4);
            QVERIFY(w->favorite);
            QCOMPARE(w->intervals.size(), 1);
            QCOMPARE(w->sport, QStringLiteral("cycling"));
            const auto r = db.ride(3);
            QVERIFY(r);
            QCOMPARE(r->workoutName, QStringLiteral("My Sweet Spot"));
            QCOMPARE(r->tss, 13.5);
            QCOMPARE(r->zoneSeconds[2], 600);
            QCOMPARE(r->sport, QStringLiteral("cycling"));
            QCOMPARE(r->distanceM, 0.0);
            QCOMPARE(db.rideSamples(3).size(), 1);
            QCOMPARE(db.playlistTracks(1).size(), 1);
            QCOMPARE(db.setting(QStringLiteral("currentProfile")).toInt(), 1);
            // seeding must not duplicate the library of an existing user
            QCOMPARE(db.workouts().size(), 1 + 1);   // + restored built-in FTP test

            // new columns/tables are writable
            Profile changed = *p;
            changed.rowingFtp = 190;
            QCOMPARE(db.saveProfile(changed), qint64(1));
            QCOMPARE(db.profile(1)->rowingFtp, 190);
            RideRecord row;
            row.profileId = 1;
            row.workoutName = QStringLiteral("2k");
            row.startedAt = QDateTime::currentDateTime();
            row.sport = QStringLiteral("rowing");
            row.distanceM = 2000;
            const qint64 rowId = db.saveRide(row, {});
            QVERIFY(rowId > 0);
            QCOMPARE(db.ride(rowId)->sport, QStringLiteral("rowing"));
            QCOMPARE(db.ride(rowId)->distanceM, 2000.0);
            QVERIFY(db.addPlannedWorkout(1, QDate(2026, 10, 23), 7) > 0);
        }
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        const QStringList rideCols = columns(path, QStringLiteral("rides"));
        QVERIFY(rideCols.contains(QStringLiteral("sport")));
        QVERIFY(rideCols.contains(QStringLiteral("distance_m")));
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("rowing_ftp")));

        // reopening an up-to-date database is a no-op
        Database again;
        QVERIFY(again.open(path));
        QCOMPARE(again.profile(1)->rowingFtp, 190);
        QCOMPARE(again.plannedWorkouts(1, {}, {}).size(), 1);
    }

    void upgradesV2WithoutDataLoss()
    {
        // A v2 database (as shipped with 1.1.x): v1 schema + the v2 step.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v2.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v2"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            const char *v2[] = {
                "ALTER TABLE workouts ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN distance_m REAL",
                "ALTER TABLE profiles ADD COLUMN rowing_ftp INTEGER NOT NULL DEFAULT 150",
                "CREATE TABLE planned_workouts (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, date TEXT NOT NULL,"
                " workout_id INTEGER NOT NULL REFERENCES workouts(id) ON DELETE CASCADE,"
                " position INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0,"
                " ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, notes TEXT)",
                "CREATE TABLE rowing_records (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, distance_m INTEGER NOT NULL,"
                " time_sec REAL NOT NULL, ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, created_at TEXT, trace TEXT)",
                "UPDATE profiles SET rowing_ftp = 177 WHERE id = 1",
                "INSERT INTO rides (id, profile_id, workout_id, workout_name, started_at, duration, sport, distance_m)"
                " VALUES (4, 1, -1, '2k', 1700100000, 480, 'rowing', 2000)",
                "INSERT INTO planned_workouts (profile_id, date, workout_id) VALUES (1, '2026-10-20', 7)",
                "INSERT INTO rowing_records (profile_id, distance_m, time_sec, ride_id, trace) VALUES (1, 2000, 480, 4, '[[0,0],[480,2000]]')",
                "PRAGMA user_version = 2",
            };
            for (const char *sql : v2)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v2"));
        QCOMPARE(pragmaVersion(path), 2);
        QVERIFY(!columns(path, QStringLiteral("profiles")).contains(QStringLiteral("gear_setup")));

        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
            QCOMPARE(db.profile(1)->ftp, 262);
            QCOMPARE(db.profile(1)->rowingFtp, 177);
            QCOMPARE(db.workout(7)->name, QStringLiteral("My Sweet Spot"));
            QCOMPARE(db.ride(3)->tss, 13.5);
            QCOMPARE(db.ride(4)->distanceM, 2000.0);
            QCOMPARE(db.ride(4)->sport, QStringLiteral("rowing"));
            const QVector<Sample> old = db.rideSamples(3);
            QCOMPARE(old.size(), 1);
            QCOMPARE(old.first().power, 220);
            QCOMPARE(old.first().speedMps, 0.0);   // no speed recorded before v3
            QCOMPARE(db.plannedWorkouts(1, {}, {}).size(), 1);
            QVERIFY(db.bestRowingRecord(1, 2000));
            QCOMPARE(db.playlistTracks(1).size(), 1);
            QCOMPARE(db.workouts().size(), 2);
            // the new columns work
            QVERIFY(db.gearSetupJson(1).isEmpty());
            QVERIFY(db.setGearSetupJson(1, QStringLiteral("{\"chainrings\":[42]}")));
            QCOMPARE(db.gearSetupJson(1), QStringLiteral("{\"chainrings\":[42]}"));
            RideRecord r;
            r.profileId = 1;
            r.workoutName = QStringLiteral("Free Ride");
            r.startedAt = QDateTime::currentDateTime();
            r.distanceM = 1234;
            Sample s;
            s.power = 200;
            s.speedMps = 9.5;
            const qint64 id = db.saveRide(r, { s });
            QVERIFY(id > 0);
            QCOMPARE(db.rideSamples(id).first().speedMps, 9.5);
        }
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("gear_setup")));
        QVERIFY(columns(path, QStringLiteral("ride_samples")).contains(QStringLiteral("speed")));
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("height_cm")));
        Database again;
        QVERIFY(again.open(path));
        QCOMPARE(again.gearSetupJson(1), QStringLiteral("{\"chainrings\":[42]}"));
    }

    void upgradesV3ToV4ProfileHeight()
    {
        // a v3 database (1.1.4 free ride test builds): the profile keeps its
        // weight, the new height is 0 (not set) and can be saved
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v3.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
        }
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v3"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            // back to v3: without height_cm (SQLite >= 3.35 can drop columns)
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE profiles DROP COLUMN height_cm")));
            QVERIFY(q.exec(QStringLiteral("UPDATE profiles SET weight = 61.5 WHERE id = 1")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 3")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v3"));
        QVERIFY(!columns(path, QStringLiteral("profiles")).contains(QStringLiteral("height_cm")));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        auto p = db.profile(1);
        QVERIFY(p);
        QCOMPARE(p->weightKg, 61.5);
        QCOMPARE(p->heightCm, 0.0);
        Profile changed = *p;
        changed.heightCm = 168;
        QCOMPARE(db.saveProfile(changed), qint64(1));
        QCOMPARE(db.profile(1)->heightCm, 168.0);
        // validation: 0 (not set) or 100..230 cm
        changed.heightCm = 50;
        QVERIFY(!changed.isValid());
        changed.heightCm = 0;
        QVERIFY(changed.isValid());
        // ProfileManager reads and writes it
        ProfileManager profiles(&db);
        profiles.load();
        QVERIFY(profiles.saveProfile({ { QStringLiteral("heightCm"), 181.0 }, { QStringLiteral("weight"), 72.0 } }));
        QCOMPARE(profiles.heightCm(), 181.0);
        QCOMPARE(db.profile(profiles.profileId())->heightCm, 181.0);
        QCOMPARE(db.profile(profiles.profileId())->weightKg, 72.0);
    }

    void upgradesV4ToV5PlanKinds()
    {
        // a v4 database (1.2.0): planned_workouts with workout_id NOT NULL
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v4.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v4"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            const char *v2to4[] = {
                "ALTER TABLE workouts ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN distance_m REAL",
                "ALTER TABLE profiles ADD COLUMN rowing_ftp INTEGER NOT NULL DEFAULT 150",
                "CREATE TABLE planned_workouts (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, date TEXT NOT NULL,"
                " workout_id INTEGER NOT NULL REFERENCES workouts(id) ON DELETE CASCADE,"
                " position INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0,"
                " ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, notes TEXT)",
                "CREATE INDEX idx_plan_profile_date ON planned_workouts(profile_id, date, position)",
                "CREATE TABLE rowing_records (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, distance_m INTEGER NOT NULL,"
                " time_sec REAL NOT NULL, ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, created_at TEXT, trace TEXT)",
                "ALTER TABLE profiles ADD COLUMN gear_setup TEXT",
                "ALTER TABLE ride_samples ADD COLUMN speed REAL",
                "ALTER TABLE profiles ADD COLUMN height_cm REAL NOT NULL DEFAULT 0",
                "INSERT INTO workouts (id, name, category, intervals, sport) VALUES (8, 'Other', 'Endurance',"
                " '[{\"type\":\"steady\",\"name\":\"E\",\"duration\":1200,\"start\":0.6,\"end\":0.6}]', 'cycling')",
                "INSERT INTO planned_workouts (id, profile_id, date, workout_id, position, completed, ride_id, notes)"
                " VALUES (5, 1, '2026-10-20', 7, 0, 1, 3, 'done')",
                "INSERT INTO planned_workouts (id, profile_id, date, workout_id, position, completed, notes)"
                " VALUES (6, 1, '2026-10-20', 8, 1, 0, 'next')",
                "INSERT INTO planned_workouts (id, profile_id, date, workout_id, position) VALUES (9, 1, '2026-10-22', 7, 0)",
                "PRAGMA user_version = 4",
            };
            for (const char *sql : v2to4)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v4"));
        QCOMPARE(pragmaVersion(path), 4);
        QVERIFY(!columns(path, QStringLiteral("planned_workouts")).contains(QStringLiteral("kind")));

        {
            Database db;
            QVERIFY(db.open(path));
            QCOMPARE(pragmaVersion(path), 5);
            // existing entries: same ids, order, completion, ride link, notes; kind workout
            QVector<PlannedWorkout> all = db.plannedWorkouts(1, {}, {});
            QCOMPARE(all.size(), 3);
            QCOMPARE(all.at(0).id, qint64(5));
            QCOMPARE(all.at(0).kind, QStringLiteral("workout"));
            QCOMPARE(all.at(0).workoutId, qint64(7));
            QVERIFY(all.at(0).completed);
            QCOMPARE(all.at(0).rideId, qint64(3));
            QCOMPARE(all.at(0).notes, QStringLiteral("done"));
            QCOMPARE(all.at(0).workoutName, QStringLiteral("My Sweet Spot"));
            QCOMPARE(all.at(0).durationSec, 600);
            QCOMPARE(all.at(1).id, qint64(6));
            QCOMPARE(all.at(1).position, 1);
            QCOMPARE(all.at(1).workoutName, QStringLiteral("Other"));
            QCOMPARE(all.at(2).id, qint64(9));

            // non-workout kinds: NULL workout_id is accepted
            PlannedWorkout fr;
            fr.profileId = 1;
            fr.date = QDate(2026, 10, 20);
            fr.kind = QStringLiteral("freeride");
            fr.plannedDurationSec = 3600;
            fr.options = QStringLiteral("{\"mode\":\"erg\",\"ergWatts\":180}");
            const qint64 frId = db.addPlannedEntry(fr);
            QVERIFY(frId > 9);   // AUTOINCREMENT continues after the copied ids
            PlannedWorkout rlv;
            rlv.profileId = 1;
            rlv.date = QDate(2026, 10, 21);
            rlv.kind = QStringLiteral("rlv");
            rlv.routePath = QStringLiteral("/videos/route/route.xml");
            rlv.routeName = QStringLiteral("Route");
            const qint64 rlvId = db.addPlannedEntry(rlv);
            QVERIFY(rlvId > frId);
            const auto frBack = db.plannedWorkout(frId);
            QVERIFY(frBack);
            QCOMPARE(frBack->kind, QStringLiteral("freeride"));
            QCOMPARE(frBack->workoutId, qint64(-1));
            QCOMPARE(frBack->position, 2);   // end of the day
            QCOMPARE(frBack->plannedDurationSec, 3600);
            QCOMPARE(frBack->options, fr.options);
            QCOMPARE(db.plannedWorkout(rlvId)->routePath, rlv.routePath);
            QCOMPARE(db.plannedWorkout(rlvId)->routeName, rlv.routeName);
            QCOMPARE(db.plannedWorkouts(1, {}, {}).size(), 5);
            // invalid: workout kind without a workout, rlv without route, unknown kind
            PlannedWorkout bad = rlv;
            bad.routePath.clear();
            QCOMPARE(db.addPlannedEntry(bad), qint64(-1));
            bad = fr;
            bad.kind = QStringLiteral("workout");
            QCOMPARE(db.addPlannedEntry(bad), qint64(-1));
            bad.kind = QStringLiteral("swim");
            QCOMPARE(db.addPlannedEntry(bad), qint64(-1));
            {
                // the CHECK constraint guards raw SQL too
                QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
                d.setDatabaseName(path);
                QVERIFY(d.open());
                QSqlQuery q(d);
                QVERIFY(!q.exec(QStringLiteral("INSERT INTO planned_workouts (profile_id, date, kind) VALUES (1, '2026-10-23', 'workout')")));
                d.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("raw"));

            // a workout entry's completion helper never ticks off other kinds
            QCOMPARE(db.completePlannedWorkout(1, 8, QDate(2026, 10, 20), -1), qint64(6));
            QVERIFY(!db.plannedWorkout(frId)->completed);

            // deleting a ride reopens entries of any kind
            QVERIFY(db.setPlannedCompleted(frId, true, 3));
            QVERIFY(db.deleteRide(3));
            QVERIFY(!db.plannedWorkout(5)->completed);
            QCOMPARE(db.plannedWorkout(5)->rideId, qint64(-1));
            QVERIFY(!db.plannedWorkout(frId)->completed);

            // FK cascade still deletes workout entries with their workout - only those
            QVERIFY(db.deleteWorkout(7));
            QVERIFY(!db.plannedWorkout(5));
            QVERIFY(!db.plannedWorkout(9));
            QVERIFY(db.plannedWorkout(6));
            QVERIFY(db.plannedWorkout(frId));
            QVERIFY(db.plannedWorkout(rlvId));
            {
                // the FK itself (not only Database::deleteWorkout's explicit delete)
                QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
                d.setDatabaseName(path);
                QVERIFY(d.open());
                QSqlQuery q(d);
                QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
                QVERIFY(q.exec(QStringLiteral("DELETE FROM workouts WHERE id = 8")));
                d.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("raw"));
            QVERIFY(!db.plannedWorkout(6));
            QCOMPARE(db.plannedWorkouts(1, {}, {}).size(), 2);
            QVERIFY(db.updatePlannedDetails(frId, 1800, QStringLiteral("{\"mode\":\"ride\"}")));
            QCOMPARE(db.plannedWorkout(frId)->plannedDurationSec, 1800);
            QVERIFY(db.updatePlannedDetails(frId, 0, QString()));
            QCOMPARE(db.plannedWorkout(frId)->plannedDurationSec, 0);
            QVERIFY(db.plannedWorkout(frId)->options.isEmpty());
        }
        // reopening a v5 database changes nothing
        Database again;
        QVERIFY(again.open(path));
        QCOMPARE(again.plannedWorkouts(1, {}, {}).size(), 2);
    }

    void partiallyMigratedColumnsAreTolerated()
    {
        // A v1 database that already has one of the v2 columns (e.g. from a
        // development build) still migrates.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("partial.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("p"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY(q.exec(QString::fromUtf8(sql)));
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE workouts ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("p"));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(db.workout(7)->sport, QStringLiteral("cycling"));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
    }

    void profileManagerRowingFtp()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        QCOMPARE(profiles.rowingFtp(), 150);
        QSignalSpy spy(&profiles, &ProfileManager::profileChanged);
        QVERIFY(profiles.setRowingFtp(210));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(db.profile(profiles.profileId())->rowingFtp, 210);
        QVERIFY(!profiles.setRowingFtp(10));   // out of range
        QCOMPARE(profiles.rowingFtp(), 210);
        QCOMPARE(profiles.ftpForSport(QStringLiteral("rowing")), 210);
        QCOMPARE(profiles.ftpForSport(QStringLiteral("cycling")), profiles.ftp());
        QVERIFY(profiles.saveProfile({ { QStringLiteral("rowingFtp"), 180 } }));
        QCOMPARE(profiles.rowingFtp(), 180);

        // helper
        Interval i;
        i.startPercent = i.endPercent = 0.5;
        QCOMPARE(targetWatts(i, QStringLiteral("rowing"), 250, 180), 90);
        QCOMPARE(targetWatts(i, QStringLiteral("cycling"), 250, 180), 125);
        QCOMPARE(ftpForSport(QString(), 250, 180), 250);
    }

    void builderAndLibrarySport()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        QVERIFY(profiles.setRowingFtp(200));
        WorkoutBuilder builder(&db, &profiles);
        builder.newWorkout(QStringLiteral("rowing"));
        QCOMPARE(builder.sport(), QStringLiteral("rowing"));
        QCOMPARE(builder.ftp(), 200);
        builder.setName(QStringLiteral("Row pyramid"));
        builder.setPercent(1, 100);
        QCOMPARE(builder.intervals()->data(builder.intervals()->index(1), IntervalModel::WattsRole).toInt(), 200);
        const qint64 id = builder.save();
        QVERIFY(id > 0);
        QCOMPARE(db.workout(id)->sport, QStringLiteral("rowing"));
        QVERIFY(builder.loadWorkout(id));
        QCOMPARE(builder.sport(), QStringLiteral("rowing"));

        WorkoutLibrary lib(&db, &profiles);
        lib.reload();
        QVERIFY(lib.roleNames().values().contains("sport"));
        lib.setSportFilter(QStringLiteral("rowing"));
        QCOMPARE(lib.rowCount(), 1);
        QCOMPARE(lib.data(lib.index(0), WorkoutLibrary::SportRole).toString(), QStringLiteral("rowing"));
        QCOMPARE(lib.get(id).value(QStringLiteral("sport")).toString(), QStringLiteral("rowing"));
        lib.setSportFilter(QStringLiteral("cycling"));
        QCOMPARE(lib.rowCount(), Workout::builtInLibrary().size());
        lib.setSportFilter(QStringLiteral("all"));
        QCOMPARE(lib.rowCount(), Workout::builtInLibrary().size() + 1);
    }
};

QTEST_GUILESS_MAIN(TestMigration)
#include "tst_migration.moc"
