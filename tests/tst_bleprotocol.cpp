#include "FtmsProtocol.h"
#include "HrsProtocol.h"

#include <QtTest>

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
};

QTEST_GUILESS_MAIN(TestBleProtocol)
#include "tst_bleprotocol.moc"
