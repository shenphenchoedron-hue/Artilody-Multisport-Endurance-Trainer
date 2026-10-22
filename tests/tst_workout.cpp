#include "TimeFormat.h"
#include "Workout.h"
#include "Zones.h"

#include <QJsonArray>
#include <QtTest>

using namespace watt;

class TestWorkout : public QObject
{
    Q_OBJECT
private slots:
    void intervalJsonRoundTrip()
    {
        Interval i = Interval::defaultFor(BlockType::Ramp);
        i.cadence = 92;
        i.rpeZone = 4;
        const Interval back = Interval::fromJson(i.toJson());
        QCOMPARE(back, i);
    }

    void rampInterpolation()
    {
        Interval i;
        i.durationSec = 100;
        i.startPercent = 0.5;
        i.endPercent = 1.0;
        QVERIFY(i.isRamp());
        QCOMPARE(i.wattsAt(0, 200), 100);
        QCOMPARE(i.wattsAt(50, 200), 150);
        QCOMPARE(i.wattsAt(100, 200), 200);
        QCOMPARE(i.wattsAt(500, 200), 200);   // clamped
    }

    void builtInLibraryIsValid()
    {
        const auto lib = Workout::builtInLibrary();
        QVERIFY(lib.size() >= 6);
        for (const Workout &w : lib) {
            QVERIFY2(!w.intervals.isEmpty(), qPrintable(w.name));
            QVERIFY(w.builtIn);
        }
        QCOMPARE(lib.at(0).name, QStringLiteral("Sweet Spot 3 × 12"));
        QCOMPARE(lib.at(0).totalDurationSec(), 56 * 60);
    }

    void summaryAndLabels()
    {
        const Workout ss = Workout::builtInLibrary().at(0);
        QCOMPARE(ss.intervalsLabel(), QStringLiteral("3 × 12 min"));
        QCOMPARE(ss.summary(250), QStringLiteral("3 × 12 min @ 225 W"));
    }

    void intervalLookup()
    {
        Workout w;
        w.intervals = { Interval::defaultFor(BlockType::WarmUp), Interval::defaultFor(BlockType::Recovery) };
        QCOMPARE(w.totalDurationSec(), 900);
        QCOMPARE(w.intervalIndexAt(0), 0);
        QCOMPARE(w.intervalIndexAt(599), 0);
        QCOMPARE(w.intervalIndexAt(600), 1);
        QCOMPARE(w.intervalIndexAt(900), -1);
        QCOMPARE(w.intervalStartSec(1), 600);
        QCOMPARE(w.targetWattsAt(700, 200), 110);   // recovery = 55% FTP
    }

    void segmentsSplitRamps()
    {
        Workout w;
        Interval r;
        r.durationSec = 150;
        r.startPercent = 0.5;
        r.endPercent = 1.0;
        w.intervals = { r };
        const auto seg = w.segments(200, ZoneSet(), 60);
        QCOMPARE(seg.size(), 3);
        QCOMPARE(seg.at(0).startSec, 0);
        QCOMPARE(seg.at(2).startSec, 120);
        QCOMPARE(seg.at(2).durationSec, 30);
        QVERIFY(seg.at(0).watts < seg.at(2).watts);
    }

    void zones()
    {
        ZoneSet z;
        QCOMPARE(z.zoneForFraction(0.30), 0);
        QCOMPARE(z.zoneForFraction(0.60), 1);
        QCOMPARE(z.zoneForFraction(0.80), 2);
        QCOMPARE(z.zoneForFraction(0.90), 3);
        QCOMPARE(z.zoneForFraction(1.00), 4);
        QCOMPARE(z.zoneForFraction(1.20), 5);
        QCOMPARE(z.zoneForWatts(225, 250), 3);
        QVERIFY(z.isValid());
        QCOMPARE(ZoneSet::fromJson(z.toJson()), z);

        ZoneSet bad;
        bad.upper = { 0.5, 0.4, 0.8, 0.9, 1.0 };
        QVERIFY(!bad.isValid());
        QCOMPARE(ZoneSet::fromJson(bad.toJson()), ZoneSet());   // falls back to defaults
    }

    void heartRateZones()
    {
        // HRR = 175 - 58 = 117
        QCOMPARE(heartRateZone(100, 175, 58), 0);   // 36%
        QCOMPARE(heartRateZone(125, 175, 58), 1);   // 57%
        QCOMPARE(heartRateZone(142, 175, 58), 3);   // 72%
        QCOMPARE(heartRateZone(175, 175, 58), 5);
        QCOMPARE(heartRateZoneLow(3, 175, 58), 140);
        QCOMPARE(heartRateZoneHigh(3, 175, 58), 152);
        QCOMPARE(heartRateZone(0, 175, 58), 0);
    }

    void timeFormat()
    {
        QCOMPARE(formatClock(0), QStringLiteral("0:00"));
        QCOMPARE(formatClock(24 * 60 + 18), QStringLiteral("24:18"));
        QCOMPARE(formatClock(3723), QStringLiteral("1:02:03"));
        QCOMPARE(formatHoursMinutes(21 * 3600 + 35 * 60), QStringLiteral("21h 35m"));
        QCOMPARE(formatHoursMinutes(20 * 60, true), QStringLiteral("0h 20m"));
        QCOMPARE(parseClock(QStringLiteral("12:00")), 720);
        QCOMPARE(parseClock(QStringLiteral("1:00:00")), 3600);
        QCOMPARE(parseClock(QStringLiteral("abc")), -1);
    }
};

QTEST_GUILESS_MAIN(TestWorkout)
#include "tst_workout.moc"
