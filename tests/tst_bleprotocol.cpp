#include "FtmsProtocol.h"
#include "HrsProtocol.h"

#include <QtTest>
#include <limits>

using namespace watt;

static QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray b;
    for (int v : values)
        b.append(char(quint8(v)));
    return b;
}

class TestBleProtocol : public QObject
{
    Q_OBJECT
private slots:
    void indoorBikeDataSpeedCadencePower()
    {
        // flags 0x0044: speed present (bit0 = 0), cadence (bit2), power (bit6)
        // speed 3000 (30.00 km/h), cadence 174 (87 rpm), power 185 W
        const QByteArray d = bytes({ 0x44, 0x00, 0xB8, 0x0B, 0xAE, 0x00, 0xB9, 0x00 });
        const auto r = ftms::parseIndoorBikeData(d);
        QVERIFY(r);
        QCOMPARE(*r->speedKmh, 30.0);
        QCOMPARE(*r->cadenceRpm, 87.0);
        QCOMPARE(*r->powerW, 185);
        QVERIFY(!r->heartRateBpm);
    }

    void indoorBikeDataAllFields()
    {
        // flags: bit0=1 (no speed), 1,2,3,4,5,6,7,8,9,10,11,12 set => 0x1FFF
        const QByteArray d = bytes({ 0xFF, 0x1F,
                                     0x10, 0x27,             // avg speed 100.00
                                     0xA0, 0x00,             // cadence 80
                                     0x96, 0x00,             // avg cadence 75
                                     0x10, 0x27, 0x00,       // distance 10000 m
                                     0x05, 0x00,             // resistance 5
                                     0xFA, 0x00,             // power 250
                                     0xC8, 0x00,             // avg power 200
                                     0x64, 0x00, 0x00, 0x00, 0x00,   // energy 100 kcal, /h, /min
                                     0x8C,                   // HR 140
                                     0x0A,                   // MET
                                     0x3C, 0x00,             // elapsed 60 s
                                     0x78, 0x00 });          // remaining 120 s
        const auto r = ftms::parseIndoorBikeData(d);
        QVERIFY(r);
        QVERIFY(!r->speedKmh);
        QCOMPARE(*r->averageSpeedKmh, 100.0);
        QCOMPARE(*r->cadenceRpm, 80.0);
        QCOMPARE(*r->totalDistanceM, 10000);
        QCOMPARE(*r->resistanceLevel, 5);
        QCOMPARE(*r->powerW, 250);
        QCOMPARE(*r->averagePowerW, 200);
        QCOMPARE(*r->totalEnergyKcal, 100);
        QCOMPARE(*r->heartRateBpm, 140);
        QCOMPARE(*r->elapsedTimeS, 60);
        QCOMPARE(*r->remainingTimeS, 120);
    }

    void indoorBikeDataNegativePowerAndTruncated()
    {
        const auto neg = ftms::parseIndoorBikeData(bytes({ 0x41, 0x00, 0xF6, 0xFF }));   // no speed, power -10
        QVERIFY(neg);
        QCOMPARE(*neg->powerW, -10);
        QVERIFY(!ftms::parseIndoorBikeData(bytes({ 0x44, 0x00, 0xB8 })));
        QVERIFY(!ftms::parseIndoorBikeData(QByteArray()));
    }

    void features()
    {
        // machine: cadence(bit1) + power(bit14) ; target: power(bit3) + simulation(bit13)
        const auto f = ftms::parseFeatures(bytes({ 0x02, 0x40, 0x00, 0x00, 0x08, 0x20, 0x00, 0x00 }));
        QVERIFY(f);
        QVERIFY(f->cadence);
        QVERIFY(f->powerMeasurement);
        QVERIFY(f->powerTarget);
        QVERIFY(f->simulation);
        QVERIFY(!f->resistanceTarget);
        QVERIFY(!ftms::parseFeatures(bytes({ 0x01 })));
    }

