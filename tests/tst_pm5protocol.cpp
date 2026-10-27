#include "Pm5Protocol.h"

#include <QtTest>

using namespace watt;

namespace {
// Little-endian packet builder.
struct Packet
{
    QByteArray b;
    Packet &u8(int v) { b.append(char(quint8(v))); return *this; }
    Packet &u16(int v) { return u8(v & 0xFF).u8((v >> 8) & 0xFF); }
    Packet &u24(int v) { return u8(v & 0xFF).u8((v >> 8) & 0xFF).u8((v >> 16) & 0xFF); }
    operator QByteArray() const { return b; }
};

QByteArray generalStatus()
{
    return Packet()
        .u24(12345)      // 123.45 s
        .u24(5000)       // 500.0 m
        .u8(1)           // workout type
        .u8(255)         // interval type (none)
        .u8(1)           // workout state: workout row
        .u8(1)           // rowing state: active
        .u8(2)           // stroke state: driving
        .u24(2000)       // total work distance
        .u24(0x030201)   // workout duration raw
        .u8(0x80)        // duration type
        .u8(120);        // drag factor
}

QByteArray status1(bool mux, int hr = 142)
{
    Packet p;
    p.u24(6000)          // 60.00 s
        .u16(4250)       // 4.250 m/s
        .u8(24)          // spm
        .u8(hr)
        .u16(11765)      // 1:57.65 current pace
        .u16(12010)      // 2:00.10 average pace
        .u16(300)        // rest distance
        .u24(4500);      // 45.00 s rest time
    if (mux)
        p.u16(187);      // average power (multiplexed only)
    p.u8(0);             // erg machine type: static D
    return p;
}

QByteArray status2(bool mux)
{
    Packet p;
    p.u24(9000).u8(3);   // 90 s, interval 3
    if (!mux)
        p.u16(175);      // average power (direct only)
    p.u16(42)            // total calories
        .u16(12500)      // split avg pace 2:05.00
        .u16(180)        // split avg power
        .u16(950)        // split avg cal/hr
        .u24(1234)       // last split 123.4 s
        .u24(500);       // last split distance
    return p;
}

QByteArray stroke(bool mux)
{
    Packet p;
    p.u24(7000)          // 70 s
        .u16(212)        // stroke power
        .u16(1100)       // cal/hr
        .u16(57)         // stroke count
        .u24(1800)       // projected time
        .u24(6500);      // projected distance
    if (mux)
        p.u16(4567);     // work per stroke 456.7 J
    return p;
}
} // namespace

class TestPm5Protocol : public QObject
{
    Q_OBJECT
private slots:
    void uuids()
    {
        QCOMPARE(pm5::uuid(pm5::kRowingService), QUuid(QStringLiteral("{ce060030-43e5-11e4-916c-0800200c9a66}")));
        QCOMPARE(pm5::uuid(pm5::kMultiplexed), QUuid(QStringLiteral("{CE060080-43E5-11E4-916C-0800200C9A66}")));
        QCOMPARE(*pm5::shortId(pm5::uuid(pm5::kAdditionalStrokeData)), quint16(0x0036));
        QVERIFY(!pm5::shortId(QUuid(QStringLiteral("{00001826-0000-1000-8000-00805f9b34fb}"))));
        QVERIFY(!pm5::shortId(QUuid(QStringLiteral("{ce060030-43e5-11e4-916c-0800200c9a67}"))));
    }

    void sampleRate()
    {
        QCOMPARE(pm5::sampleRateCommand(pm5::SampleRate::Ms250), QByteArray(1, '\x02'));
        QCOMPARE(pm5::sampleRateCommand(pm5::SampleRate::Ms1000), QByteArray(1, '\x00'));
        QCOMPARE(pm5::sampleRateCommand(pm5::SampleRate::Ms100), QByteArray(1, '\x03'));
        QCOMPARE(pm5::sampleRateForInterval(250), pm5::SampleRate::Ms250);
        QCOMPARE(pm5::sampleRateForInterval(50), pm5::SampleRate::Ms100);
        QCOMPARE(pm5::sampleRateForInterval(400), pm5::SampleRate::Ms500);
        QCOMPARE(pm5::sampleRateForInterval(5000), pm5::SampleRate::Ms1000);
    }

