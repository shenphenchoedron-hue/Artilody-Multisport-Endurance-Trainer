#include "Database.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

class TestDatabase : public QObject
{
    Q_OBJECT
private slots:
    void seedsDefaults()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        QCOMPARE(db.profiles().size(), 1);
        QCOMPARE(db.workouts().size(), Workout::builtInLibrary().size());
        db.seedDefaults();   // idempotent
        QCOMPARE(db.workouts().size(), Workout::builtInLibrary().size());
        QCOMPARE(db.profiles().size(), 1);
    }

    void builtInFtpTestIsProtected()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        QVERIFY(db.hasBuiltInFtpTest());
        qint64 rampId = -1;
        for (const Workout &w : db.workouts()) {
            if (w.isProtected())
                rampId = w.id;
        }
        QVERIFY(rampId > 0);
        QCOMPARE(db.workout(rampId)->name, QStringLiteral("Ramp FTP Test"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("required for FTP tests")));
        QVERIFY(!db.deleteWorkout(rampId));
        QVERIFY(db.workout(rampId));

        // personal copies (e.g. edited in the Builder) are FTP tests but deletable
        Workout copy = *db.workout(rampId);
        copy.id = -1;
        copy.builtIn = false;
        const qint64 copyId = db.saveWorkout(copy);
        QVERIFY(db.workout(copyId)->isFtpTest());
        QVERIFY(!db.workout(copyId)->isProtected());
        QVERIFY(db.deleteWorkout(copyId));
    }

    void seedRestoresMissingFtpTest()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        const int count = db.workouts().size();
        // Simulate a library from an older version where the ramp test was
        // deleted: no built-in "FTP Tests" workout is left.
        for (Workout w : db.workouts()) {
            if (w.isProtected()) {
                w.builtIn = false;
                w.category = QStringLiteral("Custom");
                QVERIFY(db.saveWorkout(w) > 0);
                QVERIFY(db.deleteWorkout(w.id));
            }
        }
        QVERIFY(!db.hasBuiltInFtpTest());
        QCOMPARE(db.workouts().size(), count - 1);

        db.seedDefaults();   // already seeded, but the FTP test comes back
        QVERIFY(db.hasBuiltInFtpTest());
        QCOMPARE(db.workouts().size(), count);
        int ramps = 0;
        for (const Workout &w : db.workouts()) {
            if (w.isProtected()) {
                ++ramps;
                QCOMPARE(w.name, QStringLiteral("Ramp FTP Test"));
                QCOMPARE(w.intervals.size(), 2);
            }
        }
        QCOMPARE(ramps, 1);
        db.seedDefaults();   // still idempotent
        QCOMPARE(db.workouts().size(), count);
    }

    void profileRoundTrip()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        Profile p;
        p.name = QStringLiteral("Lone");
        p.ftp = 260;
        p.zones.upper = { 0.5, 0.7, 0.85, 0.95, 1.1 };
        const qint64 id = db.saveProfile(p);
        QVERIFY(id > 0);
        auto back = db.profile(id);
        QVERIFY(back.has_value());
        QCOMPARE(back->name, p.name);
        QCOMPARE(back->ftp, 260);
        QCOMPARE(back->zones, p.zones);

        back->ftp = 270;
        QCOMPARE(db.saveProfile(*back), id);
        QCOMPARE(db.profile(id)->ftp, 270);
        QVERIFY(db.deleteProfile(id));
        QVERIFY(!db.profile(id).has_value());
    }

    void workoutsCrud()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        Workout w = Workout::builtInLibrary().at(2);
        w.builtIn = false;
        const qint64 id = db.saveWorkout(w);
        QVERIFY(id > 0);
        auto back = db.workout(id);
        QVERIFY(back);
        QCOMPARE(back->name, w.name);
        QCOMPARE(back->intervals.size(), w.intervals.size());
        QCOMPARE(back->intervals, w.intervals);

        QVERIFY(db.setWorkoutFavorite(id, true));
        QVERIFY(db.incrementWorkoutUsage(id));
        back = db.workout(id);
        QVERIFY(back->favorite);
        QCOMPARE(back->timesUsed, 1);
        QVERIFY(db.deleteWorkout(id));
        QVERIFY(!db.workout(id));
    }

    void ridesWithSamples()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        const qint64 profileId = db.profiles().first().id;

        RideRecord r;
        r.profileId = profileId;
        r.workoutName = QStringLiteral("Test Ride");
        r.startedAt = QDateTime::currentDateTime().addDays(-2);
        r.durationSec = 3;
        r.avgPower = 150;
        r.zoneSeconds = { 1, 2, 0, 0, 0, 0 };
        const QVector<Sample> s { { 0, 100, 100, 80, 120 }, { 1, 150, 150, 85, 125 }, { 2, 200, 150, 90, 130 } };
        const qint64 rideId = db.saveRide(r, s);
        QVERIFY(rideId > 0);

        QCOMPARE(db.rides(profileId).size(), 1);
        QCOMPARE(db.rides(profileId, QDateTime::currentDateTime().addDays(-1)).size(), 0);
        auto back = db.ride(rideId);
        QVERIFY(back);
        QCOMPARE(back->workoutName, r.workoutName);
        QCOMPARE(back->zoneSeconds[1], 2);
        const auto samples = db.rideSamples(rideId);
        QCOMPARE(samples.size(), 3);
        QCOMPARE(samples.at(2).power, 200);
        QCOMPARE(samples.at(2).heartRate, 130);

        QVERIFY(db.deleteRide(rideId));
        QCOMPARE(db.rideSamples(rideId).size(), 0);   // cascade
    }

    void playlists()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const qint64 pl = db.createPlaylist(QStringLiteral("Mix"));
        QVERIFY(pl > 0);
        const qint64 a = db.addTrack(pl, { -1, QStringLiteral("/a.mp3"), QStringLiteral("A"), QString(), 0 });
        const qint64 b = db.addTrack(pl, { -1, QStringLiteral("/b.mp3"), QStringLiteral("B"), QString(), 0 });
        QCOMPARE(db.playlistTracks(pl).size(), 2);
        QCOMPARE(db.playlists().first().trackCount, 2);
        QVERIFY(db.reorderTracks(pl, { b, a }));
        QCOMPARE(db.playlistTracks(pl).first().title, QStringLiteral("B"));
        QVERIFY(db.updateTrackInfo(a, QStringLiteral("A2"), QStringLiteral("Artist"), 1000));
        QVERIFY(db.removeTrack(b));
        QCOMPARE(db.playlistTracks(pl).first().title, QStringLiteral("A2"));
        QVERIFY(db.renamePlaylist(pl, QStringLiteral("Renamed")));
        QVERIFY(db.deletePlaylist(pl));
        QCOMPARE(db.playlists().size(), 0);
    }

    void persistsOnDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.setSetting(QStringLiteral("k"), 42);
        }
        Database db2;
        QVERIFY(db2.open(path));
        QCOMPARE(db2.setting(QStringLiteral("k")).toInt(), 42);
        QCOMPARE(db2.setting(QStringLiteral("missing"), 7).toInt(), 7);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