    void controlPointCommands()
    {
        QCOMPARE(ftms::requestControl(), bytes({ 0x00 }));
        QCOMPARE(ftms::startOrResume(), bytes({ 0x07 }));
        QCOMPARE(ftms::stopOrPause(true), bytes({ 0x08, 0x02 }));
        QCOMPARE(ftms::setTargetPower(190), bytes({ 0x05, 0xBE, 0x00 }));
        QCOMPARE(ftms::setTargetPower(300), bytes({ 0x05, 0x2C, 0x01 }));
        QCOMPARE(ftms::setTargetPower(-5), bytes({ 0x05, 0x00, 0x00 }));
        // wind 0, grade 1.5% = 150, crr 0.004 = 40, cw 0.51 = 51
        QCOMPARE(ftms::setIndoorBikeSimulation(0, 1.5, 0.004, 0.51),
                 bytes({ 0x11, 0x00, 0x00, 0x96, 0x00, 0x28, 0x33 }));
        QCOMPARE(ftms::setIndoorBikeSimulation(0, -2.0, 0.004, 0.51).mid(3, 2), bytes({ 0x38, 0xFF }));
    }

    void indoorBikeSimulationEncoding()
    {
        // grade -5.5 % = -550 = 0xFDDA (SInt16 LE), what Zwift sends for Crr/Cw
        QCOMPARE(ftms::setIndoorBikeSimulation(0, -5.5, 0.004, 0.51),
                 bytes({ 0x11, 0x00, 0x00, 0xDA, 0xFD, 0x28, 0x33 }));
        // the frame Zwift was logged sending: 11 00 00 ee 00 28 33 = +2.38 %
        QCOMPARE(ftms::setIndoorBikeSimulation(0, 2.38, 0.004, 0.51),
                 bytes({ 0x11, 0x00, 0x00, 0xEE, 0x00, 0x28, 0x33 }));
        // wind SInt16 0.001 m/s, rounding to the 0.01 % grade resolution
        QCOMPARE(ftms::setIndoorBikeSimulation(-1.5, 0.004, 0.004, 0.51).mid(1, 4),
                 bytes({ 0x24, 0xFA, 0x00, 0x00 }));
        QCOMPARE(ftms::setIndoorBikeSimulation(0, 10.0, 0.004, 0.51).mid(3, 2), bytes({ 0xE8, 0x03 }));
        QCOMPARE(ftms::setIndoorBikeSimulation(0, -10.0, 0.004, 0.51).mid(3, 2), bytes({ 0x18, 0xFC }));
        // Crr/Cw UInt8 saturate
        QCOMPARE(ftms::setIndoorBikeSimulation(0, 0, 0.1, 9.0).mid(5, 2), bytes({ 0xFF, 0xFF }));
        QCOMPARE(ftms::setIndoorBikeSimulation(0, 0, -1, -1).mid(5, 2), bytes({ 0x00, 0x00 }));
    }