    void generalStatusParses()
    {
        const auto s = pm5::parseGeneralStatus(generalStatus());
        QVERIFY(s);
        QCOMPARE(s->elapsedSeconds, 123.45);
        QCOMPARE(s->distanceMeters, 500.0);
        QCOMPARE(s->workoutType, 1);
        QCOMPARE(s->intervalType, 255);
        QCOMPARE(s->workoutState, int(pm5::WorkoutState::WorkoutRow));
        QCOMPARE(s->rowingState, 1);
        QVERIFY(s->rowingActive());
        QCOMPARE(s->strokeState, 2);
        QCOMPARE(s->totalWorkDistance, 2000);
        QCOMPARE(s->workoutDuration, 0x030201);
        QCOMPARE(s->durationType, 0x80);
        QCOMPARE(s->dragFactor, 120);
    }

    void generalStatusLargeValuesUnsigned()
    {
        QByteArray d = generalStatus();
        d[0] = char(0xFF); d[1] = char(0xFF); d[2] = char(0xFF);   // max UInt24
        const auto s = pm5::parseGeneralStatus(d);
        QVERIFY(s);
        QCOMPARE(s->elapsedSeconds, 0xFFFFFF * 0.01);
    }

    void generalStatusTooShort()
    {
        QVERIFY(!pm5::parseGeneralStatus(generalStatus().left(18)));
        QVERIFY(!pm5::parseGeneralStatus(QByteArray()));
    }

    void additionalStatus1Direct()
    {
        const auto s = pm5::parseAdditionalStatus1(status1(false));
        QVERIFY(s);
        QCOMPARE(s->elapsedSeconds, 60.0);
        QCOMPARE(s->speedMps, 4.25);
        QCOMPARE(s->strokesPerMinute, 24);
        QCOMPARE(s->heartRateBpm, 142);
        QVERIFY(s->heartRateValid);
        QCOMPARE(s->currentPace500Seconds, 117.65);
        QCOMPARE(s->averagePace500Seconds, 120.10);
        QCOMPARE(s->restDistanceMeters, 300);
        QCOMPARE(s->restTimeSeconds, 45.0);
        QCOMPARE(s->ergMachineType, 0);
        QVERIFY(!s->averagePowerW);
    }

    void additionalStatus1InvalidHeartRate()
    {
        const auto s = pm5::parseAdditionalStatus1(status1(false, 255));
        QVERIFY(s);
        QCOMPARE(s->heartRateBpm, 0);
        QVERIFY(!s->heartRateValid);
    }

    void additionalStatus1TooShort()
    {
        QVERIFY(!pm5::parseAdditionalStatus1(status1(false).left(16)));
        QVERIFY(!pm5::parseAdditionalStatus1(status1(true).left(16), pm5::Layout::Multiplexed));
    }

    void additionalStatus1ShortMultiplexedKeepsStrokeRate()
    {
        // A multiplexed 0x32 shorter than the documented 19 bytes must not lose
        // the stroke rate (it used to be dropped as a whole).
        const auto s17 = pm5::parseAdditionalStatus1(status1(true).left(17), pm5::Layout::Multiplexed);
        QVERIFY(s17);
        QCOMPARE(s17->strokesPerMinute, 24);
        QCOMPARE(s17->heartRateBpm, 142);
        QCOMPARE(s17->currentPace500Seconds, 117.65);
        QVERIFY(!s17->averagePowerW);
        const auto s18 = pm5::parseAdditionalStatus1(status1(true).left(18), pm5::Layout::Multiplexed);
        QVERIFY(s18);
        QCOMPARE(s18->strokesPerMinute, 24);
        QCOMPARE(*s18->averagePowerW, 187);
    }

    void additionalStatus1StrokeRateOffset_data()
    {
        QTest::addColumn<int>("size");
        QTest::addColumn<bool>("mux");
        QTest::newRow("direct 17") << 17 << false;
        QTest::newRow("direct 19") << 19 << false;
        QTest::newRow("mux 19") << 19 << true;
        QTest::newRow("mux 17") << 17 << true;
    }

    void additionalStatus1StrokeRateOffset()
    {
        QFETCH(int, size);
        QFETCH(bool, mux);
        // stroke rate is byte 5 in every layout; neighbours differ to catch an off-by-one
        QByteArray d = status1(size >= 19).left(size);
        d[4] = char(0x11);
        d[5] = char(31);
        d[6] = char(150);
        const auto s = pm5::parseAdditionalStatus1(d, mux ? pm5::Layout::Multiplexed : pm5::Layout::Direct);
        QVERIFY(s);
        QCOMPARE(s->strokesPerMinute, 31);
        QCOMPARE(s->heartRateBpm, 150);
        const auto p = pm5::parseMultiplexed(QByteArray(1, char(0x32)) + d);
        QVERIFY(p && p->additionalStatus1);
        QCOMPARE(p->additionalStatus1->strokesPerMinute, 31);
    }

