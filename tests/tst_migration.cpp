// Schema migration v1 -> v2 (PRAGMA user_version) without data loss.
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
        QCOMPARE(Database::kSchemaVersion, 2);
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
        QCOMPARE(pragmaVersion(path), 2);
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
        QCOMPARE(pragmaVersion(path), 2);
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