    void indoorBikeSimulationNonFinite()
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        // NaN / inf: wind and grade 0, Crr 0.004 (40), Cw 0.39 kg/m (39)
        QCOMPARE(ftms::setIndoorBikeSimulation(nan, nan, nan, nan),
                 bytes({ 0x11, 0x00, 0x00, 0x00, 0x00, 0x28, 0x27 }));
        QCOMPARE(ftms::setIndoorBikeSimulation(inf, -inf, inf, -inf),
                 bytes({ 0x11, 0x00, 0x00, 0x00, 0x00, 0x28, 0x27 }));
        // huge finite values saturate instead of overflowing lround()
        QCOMPARE(ftms::setIndoorBikeSimulation(1e300, -1e300, 1e300, 1e300),
                 bytes({ 0x11, 0xFF, 0x7F, 0x00, 0x80, 0xFF, 0xFF }));
        // valid values are untouched by the guard
        QCOMPARE(ftms::setIndoorBikeSimulation(0, 2.38, 0.004, 0.51),
                 bytes({ 0x11, 0x00, 0x00, 0xEE, 0x00, 0x28, 0x33 }));
        QCOMPARE(ftms::setTargetResistance(nan), bytes({ 0x04, 0x00 }));
        QCOMPARE(ftms::setTargetResistance(1e300), bytes({ 0x04, 0xFF }));
    }

    void controlRefusalGuard()
    {
        using G = ftms::ControlRefusalGuard;
        G g;
        const quint8 power = quint8(ftms::OpCode::SetTargetPower);
        const quint8 sim = quint8(ftms::OpCode::SetIndoorBikeSimulation);
        // the first refusals ask for control again at once ...
        QCOMPARE(g.onNotPermitted(power), G::Action::RequestNow);
        QCOMPARE(g.onNotPermitted(power), G::Action::RequestNow);
        // ... a trainer that keeps refusing the same command only gets the
        // delayed retry (no endless request/refuse loop)
        QCOMPARE(g.onNotPermitted(power), G::Action::RetryLater);
        QCOMPARE(g.onNotPermitted(power), G::Action::RetryLater);
        QCOMPARE(g.count(), 4);
        // other op codes succeeding (request control, start) do not clear it
        g.onSuccess(quint8(ftms::OpCode::RequestControl));
        g.onSuccess(quint8(ftms::OpCode::StartOrResume));
        QCOMPARE(g.onNotPermitted(power), G::Action::RetryLater);
        // the refused command going through does
        g.onSuccess(power);
        QCOMPARE(g.count(), 0);
        QCOMPARE(g.onNotPermitted(power), G::Action::RequestNow);
        // a different command starts its own count
        QCOMPARE(g.onNotPermitted(sim), G::Action::RequestNow);
        QCOMPARE(g.count(), 1);
        QCOMPARE(g.opCode(), sim);
        g.reset();
        QCOMPARE(g.count(), 0);
    }

    void featureBitsForResistanceModes()
    {
        // target features: bit 3 power target only -> ERG but no simulation
        auto f = ftms::parseFeatures(bytes({ 0x02, 0x40, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 }));
        QVERIFY(f);
        QVERIFY(f->powerTarget);
        QVERIFY(!f->simulation);
        QVERIFY(!f->resistanceTarget);
        // bit 13 simulation only (0x2000)
        f = ftms::parseFeatures(bytes({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00 }));
        QVERIFY(f);
        QVERIFY(f->simulation);
        QVERIFY(!f->powerTarget);
        // bit 2 resistance target
        f = ftms::parseFeatures(bytes({ 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00 }));
        QVERIFY(f && f->resistanceTarget && !f->simulation);
        // 7 bytes: too short
        QVERIFY(!ftms::parseFeatures(bytes({ 0x00, 0x00, 0x00, 0x00, 0x08, 0x20, 0x00 })));
    }

    void simulationRejectedResponse()
    {
        // "Op Code not supported" for 0x11 -> fallback
        const auto r = ftms::parseControlPointResponse(bytes({ 0x80, 0x11, 0x02 }));
        QVERIFY(r);
        QCOMPARE(r->requestOpCode, quint8(ftms::OpCode::SetIndoorBikeSimulation));
        QCOMPARE(r->result, quint8(ftms::ResultCode::NotSupported));
        QVERIFY(!r->success());
    }

    void controlPointResponse()
    {
        const auto ok = ftms::parseControlPointResponse(bytes({ 0x80, 0x05, 0x01 }));
        QVERIFY(ok);
        QCOMPARE(ok->requestOpCode, quint8(0x05));
        QVERIFY(ok->success());
        const auto denied = ftms::parseControlPointResponse(bytes({ 0x80, 0x05, 0x05 }));
        QVERIFY(denied && !denied->success());
        QVERIFY(!ftms::parseControlPointResponse(bytes({ 0x05, 0x01 })));
    }

    void heartRate8bit()
    {
        const auto m = hrs::parseMeasurement(bytes({ 0x06, 0x8E }));   // contact detected, 142 bpm
        QVERIFY(m);
        QCOMPARE(m->bpm, 142);
        QVERIFY(m->contactSupported);
        QVERIFY(m->contactDetected);
    }

    void heartRate16bitWithEnergyAndRr()
    {
        // flags: 16-bit (0x01) + energy (0x08) + RR (0x10)
        const auto m = hrs::parseMeasurement(bytes({ 0x19, 0x2C, 0x01, 0x0A, 0x00, 0x00, 0x04, 0x00, 0x02 }));
        QVERIFY(m);
        QCOMPARE(m->bpm, 300);
        QCOMPARE(*m->energyExpendedKj, 10);
        QCOMPARE(m->rrIntervalsMs.size(), 2);
        QCOMPARE(m->rrIntervalsMs.at(0), 1000.0);   // 1024/1024 s
        QCOMPARE(m->rrIntervalsMs.at(1), 500.0);
    }

    void heartRateMalformed()
    {
        QVERIFY(!hrs::parseMeasurement(bytes({ 0x00 })));
        QVERIFY(!hrs::parseMeasurement(bytes({ 0x01, 0x50 })));
    }

    // --- FTMS advertising service data (FTMS v1.0 §3.1) ---------------------

    void serviceDataRower()
    {
        // Flags: machine available; type: Rower (bit 4)
        const auto d = ftms::parseServiceData(bytes({ 0x01, 0x10, 0x00 }));
        QVERIFY(d);
        QVERIFY(d->available);
        QCOMPARE(d->machineType, quint16(0x0010));
        QVERIFY(d->rower());
        QVERIFY(!d->indoorBike());
        QVERIFY(d->rowerOnly());
        QCOMPARE(ftms::machineTypeText(d->machineType), QStringLiteral("Rower"));
    }

    void serviceDataBike()
    {
        const auto d = ftms::parseServiceData(bytes({ 0x01, 0x20, 0x00 }));
        QVERIFY(d);
        QVERIFY(d->indoorBike());
        QVERIFY(!d->rower());
        QVERIFY(!d->rowerOnly());
        QCOMPARE(ftms::machineTypeText(0x0020), QStringLiteral("Indoor Bike"));
    }

    void serviceDataBothAndOthers()
    {
        const auto both = ftms::parseServiceData(bytes({ 0x00, 0x30, 0x00 }));
        QVERIFY(both);
        QVERIFY(!both->available);
        QVERIFY(both->rower() && both->indoorBike());
        QVERIFY(!both->rowerOnly());   // ambiguous: stays a trainer
        QCOMPARE(ftms::machineTypeText(0x0030), QStringLiteral("Rower, Indoor Bike"));
        const auto treadmill = ftms::parseServiceData(bytes({ 0x01, 0x01, 0x00 }));
        QVERIFY(treadmill && !treadmill->rower() && !treadmill->indoorBike());
        QCOMPARE(ftms::machineTypeText(0), QStringLiteral("none"));
        QVERIFY(ftms::machineTypeText(0x0110).contains(QStringLiteral("RFU")));
        // high byte of the little-endian type is read
        const auto hi = ftms::parseServiceData(bytes({ 0x01, 0x10, 0x01 }));
        QCOMPARE(hi->machineType, quint16(0x0110));
        QVERIFY(hi->rowerOnly());
    }

    void serviceDataWithUuidPrefixAndMalformed()
    {
        // value still starting with the 16-bit UUID (26 18)
        const auto d = ftms::parseServiceData(bytes({ 0x26, 0x18, 0x01, 0x10, 0x00 }));
        QVERIFY(d && d->rowerOnly());
        QVERIFY(!ftms::parseServiceData(QByteArray()));
        QVERIFY(!ftms::parseServiceData(bytes({ 0x01 })));
        QVERIFY(!ftms::parseServiceData(bytes({ 0x01, 0x10 })));
    }

    // --- FTMS Rower Data (FTMS v1.0 §4.8) -------------------------------------

    void rowerDataStrokeOnly()
    {
        // flags 0x0000: stroke rate (0.5/min) + stroke count present
        const auto d = ftms::parseRowerData(bytes({ 0x00, 0x00, 49, 0x2A, 0x00 }));
        QVERIFY(d);
        QVERIFY(!d->moreData);
        QCOMPARE(*d->strokeRateSpm, 24.5);
        QCOMPARE(*d->strokeCount, 42);
        QVERIFY(!d->powerW && !d->totalDistanceM && !d->pace500S);
    }

    void rowerDataTypicalPm5()
    {
        // bits 2 (distance), 3 (pace), 5 (power), 8 (energy), 11 (elapsed) = 0x092C
        const auto d = ftms::parseRowerData(bytes({ 0x2C, 0x09,
                                                    48, 0x10, 0x00,           // 24 spm, 16 strokes
                                                    0x88, 0x13, 0x00,         // 5000 m
                                                    0x78, 0x00,               // 120 s / 500 m
                                                    0xCB, 0x00,               // 203 W
                                                    0x2A, 0x00, 0x20, 0x03, 0x0E,   // 42 kcal, 800/h, 14/min
                                                    0x2C, 0x01 }));           // 300 s
        QVERIFY(d);
        QCOMPARE(*d->strokeRateSpm, 24.0);
        QCOMPARE(*d->strokeCount, 16);
        QCOMPARE(*d->totalDistanceM, 5000);
        QCOMPARE(*d->pace500S, 120);
        QCOMPARE(*d->powerW, 203);
        QCOMPARE(*d->totalEnergyKcal, 42);
        QCOMPARE(*d->energyPerHourKcal, 800);
        QCOMPARE(*d->energyPerMinuteKcal, 14);
        QCOMPARE(*d->elapsedTimeS, 300);
        QVERIFY(!d->averagePowerW && !d->heartRateBpm && !d->remainingTimeS);
    }

    void rowerDataAllFields()
    {
        // every flag 1..12 set, More Data clear
        const auto d = ftms::parseRowerData(bytes({ 0xFE, 0x1F,
                                                    40, 0x05, 0x00,           // 20 spm, 5 strokes
                                                    41,                       // avg 20.5 spm
                                                    0x10, 0x27, 0x00,         // 10000 m
                                                    0x7D, 0x00,               // pace 125 s
                                                    0x82, 0x00,               // avg pace 130 s
                                                    0xFF, 0xFF,               // power -1 (SInt16)
                                                    0x96, 0x00,               // avg power 150
                                                    0x0A, 0x00,               // resistance 10
                                                    0x64, 0x00, 0x58, 0x02, 0x0A,   // 100 kcal, 600/h, 10/min
                                                    0x8C,                     // HR 140
                                                    0x55,                     // MET 8.5
                                                    0x10, 0x0E,               // elapsed 3600 s
                                                    0x3C, 0x00 }));           // remaining 60 s
        QVERIFY(d);
        QCOMPARE(*d->strokeRateSpm, 20.0);
        QCOMPARE(*d->strokeCount, 5);
        QCOMPARE(*d->averageStrokeRateSpm, 20.5);
        QCOMPARE(*d->totalDistanceM, 10000);
        QCOMPARE(*d->pace500S, 125);
        QCOMPARE(*d->averagePace500S, 130);
        QCOMPARE(*d->powerW, -1);
        QCOMPARE(*d->averagePowerW, 150);
        QCOMPARE(*d->resistanceLevel, 10);
        QCOMPARE(*d->totalEnergyKcal, 100);
        QCOMPARE(*d->heartRateBpm, 140);
        QCOMPARE(*d->metabolicEquivalent, 8.5);
        QCOMPARE(*d->elapsedTimeS, 3600);
        QCOMPARE(*d->remainingTimeS, 60);
    }

    void rowerDataMoreDataAndMalformed()
    {
        // More Data set: no stroke rate/count, power follows the flags directly
        const auto d = ftms::parseRowerData(bytes({ 0x21, 0x00, 0xC8, 0x00 }));
        QVERIFY(d);
        QVERIFY(d->moreData);
        QVERIFY(!d->strokeRateSpm && !d->strokeCount);
        QCOMPARE(*d->powerW, 200);
        QVERIFY(!ftms::parseRowerData(QByteArray()));
        QVERIFY(!ftms::parseRowerData(bytes({ 0x00 })));
        QVERIFY(!ftms::parseRowerData(bytes({ 0x00, 0x00, 40, 0x05 })));    // stroke count cut
        QVERIFY(!ftms::parseRowerData(bytes({ 0x2C, 0x09, 48, 0x10, 0x00, 0x88, 0x13 })));
    }

    void rowerDataAssemblerMergesSplitRecord()
    {
        ftms::RowerDataAssembler a;
        // packet 1: More Data, distance + pace
        QVERIFY(!a.feed(bytes({ 0x0D, 0x00, 0xE8, 0x03, 0x00, 0x78, 0x00 })));
        // packet 2: More Data, power + elapsed
        QVERIFY(!a.feed(bytes({ 0x21, 0x08, 0xCB, 0x00, 0x3C, 0x00 })));
        // packet 3: last one, stroke rate + count
        const auto d = a.feed(bytes({ 0x00, 0x00, 50, 0x07, 0x00 }));
        QVERIFY(d);
        QVERIFY(!d->moreData);
        QCOMPARE(*d->totalDistanceM, 1000);
        QCOMPARE(*d->pace500S, 120);
        QCOMPARE(*d->powerW, 203);
        QCOMPARE(*d->elapsedTimeS, 60);
        QCOMPARE(*d->strokeRateSpm, 25.0);
        QCOMPARE(*d->strokeCount, 7);
        // the buffer is empty again
        const auto next = a.feed(bytes({ 0x00, 0x00, 50, 0x08, 0x00 }));
        QVERIFY(next && !next->totalDistanceM);
        QCOMPARE(*next->strokeCount, 8);
        // malformed packet: ignored
        QVERIFY(!a.feed(bytes({ 0x00 })));
    }

    void rowerDataAssemblerReleasesNeverEndingRecord()
    {
        // a device that always sets More Data: the record is released when
        // the next packet repeats a field
        ftms::RowerDataAssembler a;
        QVERIFY(!a.feed(bytes({ 0x21, 0x00, 0xC8, 0x00 })));
        const auto d = a.feed(bytes({ 0x21, 0x00, 0xD2, 0x00 }));
        QVERIFY(d);
        QCOMPARE(*d->powerW, 200);
        a.reset();
        QVERIFY(!a.feed(bytes({ 0x21, 0x00, 0xD2, 0x00 })));
        const auto e = a.feed(bytes({ 0x00, 0x00, 40, 0x01, 0x00 }));
        QCOMPARE(*e->powerW, 210);
        QCOMPARE(*e->strokeCount, 1);

        ftms::RowerData empty;
        QVERIFY(empty.isEmpty());
        QVERIFY(!e->isEmpty());
        QVERIFY(e->overlaps(*e));
        QVERIFY(!empty.overlaps(*e));
    }

    // --- readable text for the diagnostic log ---------------------------------
    void describeSimulationCommand()
    {
        // 0x11: wind 0, grade -2.20 %, Crr 0.0040, Cw 0.39 kg/m
        const QByteArray cmd = ftms::setIndoorBikeSimulation(0.0, -2.2, 0.004, 0.39);
        QCOMPARE(cmd, bytes({ 0x11, 0x00, 0x00, 0x24, 0xFF, 0x28, 0x27 }));
        QCOMPARE(ftms::describeControlPoint(cmd),
                 QStringLiteral("0x11 Set Indoor Bike Simulation: wind 0.000 m/s, grade -2.20 %, Crr 0.0040, Cw 0.39 kg/m"));
        // wind 1.5 m/s, grade 6 %, Crr 0.0050, Cw 0.51
        QCOMPARE(ftms::describeControlPoint(ftms::setIndoorBikeSimulation(1.5, 6.0, 0.005, 0.51)),
                 QStringLiteral("0x11 Set Indoor Bike Simulation: wind 1.500 m/s, grade 6.00 %, Crr 0.0050, Cw 0.51 kg/m"));
        QCOMPARE(ftms::describeControlPoint(bytes({ 0x11, 0x00 })),
                 QStringLiteral("0x11 Set Indoor Bike Simulation: (truncated)"));
    }

    void describeOtherCommands()
    {
        QCOMPARE(ftms::describeControlPoint(ftms::setTargetPower(150)), QStringLiteral("0x05 Set Target Power: 150 W"));
        QCOMPARE(ftms::describeControlPoint(ftms::requestControl()), QStringLiteral("0x00 Request Control"));
        QCOMPARE(ftms::describeControlPoint(ftms::startOrResume()), QStringLiteral("0x07 Start or Resume"));
        QCOMPARE(ftms::describeControlPoint(ftms::stopOrPause(true)), QStringLiteral("0x08 Stop or Pause: pause"));
        QCOMPARE(ftms::describeControlPoint(ftms::setTargetResistance(10.0)),
                 QStringLiteral("0x04 Set Target Resistance Level: level 10.0"));
        QCOMPARE(ftms::describeControlPoint(bytes({ 0x03, 0x2C, 0x01 })),
                 QStringLiteral("0x03 Set Target Inclination: 30.0 %"));
        QCOMPARE(ftms::describeControlPoint(bytes({ 0x42, 0x01 })), QStringLiteral("0x42 Op 0x42 (+01)"));
        QCOMPARE(ftms::describeControlPoint(QByteArray()), QStringLiteral("(empty)"));
    }

    void describeResponses()
    {
        QCOMPARE(ftms::describeControlPointResponse(bytes({ 0x80, 0x11, 0x01 })),
                 QStringLiteral("Response to 0x11 Set Indoor Bike Simulation: Success (0x01)"));
        QCOMPARE(ftms::describeControlPointResponse(bytes({ 0x80, 0x05, 0x05 })),
                 QStringLiteral("Response to 0x05 Set Target Power: Control not permitted (0x05)"));
        QCOMPARE(ftms::describeControlPointResponse(bytes({ 0x80, 0x11, 0x02 })),
                 QStringLiteral("Response to 0x11 Set Indoor Bike Simulation: Not supported (0x02)"));
        QCOMPARE(ftms::describeControlPointResponse(bytes({ 0x80, 0x00, 0x03 })),
                 QStringLiteral("Response to 0x00 Request Control: Invalid parameter (0x03)"));
        QCOMPARE(ftms::describeControlPointResponse(bytes({ 0x80, 0x00, 0x04, 0xAA })),
                 QStringLiteral("Response to 0x00 Request Control: Operation failed (0x04) params aa"));
        QVERIFY(ftms::describeControlPointResponse(bytes({ 0x11, 0x00 })).startsWith(QStringLiteral("(not a response")));
    }

    void describeStatusNotifications()
    {
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0x12, 0x00, 0x00, 0x96, 0x00, 0x28, 0x27 })),
                 QStringLiteral("Indoor Bike Simulation Parameters Changed: wind 0.000 m/s, grade 1.50 %, Crr 0.0040, Cw 0.39 kg/m"));
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0x08, 0x2C, 0x01 })), QStringLiteral("Target Power Changed: 300 W"));
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0xFF })), QStringLiteral("Control Permission Lost"));
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0x04 })), QStringLiteral("Started or Resumed by the User"));
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0x02, 0x02 })), QStringLiteral("Stopped or Paused by the User (pause)"));
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0x07, 0x64 })), QStringLiteral("Target Resistance Level Changed: 10.0"));
        QCOMPARE(ftms::describeMachineStatus(bytes({ 0x08 })), QStringLiteral("Target Power Changed: 0 W (truncated)"));
        // Training Status 0x2AD3
        QCOMPARE(ftms::describeTrainingStatus(bytes({ 0x00, 0x09 })),
                 QStringLiteral("Speed Outside of Control Region - Low"));
        QCOMPARE(ftms::describeTrainingStatus(bytes({ 0x01, 0x0D, 'Q', 'S' })),
                 QStringLiteral("Manual Mode (Quick Start) \"QS\""));
    }

    void describeFeaturesAndRanges()
    {
        // machine: cadence (1), resistance level (7), power measurement (14);
        // targets: resistance (2), power (3), indoor bike simulation (13)
        const QByteArray f = bytes({ 0x82, 0x40, 0x00, 0x00, 0x0C, 0x20, 0x00, 0x00 });
        QCOMPARE(ftms::describeFeatures(f),
                 QStringLiteral("machine [Cadence, Resistance Level, Power Measurement] (0x00004082); "
                                "targets [Resistance Target, Power Target, Indoor Bike Simulation] (0x0000200c)"));
        QVERIFY(ftms::describeFeatures(bytes({ 0x01 })).startsWith(QStringLiteral("(unreadable")));
        QCOMPARE(ftms::describeRange(ftms::kSupportedPowerRangeUuid, bytes({ 0x00, 0x00, 0xD0, 0x07, 0x01, 0x00 })),
                 QStringLiteral("power 0..2000 W, step 1 W"));
        QCOMPARE(ftms::describeRange(ftms::kSupportedInclinationRangeUuid, bytes({ 0x9C, 0xFF, 0xC8, 0x00, 0x01, 0x00 })),
                 QStringLiteral("inclination -10.0..20.0 %, step 0.1 %"));
        QCOMPARE(ftms::describeRange(ftms::kSupportedResistanceRangeUuid, bytes({ 0x00, 0x00, 0xE8, 0x03, 0x0A, 0x00 })),
                 QStringLiteral("resistance level 0.0..100.0, step 1.0"));
        // Indoor Bike Data
        const auto d = ftms::parseIndoorBikeData(bytes({ 0x64, 0x00, 0xB8, 0x0B, 0xAE, 0x00, 0x0C, 0x00, 0xB9, 0x00 }));
        QVERIFY(d);
        QCOMPARE(ftms::describeIndoorBikeData(*d), QStringLiteral("speed 30.00 km/h, cadence 87.0 rpm, power 185 W, resistance 12"));
        QCOMPARE(ftms::opCodeName(0x11), QStringLiteral("Set Indoor Bike Simulation"));
    }
};

QTEST_GUILESS_MAIN(TestBleProtocol)
#include "tst_bleprotocol.moc"