    void additionalStatus1Multiplexed()
    {
        QByteArray d = status1(true);
        d[18] = char(1);   // erg type moves to byte 18
        const auto s = pm5::parseAdditionalStatus1(d, pm5::Layout::Multiplexed);
        QVERIFY(s);
        QCOMPARE(*s->averagePowerW, 187);
        QCOMPARE(s->ergMachineType, 1);
        QCOMPARE(s->restTimeSeconds, 45.0);
    }

    void additionalStatus2Direct()
    {
        const auto s = pm5::parseAdditionalStatus2(status2(false));
        QVERIFY(s);
        QCOMPARE(s->elapsedSeconds, 90.0);
        QCOMPARE(s->intervalCount, 3);
        QCOMPARE(*s->averagePowerW, 175);
        QCOMPARE(s->totalCalories, 42);
        QCOMPARE(s->splitAveragePace500Seconds, 125.0);
        QCOMPARE(s->splitAveragePowerW, 180);
        QCOMPARE(s->splitAverageCalories, 950);
        QCOMPARE(s->lastSplitTimeSeconds, 123.4);
        QCOMPARE(s->lastSplitDistanceMeters, 500);
        // a 20-byte direct packet carries average power; shorter ones are
        // taken as the 18-byte layout (see additionalStatus2DirectShort)
        QVERIFY(!pm5::parseAdditionalStatus2(status2(false).left(17)));
    }

    void additionalStatus2DirectShort()
    {
        // direct 0x0033 in the 18-byte layout (no average power): calories at 4-5
        const auto s = pm5::parseAdditionalStatus2(status2(true));
        QVERIFY(s);
        QVERIFY(!s->averagePowerW);
        QCOMPARE(s->totalCalories, 42);
        QCOMPARE(s->splitAveragePowerW, 180);
        QCOMPARE(s->lastSplitDistanceMeters, 500);
        // 19 bytes (one trailing byte): still the short layout
        const auto t = pm5::parseAdditionalStatus2(QByteArray(status2(true)) + QByteArray(1, '\x07'));
        QVERIFY(t && !t->averagePowerW);
        QCOMPARE(t->totalCalories, 42);
    }

    void additionalStatus1Direct19Bytes()
    {
        // current firmware: direct 0x0032 with 19 bytes = average power at
        // 16-17, machine type at 18
        QByteArray d = status1(true);
        QCOMPARE(d.size(), 19);
        d[18] = char(2);
        const auto s = pm5::parseAdditionalStatus1(d);
        QVERIFY(s);
        QCOMPARE(*s->averagePowerW, 187);
        QCOMPARE(s->ergMachineType, 2);
        QCOMPARE(s->strokesPerMinute, 24);
        QCOMPARE(s->restDistanceMeters, 300);   // metres per the interface definition
        QCOMPARE(s->restTimeSeconds, 45.0);
        // 18 bytes: ambiguous (byte 16 may be the average power low byte),
        // no machine type, no average power
        const auto e = pm5::parseAdditionalStatus1(QByteArray(status1(false)) + QByteArray(1, '\0'));
        QVERIFY(e && !e->averagePowerW);
        QCOMPARE(e->ergMachineType, 0);
    }

    void additionalStatus1Direct18BytesNoMachineType()
    {
        // direct 0x0032 of 18 bytes in the 19-byte layout cut short: byte 16
        // is the average power low byte (187 = 0xBB), not a machine type
        const QByteArray d = status1(true).left(18);
        QCOMPARE(d.size(), 18);
        QCOMPARE(int(quint8(d.at(16))), 187);
        const auto s = pm5::parseAdditionalStatus1(d);
        QVERIFY(s);
        QCOMPARE(s->ergMachineType, 0);   // default (rower), not 187
        QCOMPARE(pm5::machineKind(s->ergMachineType), pm5::machineKind(0));
        QVERIFY(!s->averagePowerW);
        QCOMPARE(s->strokesPerMinute, 24);
        // 17 bytes: machine type at 16
        QByteArray d17 = status1(false);
        QCOMPARE(d17.size(), 17);
        d17[16] = char(64);
        QCOMPARE(pm5::parseAdditionalStatus1(d17)->ergMachineType, 64);
        // >= 19 bytes: machine type at 18
        QByteArray d19 = status1(true);
        d19[18] = char(65);
        QCOMPARE(pm5::parseAdditionalStatus1(d19)->ergMachineType, 65);
        QCOMPARE(*pm5::parseAdditionalStatus1(d19)->averagePowerW, 187);
    }

