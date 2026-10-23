#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutIO.h"
#include "WorkoutLibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;
namespace io = watt::workoutio;

namespace {

QByteArray awtWith(const QJsonArray &intervals, const QString &name = QStringLiteral("Test"),
                   const QString &category = QStringLiteral("Intervals"))
{
    QJsonObject w{ { QStringLiteral("name"), name }, { QStringLiteral("category"), category },
                   { QStringLiteral("intervals"), intervals } };
    QJsonObject root{ { QStringLiteral("format"), QString::fromLatin1(io::kFormatId) },
                      { QStringLiteral("version"), 1 },
                      { QStringLiteral("workouts"), QJsonArray{ w } } };
    return QJsonDocument(root).toJson();
}

QJsonObject jsonInterval(double duration, double start, double end, int cadence = 0)
{
    QJsonObject o{ { QStringLiteral("name"), QStringLiteral("Block") }, { QStringLiteral("type"), QStringLiteral("steady") },
                   { QStringLiteral("duration"), duration }, { QStringLiteral("start"), start }, { QStringLiteral("end"), end } };
    if (cadence)
        o.insert(QStringLiteral("cadence"), cadence);
    return o;
}

bool anyContains(const QStringList &list, const QString &needle)
{
    for (const QString &s : list) {
        if (s.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

} // namespace

class TestWorkoutIO : public QObject
{
    Q_OBJECT
private slots:
    void awtRoundTripsBuiltIns()
    {
        const QVector<Workout> lib = Workout::builtInLibrary();
        const QByteArray data = io::toAwt(lib, QStringLiteral("1.1.0"));
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        QCOMPARE(root.value(QStringLiteral("format")).toString(), QString::fromLatin1(io::kFormatId));
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("app")).toString(), QStringLiteral("Artilody Watts Trainer 1.1.0"));
        QVERIFY(root.value(QStringLiteral("exportedAt")).toString().endsWith(QLatin1Char('Z')));

        const io::ImportResult r = io::parseAwt(data);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
        QCOMPARE(r.workouts.size(), lib.size());
        for (int i = 0; i < lib.size(); ++i) {
            const Workout &a = lib.at(i);
            const Workout &b = r.workouts.at(i);
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.category, a.category);
            QCOMPARE(b.focus, a.focus);
            QCOMPARE(b.description, a.description);
            QVERIFY(b.intervals == a.intervals);
            QVERIFY(!b.builtIn);
            QVERIFY(!b.favorite);
            QCOMPARE(b.timesUsed, 0);
            QCOMPARE(b.id, qint64(-1));
        }
    }

