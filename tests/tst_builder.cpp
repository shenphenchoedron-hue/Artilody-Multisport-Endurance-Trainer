#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutBuilder.h"
#include "WorkoutLibrary.h"

#include <QtTest>

using namespace watt;

class TestBuilder : public QObject
{
    Q_OBJECT
    Database *db = nullptr;
    ProfileManager *profiles = nullptr;

private slots:
    void init()
    {
        db = new Database;
        QVERIFY(db->open(QStringLiteral(":memory:")));
        db->seedDefaults();
        profiles = new ProfileManager(db);
        profiles->load();
    }
    void cleanup()
    {
        delete profiles;
        delete db;
    }

    void editAndSave()
    {
        WorkoutBuilder b(db, profiles);
        QCOMPARE(b.blockCount(), 3);
        b.setName(QStringLiteral("My Workout"));
        b.setWatts(1, 200);
        QCOMPARE(b.selected().value(QStringLiteral("watts")).toInt(), 200);
        QCOMPARE(b.selected().value(QStringLiteral("percent")).toInt(), 80);   // 200 / 250
        b.setPercent(1, 100);
        QCOMPARE(b.intervals()->data(b.intervals()->index(1), IntervalModel::WattsRole).toInt(), 250);
        QVERIFY(b.setDurationText(1, QStringLiteral("8:30")));
        QCOMPARE(b.intervals()->intervals().at(1).durationSec, 510);
        QVERIFY(b.setDurationText(1, QStringLiteral("10")));      // minutes
        QCOMPARE(b.intervals()->intervals().at(1).durationSec, 600);
        b.setRpe(1, 5);
        b.setCadence(1, 95);
        QVERIFY(b.dirty());

        const qint64 id = b.save();
        QVERIFY(id > 0);
        QVERIFY(!b.dirty());
        auto w = db->workout(id);
        QVERIFY(w);
        QCOMPARE(w->name, QStringLiteral("My Workout"));
        QCOMPARE(w->intervals.at(1).rpeZone, 5);
        QCOMPARE(w->intervals.at(1).cadence, 95);
    }

    void blocksAddMoveRemove()
    {
        WorkoutBuilder b(db, profiles);
        b.setSelectedIndex(1);
        b.addBlock(QStringLiteral("intervals"));             // 3 × (work + rest)
        QCOMPARE(b.blockCount(), 9);
        b.addBlock(QStringLiteral("warmup"));                // always first
        QCOMPARE(b.intervals()->intervals().at(0).type, BlockType::WarmUp);
        b.duplicateBlock(0);
        QCOMPARE(b.blockCount(), 11);
        b.moveBlock(0, 3);
        QCOMPARE(b.selectedIndex(), 0);                      // the duplicate shifted left
        b.removeBlock(0);
        QCOMPARE(b.blockCount(), 10);
    }

    void selectionFollowsBlock()
    {
        WorkoutBuilder b(db, profiles);
        b.addBlock(QStringLiteral("steady"));        // 4 blocks, selection = 2 (the new one)
        b.setSelectedIndex(2);
        const QString name = b.selected().value(QStringLiteral("name")).toString();
        b.removeBlock(0);
        QCOMPARE(b.selectedIndex(), 1);
        QCOMPARE(b.selected().value(QStringLiteral("name")).toString(), name);
        b.moveBlock(0, 2);                           // moves a block across the selection
        QCOMPARE(b.selected().value(QStringLiteral("name")).toString(), name);
        b.moveBlock(2, 0);
        QCOMPARE(b.selected().value(QStringLiteral("name")).toString(), name);
    }

    void saveKeepsFavoriteAndUsage()
    {
        WorkoutBuilder b(db, profiles);
        b.setName(QStringLiteral("Mine"));
        const qint64 id = b.save();
        QVERIFY(b.loadWorkout(id));
        db->setWorkoutFavorite(id, true);           // changed in the library meanwhile
        db->incrementWorkoutUsage(id);
        b.setWatts(0, 120);
        QCOMPARE(b.save(), id);
        QVERIFY(db->workout(id)->favorite);
        QCOMPARE(db->workout(id)->timesUsed, 1);
    }

    void editingBuiltInCreatesCopy()
    {
        WorkoutBuilder b(db, profiles);
        const Workout original = db->workouts().first();
        QVERIFY(b.loadWorkout(original.id));
        b.setWatts(0, 50);
        const qint64 id = b.save();
        QVERIFY(id != original.id);
        QCOMPARE(db->workout(original.id)->intervals, original.intervals);   // untouched
        QVERIFY(!db->workout(id)->builtIn);
    }