    void additionalStrokeDataDirect17Bytes()
    {
        const auto s = pm5::parseAdditionalStrokeData(stroke(true));   // direct layout, 17 bytes
        QVERIFY(s);
        QCOMPARE(s->strokeCount, 57);
        QCOMPARE(*s->workPerStrokeJoules, 456.7);
        const auto t = pm5::parseAdditionalStrokeData(QByteArray(stroke(false)) + QByteArray(1, '\x01'));   // 16 bytes
        QVERIFY(t && !t->workPerStrokeJoules);
    }

    void additionalStatus2Multiplexed()
    {
        const QByteArray d = status2(true);
        QCOMPARE(d.size(), 18);
        const auto s = pm5::parseAdditionalStatus2(d, pm5::Layout::Multiplexed);
        QVERIFY(s);
        QVERIFY(!s->averagePowerW);
        QCOMPARE(s->totalCalories, 42);
        QCOMPARE(s->splitAveragePowerW, 180);
        QCOMPARE(s->lastSplitDistanceMeters, 500);
        QVERIFY(!pm5::parseAdditionalStatus2(d.left(17), pm5::Layout::Multiplexed));
    }

    void additionalStrokeData()
    {
        const auto s = pm5::parseAdditionalStrokeData(stroke(false));
        QVERIFY(s);
        QCOMPARE(s->elapsedSeconds, 70.0);
        QCOMPARE(s->strokePowerW, 212);
        QCOMPARE(s->strokeCaloriesPerHour, 1100);
        QCOMPARE(s->strokeCount, 57);
        QCOMPARE(s->projectedWorkTimeSeconds, 1800);
        QCOMPARE(s->projectedWorkDistanceMeters, 6500);
        QVERIFY(!s->workPerStrokeJoules);
        QVERIFY(!pm5::parseAdditionalStrokeData(stroke(false).left(14)));

        const auto m = pm5::parseAdditionalStrokeData(stroke(true), pm5::Layout::Multiplexed);
        QVERIFY(m);
        QCOMPARE(m->strokePowerW, 212);
        QCOMPARE(*m->workPerStrokeJoules, 456.7);
    }

    void expectedLengths()
    {
        QCOMPARE(pm5::expectedLength(pm5::kGeneralStatus, pm5::Layout::Direct), 19);
        QCOMPARE(pm5::expectedLength(pm5::kGeneralStatus, pm5::Layout::Multiplexed), 19);
        QCOMPARE(pm5::expectedLength(pm5::kAdditionalStatus1, pm5::Layout::Direct), 17);
        QCOMPARE(pm5::expectedLength(pm5::kAdditionalStatus1, pm5::Layout::Multiplexed), 19);
        QCOMPARE(pm5::expectedLength(pm5::kAdditionalStatus2, pm5::Layout::Direct), 20);
        QCOMPARE(pm5::expectedLength(pm5::kAdditionalStatus2, pm5::Layout::Multiplexed), 18);
        QCOMPARE(pm5::expectedLength(pm5::kAdditionalStrokeData, pm5::Layout::Direct), 15);
        QCOMPARE(pm5::expectedLength(pm5::kAdditionalStrokeData, pm5::Layout::Multiplexed), 17);
        QCOMPARE(pm5::expectedLength(0x0099, pm5::Layout::Direct), 0);
    }