    void zwoRoundTripsBuiltIns()
    {
        for (const Workout &a : Workout::builtInLibrary()) {
            const QByteArray xml = io::toZwo(a);
            QVERIFY(xml.contains("<sportType>bike</sportType>"));
            const io::ImportResult r = io::parseZwo(xml);
            QVERIFY2(r.ok(), qPrintable(r.error));
            QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
            const Workout &b = r.workouts.first();
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.description, a.description);
            QCOMPARE(b.category, a.category);
            QCOMPARE(b.intervals.size(), a.intervals.size());
            for (int i = 0; i < a.intervals.size(); ++i) {
                const Interval &x = a.intervals.at(i);
                const Interval &y = b.intervals.at(i);
                QCOMPARE(y.durationSec, x.durationSec);
                QVERIFY(std::abs(y.startPercent - x.startPercent) < 0.001);
                QVERIFY(std::abs(y.endPercent - x.endPercent) < 0.001);
                QCOMPARE(y.isRamp(), x.isRamp());
                QCOMPARE(y.type, x.type);
                QCOMPARE(y.cadence, x.cadence);
                QCOMPARE(y.name, x.name);
            }
        }
    }

    void zwoSampleIsParsed()
    {
        const QByteArray zwo = R"(<?xml version="1.0" encoding="UTF-8"?>
<Workout_File>
  <Author>Someone</Author>
  <NAME>  Club Session </NAME>
  <description>Hard &amp; fun</description>
  <SportType>bike</SportType>
  <tags><tag name="Hard"/><tag name="endurance"/></tags>
  <Workout>
    <WarmUp duration="600" powerlow="0.25" POWERHIGH="0.75"/>
    <IntervalsT Repeat="3" OnDuration="60" OffDuration="30" OnPower="1.2" OffPower="0.5" Cadence="100" CadenceResting="85">
      <textevent timeoffset="10" message="Go hard!"/>
    </IntervalsT>
    <SteadyState Duration="300.4" Power="0.65" Cadence="90"><textevent timeoffset="0" message="Tempo"/></SteadyState>
    <FreeRide Duration="120" FlatRoad="1"/>
    <SomethingNew Duration="60" Power="3"/>
    <textevent timeoffset="0" message="ignored"/>
    <MaxEffort Duration="20"/>
    <Ramp Duration="240" PowerLow="0.6" PowerHigh="1.0"/>
    <Cooldown Duration="300" PowerLow="0.7" PowerHigh="0.3"/>
  </Workout>
</Workout_File>)";
        const io::ImportResult r = io::parseZwo(zwo);
        QVERIFY2(r.ok(), qPrintable(r.error));
        const Workout &w = r.workouts.first();
        QCOMPARE(w.name, QStringLiteral("Club Session"));
        QCOMPARE(w.description, QStringLiteral("Hard & fun"));
        QCOMPARE(w.category, QStringLiteral("Endurance"));
        QCOMPARE(w.intervals.size(), 1 + 6 + 1 + 1 + 1 + 1 + 1);

        const Interval &wu = w.intervals.at(0);
        QCOMPARE(wu.type, BlockType::WarmUp);
        QCOMPARE(wu.durationSec, 600);
        QCOMPARE(wu.startPercent, 0.25);
        QCOMPARE(wu.endPercent, 0.75);

        for (int n = 0; n < 3; ++n) {
            const Interval &on = w.intervals.at(1 + 2 * n);
            const Interval &off = w.intervals.at(2 + 2 * n);
            QCOMPARE(on.type, BlockType::Interval);
            QCOMPARE(on.durationSec, 60);
            QCOMPARE(on.startPercent, 1.2);
            QCOMPARE(on.cadence, 100);
            QCOMPARE(off.type, BlockType::Recovery);
            QCOMPARE(off.durationSec, 30);
            QCOMPARE(off.startPercent, 0.5);
            QCOMPARE(off.cadence, 85);
        }
        QCOMPARE(w.intervals.at(1).name, QStringLiteral("Interval"));   // offset 10 s is not a name

        const Interval &steady = w.intervals.at(7);
        QCOMPARE(steady.type, BlockType::Steady);
        QCOMPARE(steady.durationSec, 300);
        QCOMPARE(steady.startPercent, 0.65);
        QCOMPARE(steady.cadence, 90);
        QCOMPARE(steady.name, QStringLiteral("Tempo"));

        const Interval &free = w.intervals.at(8);
        QCOMPARE(free.name, QStringLiteral("Free Ride"));
        QCOMPARE(free.durationSec, 120);
        QCOMPARE(free.startPercent, 0.6);

        const Interval &max = w.intervals.at(9);
        QCOMPARE(max.name, QStringLiteral("Max Effort"));
        QCOMPARE(max.startPercent, 1.5);
        QCOMPARE(max.durationSec, 20);

        const Interval &ramp = w.intervals.at(10);
        QCOMPARE(ramp.type, BlockType::Ramp);
        QCOMPARE(ramp.startPercent, 0.6);
        QCOMPARE(ramp.endPercent, 1.0);

        const Interval &cd = w.intervals.at(11);
        QCOMPARE(cd.type, BlockType::CoolDown);
        QCOMPARE(cd.startPercent, 0.7);   // Zwift: PowerLow is the start
        QCOMPARE(cd.endPercent, 0.3);

        QVERIFY(anyContains(r.warnings, QStringLiteral("Free ride")));
        QVERIFY(anyContains(r.warnings, QStringLiteral("Max effort")));
        QVERIFY(anyContains(r.warnings, QStringLiteral("SomethingNew")));
    }

    void zwoWithoutCategoryGuessesOne()
    {
        const io::ImportResult r = io::parseZwo(
            "<workout_file><workout><SteadyState Duration='1800' Power='0.5'/></workout></workout_file>");
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.first().name, QStringLiteral("Imported workout"));
        QCOMPARE(r.workouts.first().category, QStringLiteral("Recovery"));
        QVERIFY(r.warnings.isEmpty());
    }

    void percentValuesAreNormalised()
    {
        io::ImportResult r = io::parseAwt(awtWith({ jsonInterval(600, 75, 95) }));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.first().intervals.first().startPercent, 0.75);
        QCOMPARE(r.workouts.first().intervals.first().endPercent, 0.95);
        QVERIFY(anyContains(r.warnings, QStringLiteral("percent")));

        r = io::parseZwo("<workout_file><workout><SteadyState Duration='600' Power='90'/></workout></workout_file>");
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.first().intervals.first().startPercent, 0.9);
        QVERIFY(anyContains(r.warnings, QStringLiteral("percent")));
    }

    void valuesAreClamped()
    {
        QJsonObject withRpe = jsonInterval(100000, 0.7, 0.7, 500);
        withRpe.insert(QStringLiteral("rpe"), 9);
        const io::ImportResult r = io::parseAwt(awtWith({ jsonInterval(1, 0.7, 0.7), withRpe, jsonInterval(600, 1000, 1000) },
                                                        QStringLiteral("Clamp"), QStringLiteral("Bogus")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        const Workout &w = r.workouts.first();
        QCOMPARE(w.intervals.at(0).durationSec, io::kMinIntervalSec);
        QCOMPARE(w.intervals.at(1).durationSec, io::kMaxIntervalSec);
        QCOMPARE(w.intervals.at(1).cadence, io::kMaxCadence);
        QCOMPARE(w.intervals.at(1).rpeZone, -1);
        QCOMPARE(w.intervals.at(2).startPercent, io::kMaxFraction);
        QVERIFY(io::allowedCategories().contains(w.category));
        QVERIFY(anyContains(r.warnings, QStringLiteral("Bogus")));
        QVERIFY(r.warnings.size() >= 5);
    }

    void bareObjectAndArrayAreAccepted()
    {
        const QJsonObject w{ { QStringLiteral("name"), QStringLiteral("Bare") },
                             { QStringLiteral("intervals"), QJsonArray{ jsonInterval(300, 0.6, 0.6) } } };
        io::ImportResult r = io::parseAwt(QJsonDocument(w).toJson());
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.first().name, QStringLiteral("Bare"));

        r = io::parseAwt(QJsonDocument(QJsonArray{ w, w }).toJson());
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.size(), 2);
    }

    void malformedFilesAreRejected()
    {
        QVERIFY(!io::parseAwt("{ \"workouts\": [ ").error.isEmpty());
        QVERIFY(!io::parseZwo("<workout_file><workout><SteadyState Duration='60'></workout>").error.isEmpty());
        QVERIFY(io::parseZwo("<html><body/></html>").error.contains(QStringLiteral("<workout>")));
        QVERIFY(!io::parseData("hello world").error.isEmpty());
        QVERIFY(!io::parseData("").error.isEmpty());
        QVERIFY(!io::parseAwt("{\"format\":\"something-else\",\"workouts\":[]}").error.isEmpty());
        QVERIFY(!io::parseAwt("{\"foo\": 1}").error.isEmpty());
    }

    void newerVersionIsRejected()
    {
        const QByteArray data = R"({"format":"artilody-watts-trainer/workout","version":2,"workouts":[]})";
        const io::ImportResult r = io::parseAwt(data);
        QVERIFY(r.workouts.isEmpty());
        QVERIFY(r.error.contains(QStringLiteral("newer version")));
    }

    void emptyWorkoutsAreSkipped()
    {
        io::ImportResult r = io::parseAwt(awtWith({}));
        QVERIFY(!r.ok());
        QVERIFY(!r.error.isEmpty());

        const QJsonObject empty{ { QStringLiteral("name"), QStringLiteral("Empty") }, { QStringLiteral("intervals"), QJsonArray() } };
        const QJsonObject good{ { QStringLiteral("name"), QStringLiteral("Good") },
                                { QStringLiteral("intervals"), QJsonArray{ jsonInterval(300, 0.6, 0.6) } } };
        r = io::parseAwt(QJsonDocument(QJsonArray{ empty, good }).toJson());
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.size(), 1);
        QVERIFY(anyContains(r.warnings, QStringLiteral("Empty")));

        QVERIFY(!io::parseZwo("<workout_file><name>x</name><workout/></workout_file>").ok());
    }

    void limitsAreEnforced()
    {
        // file size
        QByteArray huge(int(io::kMaxFileBytes) + 10, ' ');
        huge[0] = '{';
        QVERIFY(io::parseData(huge).error.contains(QStringLiteral("too large")));

        // number of workouts
        const QJsonObject w{ { QStringLiteral("name"), QStringLiteral("W") },
                             { QStringLiteral("intervals"), QJsonArray{ jsonInterval(60, 0.6, 0.6) } } };
        QJsonArray many;
        for (int i = 0; i <= io::kMaxWorkouts; ++i)
            many.append(w);
        QVERIFY(!io::parseAwt(QJsonDocument(many).toJson(QJsonDocument::Compact)).error.isEmpty());

        // intervals per workout
        QJsonArray intervals;
        for (int i = 0; i <= io::kMaxIntervals; ++i)
            intervals.append(jsonInterval(5, 0.6, 0.6));
        io::ImportResult r = io::parseAwt(awtWith(intervals));
        QVERIFY(r.workouts.isEmpty());
        QVERIFY(r.error.contains(QStringLiteral("intervals")));

        // workout longer than 12 h
        QJsonArray longIntervals;
        for (int i = 0; i < 3; ++i)
            longIntervals.append(jsonInterval(5 * 3600, 0.6, 0.6));
        QVERIFY(!io::parseAwt(awtWith(longIntervals)).ok());

        // IntervalsT repeat is capped
        r = io::parseZwo("<workout_file><workout><IntervalsT Repeat='100000' OnDuration='30' OffDuration='30' "
                         "OnPower='1.1' OffPower='0.5'/></workout></workout_file>");
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.workouts.first().intervals.size(), 200);
        QVERIFY(anyContains(r.warnings, QStringLiteral("repeat")));
    }

    void detectsFormatByContent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Workout w = Workout::builtInLibrary().first();
        // JSON content in a .zwo-named file and XML in a .awt file are still recognised
        QVERIFY(writeBytes(dir.filePath(QStringLiteral("a.zwo")), io::toAwt({ w }, QString())));
        QVERIFY(writeBytes(dir.filePath(QStringLiteral("b.awt")), "\xEF\xBB\xBF" + io::toZwo(w)));
        QVERIFY(io::parseFile(dir.filePath(QStringLiteral("a.zwo"))).ok());
        QVERIFY(io::parseFile(dir.filePath(QStringLiteral("b.awt"))).ok());
        QVERIFY(!io::parseFile(dir.filePath(QStringLiteral("missing.awt"))).error.isEmpty());
    }

    void safeFileNames()
    {
        QCOMPARE(io::safeFileName(QStringLiteral("VO₂ Max 5 × 3")), QStringLiteral("VO₂ Max 5 × 3"));
        QCOMPARE(io::safeFileName(QStringLiteral("a/b:c*?\"<>|d")), QStringLiteral("a b c d"));
        QCOMPARE(io::safeFileName(QStringLiteral("..")), QStringLiteral("Workout"));
        QCOMPARE(io::formatForFileName(QStringLiteral("x.ZWO")), io::Format::Zwo);
        QCOMPARE(io::formatForFileName(QStringLiteral("x.awt")), io::Format::Awt);
    }

    // --- WorkoutLibrary integration --------------------------------------------
    void libraryExportImport()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        WorkoutLibrary lib(&db, &profiles);
        lib.reload();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // nothing custom yet
        const QString allPath = dir.filePath(QStringLiteral("all.awt"));
        QCOMPARE(lib.exportAllCustom(QUrl::fromLocalFile(allPath)), 0);
        QVERIFY(!QFile::exists(allPath));
        QVERIFY(lib.lastMessage().contains(QStringLiteral("no custom workouts")));

        // a custom workout
        Workout custom = Workout::builtInLibrary().first();
        custom.builtIn = false;
        custom.name = QStringLiteral("My Sweet Spot");
        custom.intervals[1].durationSec = 900;
        const qint64 customId = db.saveWorkout(custom);
        lib.reload();
        QCOMPARE(lib.customCount(), 1);

        const QString path = dir.filePath(QStringLiteral("mine.awt"));
        QVERIFY(lib.exportWorkout(customId, QUrl::fromLocalFile(path)));
        QVERIFY(QFile::exists(path));
        QCOMPARE(lib.exportAllCustom(QUrl::fromLocalFile(allPath)), 1);

        // identical workout -> duplicate skipped
        const int before = db.workouts().size();
        QCOMPARE(lib.importFile(QUrl::fromLocalFile(path)), 0);
        QVERIFY(lib.lastMessage().contains(QStringLiteral("already")));
        QCOMPARE(db.workouts().size(), before);

        // same name, different intervals -> " (imported)"
        custom.id = customId;
        custom.intervals[1].durationSec = 600;
        db.saveWorkout(custom);
        lib.reload();
        QCOMPARE(lib.importFile(QUrl::fromLocalFile(path)), 1);
        QVERIFY2(lib.lastMessage().startsWith(QStringLiteral("Imported 1 workout")), qPrintable(lib.lastMessage()));
        const auto imported = lib.workout(lib.selectedId());
        QVERIFY(imported);
        QCOMPARE(imported->name, QStringLiteral("My Sweet Spot (imported)"));
        QVERIFY(!imported->builtIn);
        QCOMPARE(imported->intervals[1].durationSec, 900);

        // importing the same file again is recognised as duplicate of the "(imported)" copy
        QCOMPARE(lib.importFile(QUrl::fromLocalFile(path)), 0);

        // a further variant gets "(imported 2)"
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();
        f.close();
        data.replace("\"duration\": 900", "\"duration\": 1200");
        const QString variant = dir.filePath(QStringLiteral("variant.awt"));
        QVERIFY(writeBytes(variant, data));
        QCOMPARE(lib.importFiles({ QUrl::fromLocalFile(variant) }), 1);
        QCOMPARE(lib.workout(lib.selectedId())->name, QStringLiteral("My Sweet Spot (imported 2)"));

        // built-in workouts can be exported as Zwift; the extension follows the hint
        qint64 builtInId = -1;
        for (const Workout &w : db.workouts()) {
            if (w.builtIn && w.name == QStringLiteral("Sweet Spot 3 × 12"))
                builtInId = w.id;
        }
        QVERIFY(builtInId > 0);
        const QString zwoBase = dir.filePath(QStringLiteral("sweetspot"));
        QVERIFY(lib.exportWorkout(builtInId, QUrl::fromLocalFile(zwoBase), QStringLiteral("zwo")));
        QVERIFY(QFile::exists(zwoBase + QStringLiteral(".zwo")));
        // re-importing an unchanged built-in workout is a duplicate
        QCOMPARE(lib.importFile(QUrl::fromLocalFile(zwoBase + QStringLiteral(".zwo"))), 0);

        // bulk export of chosen ids, then import into a fresh library
        const QString bulk = dir.filePath(QStringLiteral("bulk.awt"));
        QCOMPARE(lib.exportWorkouts({ customId, builtInId }, QUrl::fromLocalFile(bulk)), 2);

        Database db2;
        QVERIFY(db2.open(QStringLiteral(":memory:")));
        ProfileManager profiles2(&db2);
        profiles2.load();
        WorkoutLibrary lib2(&db2, &profiles2);
        lib2.setCategory(QStringLiteral("Recovery"));
        lib2.reload();
        QCOMPARE(lib2.importFiles({ QUrl::fromLocalFile(bulk), QUrl::fromLocalFile(dir.filePath(QStringLiteral("nope.zwo"))) }), 2);
        QVERIFY(lib2.lastMessage().contains(QStringLiteral("nope.zwo")));
        QCOMPARE(lib2.category(), QStringLiteral("All"));   // filter reset so the import is visible
        for (const Workout &w : db2.workouts())
            QVERIFY(!w.builtIn);

        // failure message
        QCOMPARE(lib.importFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("nope.awt")))), 0);
        QVERIFY(lib.lastMessage().startsWith(QStringLiteral("Import failed")));

        // suggested names
        QVERIFY(lib.suggestedExportUrl(builtInId, QStringLiteral("zwo")).toLocalFile().endsWith(QStringLiteral("Sweet Spot 3 × 12.zwo")));
        QVERIFY(lib.suggestedExportUrl(-1, QStringLiteral("all")).toLocalFile().endsWith(QStringLiteral("My workouts.awt")));
    }
};

QTEST_GUILESS_MAIN(TestWorkoutIO)
#include "tst_workoutio.moc"