    void libraryFilterAndSort()
    {
        WorkoutLibrary lib(db, profiles);
        lib.reload();
        const int all = lib.rowCount();
        QVERIFY(all >= 6);
        lib.setCategory(QStringLiteral("Recovery"));
        QCOMPARE(lib.rowCount(), 1);
        lib.setCategory(QStringLiteral("All"));
        lib.setSearchText(QStringLiteral("threshold"));
        QCOMPARE(lib.rowCount(), 1);
        lib.setSearchText(QString());
        lib.setSortMode(WorkoutLibrary::Shortest);
        QVERIFY(lib.data(lib.index(0), WorkoutLibrary::TotalSecRole).toInt()
                <= lib.data(lib.index(all - 1), WorkoutLibrary::TotalSecRole).toInt());
        const qint64 id = lib.data(lib.index(0), WorkoutLibrary::IdRole).toLongLong();
        lib.toggleFavorite(id);
        QVERIFY(db->workout(id)->favorite);
        const qint64 copy = lib.duplicate(id);
        QVERIFY(copy > 0);
        QCOMPARE(lib.rowCount(), all + 1);
        QVERIFY(lib.remove(copy));
        QCOMPARE(lib.rowCount(), all);
    }

    void builtInWorkoutsCannotBeRemoved()
    {
        WorkoutLibrary lib(db, profiles);
        lib.reload();
        const int all = lib.rowCount();
        qint64 rampId = -1, otherBuiltIn = -1;
        for (const Workout &w : db->workouts()) {
            if (w.isProtected())
                rampId = w.id;
            else if (w.builtIn)
                otherBuiltIn = w.id;
        }
        QVERIFY(rampId > 0);
        QVERIFY(otherBuiltIn > 0);
        const QVariantMap ramp = lib.get(rampId);
        QVERIFY(ramp.value(QStringLiteral("isFtpTest")).toBool());
        QVERIFY(ramp.value(QStringLiteral("requiredForFtp")).toBool());
        QVERIFY(!ramp.value(QStringLiteral("deletable")).toBool());

        QVERIFY(!lib.remove(rampId));
        QVERIFY(db->workout(rampId));
        QVERIFY(!lib.remove(otherBuiltIn));
        QVERIFY(db->workout(otherBuiltIn));
        QCOMPARE(lib.rowCount(), all);
        QVERIFY(!lib.remove(987654));   // unknown id

        // an edited copy of the ramp test is still an FTP test, but deletable
        WorkoutBuilder b(db, profiles);
        QVERIFY(b.loadWorkout(rampId));
        b.setWatts(0, 100);
        const qint64 copyId = b.save();
        QVERIFY(copyId != rampId);
        lib.reload();
        const QVariantMap copy = lib.get(copyId);
        QVERIFY(copy.value(QStringLiteral("isFtpTest")).toBool());
        QVERIFY(copy.value(QStringLiteral("deletable")).toBool());
        QVERIFY(!copy.value(QStringLiteral("requiredForFtp")).toBool());
        QVERIFY(lib.remove(copyId));
        QVERIFY(db->workout(rampId));
    }

    void ftpComparison()
    {
        QVERIFY(profiles->saveProfile({ { QStringLiteral("ftp"), 250 }, { QStringLiteral("weight"), 68 } }));
        QVariantMap m = profiles->ftpComparison(262, 250);
        QCOMPARE(m.value(QStringLiteral("diff")).toInt(), 12);
        QCOMPARE(m.value(QStringLiteral("diffText")).toString(), QStringLiteral("+12 W"));
        QCOMPARE(m.value(QStringLiteral("percentText")).toString(), QStringLiteral("+4.8%"));
        QCOMPARE(m.value(QStringLiteral("wattsPerKgText")).toString(), QStringLiteral("3.85 W/kg"));
        m = profiles->ftpComparison(240, 250);
        QCOMPARE(m.value(QStringLiteral("diffText")).toString(), QString(QChar(0x2212)) + QStringLiteral("10 W"));
        QCOMPARE(m.value(QStringLiteral("percentText")).toString(), QString(QChar(0x2212)) + QStringLiteral("4.0%"));
        m = profiles->ftpComparison(250, 250);
        QCOMPARE(m.value(QStringLiteral("diffText")).toString(), QStringLiteral("±0 W"));
    }

    void profileValidation()
    {
        QVERIFY(!profiles->saveProfile({ { QStringLiteral("ftp"), 10 } }));
        QVERIFY(!profiles->lastError().isEmpty());
        QVERIFY(profiles->saveProfile({ { QStringLiteral("ftp"), 280 }, { QStringLiteral("weight"), 70 } }));
        QCOMPARE(profiles->ftp(), 280);
        QCOMPARE(profiles->powerZones().size(), 6);
        QCOMPARE(profiles->heartRateZonesFor(175, 58).size(), 5);
        QCOMPARE(profiles->zoneBounds().size(), 5);
        // invalid values must not leave a half-created profile behind
        QCOMPARE(profiles->createProfileWith({ { QStringLiteral("name"), QStringLiteral("Bad") },
                                               { QStringLiteral("maxHr"), 70 } }), qint64(-1));
        QCOMPARE(profiles->profiles().size(), 1);
        const qint64 second = profiles->createProfileWith({ { QStringLiteral("name"), QStringLiteral("Anna") },
                                                            { QStringLiteral("ftp"), 200 } });
        QVERIFY(second > 0);
        QCOMPARE(profiles->name(), QStringLiteral("Anna"));
        QVERIFY(profiles->deleteProfile(second));
        QCOMPARE(profiles->profiles().size(), 1);
        QVERIFY(!profiles->deleteProfile(profiles->profileId()));   // keep at least one
    }
};

QTEST_GUILESS_MAIN(TestBuilder)
#include "tst_builder.moc"