    void multiplexedDispatch()
    {
        auto withId = [](int id, const QByteArray &payload) { return QByteArray(1, char(id)) + payload; };

        auto g = pm5::parseMultiplexed(withId(0x31, generalStatus()));
        QVERIFY(g && g->generalStatus);
        QCOMPARE(g->id, quint8(0x31));
        QCOMPARE(g->generalStatus->distanceMeters, 500.0);
        QVERIFY(!g->additionalStatus1 && !g->additionalStatus2 && !g->additionalStrokeData);

        auto a1 = pm5::parseMultiplexed(withId(0x32, status1(true)));
        QVERIFY(a1 && a1->additionalStatus1);
        QCOMPARE(*a1->additionalStatus1->averagePowerW, 187);
        QCOMPARE(a1->additionalStatus1->strokesPerMinute, 24);

        auto a2 = pm5::parseMultiplexed(withId(0x33, status2(true)));
        QVERIFY(a2 && a2->additionalStatus2);
        QCOMPARE(a2->additionalStatus2->totalCalories, 42);
        QVERIFY(!a2->additionalStatus2->averagePowerW);

        auto st = pm5::parseMultiplexed(withId(0x36, stroke(true)));
        QVERIFY(st && st->additionalStrokeData);
        QCOMPARE(st->additionalStrokeData->strokeCount, 57);
        QCOMPARE(*st->additionalStrokeData->workPerStrokeJoules, 456.7);
        QCOMPARE(st->payload.size(), 17);
    }

    void multiplexedUnhandledAndInvalid()
    {
        QVERIFY(!pm5::parseMultiplexed(QByteArray()));
        // Truncated payload of a supported id is rejected.
        QVERIFY(!pm5::parseMultiplexed(QByteArray(1, char(0x32)) + status1(false).left(16)));   // < 17
        QVERIFY(!pm5::parseMultiplexed(QByteArray(1, char(0x31)) + generalStatus().left(10)));
        // Other ids are reported but not decoded.
        const auto p = pm5::parseMultiplexed(QByteArray(1, char(0x35)) + QByteArray(18, '\0'));
        QVERIFY(p);
        QCOMPARE(p->id, quint8(0x35));
        QVERIFY(!p->decoded());
    }

    void workingStates()
    {
        QVERIFY(pm5::isWorkingState(int(pm5::WorkoutState::WorkoutRow)));
        QVERIFY(pm5::isWorkingState(int(pm5::WorkoutState::IntervalWorkDistance)));
        QVERIFY(!pm5::isWorkingState(int(pm5::WorkoutState::WaitToBegin)));
        QVERIFY(!pm5::isWorkingState(int(pm5::WorkoutState::IntervalRest)));
        QVERIFY(!pm5::isWorkingState(int(pm5::WorkoutState::WorkoutEnd)));
    }

    void paceWattsFormula()
    {
        // 2:00/500 m => 0.24 s/m => 2.8 / 0.013824 = 202.5 W
        QVERIFY(qAbs(pm5::wattsFromPace500(120.0) - 202.546) < 0.01);
        QVERIFY(qAbs(pm5::pace500FromWatts(202.546) - 120.0) < 0.01);
        QCOMPARE(pm5::pace500FromWatts(0), 0.0);
        QCOMPARE(pm5::wattsFromPace500(0), 0.0);
    }

    void machineKinds()
    {
        QCOMPARE(pm5::machineKind(0), pm5::MachineKind::Rower);       // static D
        QCOMPARE(pm5::machineKind(16), pm5::MachineKind::Rower);      // slides
        QCOMPARE(pm5::machineKind(64), pm5::MachineKind::Rower);      // dyno
        QCOMPARE(pm5::machineKind(224), pm5::MachineKind::Rower);     // MultiErg row
        QCOMPARE(pm5::machineKind(128), pm5::MachineKind::SkiErg);
        QCOMPARE(pm5::machineKind(143), pm5::MachineKind::SkiErg);
        QCOMPARE(pm5::machineKind(225), pm5::MachineKind::SkiErg);
        QCOMPARE(pm5::machineKind(192), pm5::MachineKind::BikeErg);
        QCOMPARE(pm5::machineKind(207), pm5::MachineKind::BikeErg);
        QCOMPARE(pm5::machineKind(226), pm5::MachineKind::BikeErg);
        QCOMPARE(pm5::machineKindFromName(QStringLiteral("PM5 430012345 Row")), pm5::MachineKind::Rower);
        QCOMPARE(pm5::machineKindFromName(QStringLiteral("PM5 430012345 Ski")), pm5::MachineKind::SkiErg);
        QCOMPARE(pm5::machineKindFromName(QStringLiteral("PM5 430012345 Bike")), pm5::MachineKind::BikeErg);
        QCOMPARE(pm5::machineKindFromName(QStringLiteral("PM5 430012345")), pm5::MachineKind::Rower);
    }
};

QTEST_GUILESS_MAIN(TestPm5Protocol)
#include "tst_pm5protocol.moc"
