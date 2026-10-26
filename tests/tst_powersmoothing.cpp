// Display smoothing of the cycling power: PowerSmoother (time-weighted
// rolling average) and SensorHub::displayPower.

#include "PowerSmoother.h"
#include "SensorHub.h"

#include <QSignalSpy>
#include <QTest>

using namespace watt;

class TstPowerSmoothing : public QObject
{
    Q_OBJECT
private slots:
    void emptyIsZero();
    void firstSampleShownImmediately();
    void constantPower();
    void averageOverWindow();
    void timeWeightedNotSampleCounted();
    void irregularRates();
    void oldSamplesLeaveWindow();
    void staleSamplesDropToZero();
    void zeroSamplesDecay();
    void windowLengths();

    void hubDefaults();
    void hubSmoothing();
    void hubToggle();
    void hubWindowSeconds();
    void hubTrainerStops();
    void hubDisconnectClears();
    void hubRawPowerUnchanged();
};

void TstPowerSmoothing::emptyIsZero()
{
    PowerSmoother s;
    QCOMPARE(s.average(0), 0.0);
    QCOMPARE(s.averageRounded(12345), 0);
    QVERIFY(s.isEmpty());
}

void TstPowerSmoothing::firstSampleShownImmediately()
{
    PowerSmoother s;
    s.addSample(1000, 250);
    QCOMPARE(s.average(1000), 250.0);
    QCOMPARE(s.average(1500), 250.0);
}

void TstPowerSmoothing::constantPower()
{
    PowerSmoother s;
    for (int t = 0; t <= 10000; t += 250)
        s.addSample(t, 200);
    QCOMPARE(s.average(10000), 200.0);
    QCOMPARE(s.average(10200), 200.0);
}

void TstPowerSmoothing::averageOverWindow()
{
    // 1 Hz: 100, 200, 300, 400 at t = 0, 1, 2, 3 s. At t = 4 s the window
    // (1 s, 4 s] holds 200, 300, 400 for one second each.
    PowerSmoother s;
    s.addSample(0, 100);
    s.addSample(1000, 200);
    s.addSample(2000, 300);
    s.addSample(3000, 400);
    QCOMPARE(s.average(4000), 300.0);
    // half way: (1.5 s, 4.5 s] = 0.5*200 + 1*300 + 1*400 + 0.5*400 (held)
    QCOMPARE(s.average(4500), (0.5 * 200 + 300 + 400 + 0.5 * 400) / 3.0);
}

void TstPowerSmoothing::timeWeightedNotSampleCounted()
{
    // 2 s of 100 W at 4 Hz (8 samples), then 1 s of 400 W at 1 Hz (1 sample):
    // a sample-count average would give ~133 W, the time average is 200 W.
    PowerSmoother s;
    for (int t = 0; t < 2000; t += 250)
        s.addSample(t, 100);
    s.addSample(2000, 400);
    QCOMPARE(s.average(3000), 200.0);
}

void TstPowerSmoothing::irregularRates()
{
    PowerSmoother s;
    // jittery intervals around 1 Hz, alternating legs 180 / 220 W
    const int times[] = { 0, 900, 2100, 2950, 4050, 5000, 6100, 6900 };
    int i = 0;
    for (int t : times)
        s.addSample(t, (i++ % 2) ? 220 : 180);
    const double avg = s.average(7500);
    QVERIFY2(avg > 185 && avg < 215, qPrintable(QString::number(avg)));
    QCOMPARE(s.averageRounded(7000), int(std::lround(s.average(7000))));
}

void TstPowerSmoothing::oldSamplesLeaveWindow()
{
    PowerSmoother s;
    s.addSample(0, 1000);          // sprint
    for (int t = 1000; t <= 6000; t += 1000)
        s.addSample(t, 150);
    QCOMPARE(s.average(6000), 150.0);   // the 1000 W sample is out of the window
}

void TstPowerSmoothing::staleSamplesDropToZero()
{
    PowerSmoother s;
    s.addSample(0, 200);
    s.addSample(1000, 200);
    // trainer stops sending: last value held for up to 3 s, then nothing
    QCOMPARE(s.average(2500), 200.0);
    QCOMPARE(s.average(3999), 200.0);
    QCOMPARE(s.average(4000), 0.0);
    QCOMPARE(s.average(60000), 0.0);
    // data again: shown at once, the old samples no longer count
    s.addSample(60000, 300);
    QCOMPARE(s.average(60000), 300.0);
    QCOMPARE(s.average(60500), 300.0);
}

void TstPowerSmoothing::zeroSamplesDecay()
{
    // the rider stops pedalling and the trainer reports 0 W: smooth decay
    PowerSmoother s;
    for (int t = 0; t <= 3000; t += 1000)
        s.addSample(t, 300);
    s.addSample(4000, 0);
    s.addSample(5000, 0);
    QCOMPARE(s.average(5000), 200.0);   // 300, 300, 0
    s.addSample(6000, 0);
    QCOMPARE(s.average(6000), 100.0);   // 300, 0, 0
    s.addSample(7000, 0);
    QCOMPARE(s.average(7000), 0.0);
}

void TstPowerSmoothing::windowLengths()
{
    PowerSmoother s(1000);
    s.addSample(0, 100);
    s.addSample(1000, 300);
    QCOMPARE(s.average(1000), 100.0);   // the new sample has no weight yet
    QCOMPARE(s.average(1500), 200.0);   // (0.5 s, 1.5 s]: half 100, half 300
    s.setWindowMs(10000);
    QCOMPARE(s.windowMs(), 10000);
    s.setHoldMs(5000);
    QCOMPARE(s.average(2000), 200.0);
    s.clear();
    QCOMPARE(s.average(2000), 0.0);
}

// ---------------------------------------------------------------- SensorHub

void TstPowerSmoothing::hubDefaults()
{
    SensorHub hub;
    QVERIFY(hub.powerSmoothing());
    QCOMPARE(hub.powerSmoothingSeconds(), 3);
    QCOMPARE(hub.displayPower(), 0);
}

void TstPowerSmoothing::hubSmoothing()
{
    SensorHub hub;
    qint64 clock = 10000;
    hub.setClockForTesting([&clock] { return clock; });
    QSignalSpy spy(&hub, &SensorHub::displayPowerChanged);
    hub.updatePower(180);
    QCOMPARE(hub.displayPower(), 180);
    QCOMPARE(spy.count(), 1);
    clock += 1000;
    hub.updatePower(220);
    clock += 1000;
    hub.updatePower(180);
    clock += 1000;
    hub.updatePower(220);
    QCOMPARE(hub.power(), 220);
    // (10 s, 13 s]: 180, 220, 180 for one second each (the new 220 W sample
    // has no weight yet)
    QCOMPARE(hub.displayPower(), 193);
    clock += 500;
    hub.refreshDisplayPower();
    QCOMPARE(hub.displayPower(), 200);
}

void TstPowerSmoothing::hubToggle()
{
    SensorHub hub;
    qint64 clock = 0;
    hub.setClockForTesting([&clock] { return clock; });
    QSignalSpy toggled(&hub, &SensorHub::powerSmoothingChanged);
    hub.updatePower(100);
    clock = 1000;
    hub.updatePower(300);
    clock = 2000;
    QCOMPARE(hub.displayPower(), 100);   // not refreshed yet
    hub.refreshDisplayPower();
    QCOMPARE(hub.displayPower(), 200);

    hub.setPowerSmoothing(false);
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(hub.displayPower(), 300);   // raw
    clock = 2500;
    hub.updatePower(150);
    QCOMPARE(hub.displayPower(), 150);

    hub.setPowerSmoothing(true);         // samples kept collecting while off
    QCOMPARE(toggled.count(), 2);
    QCOMPARE(hub.displayPower(), int(std::lround((1000 * 100 + 1500 * 300 + 0) / 2500.0)));
    hub.setPowerSmoothing(true);
    QCOMPARE(toggled.count(), 2);
}

void TstPowerSmoothing::hubWindowSeconds()
{
    SensorHub hub;
    qint64 clock = 0;
    hub.setClockForTesting([&clock] { return clock; });
    QSignalSpy changed(&hub, &SensorHub::powerSmoothingChanged);
    for (int i = 0; i < 10; ++i) {
        hub.updatePower(i < 5 ? 100 : 300);
        clock += 1000;
    }
    hub.refreshDisplayPower();
    QCOMPARE(hub.displayPower(), 300);   // 3 s: only the 300 W part
    hub.setPowerSmoothingSeconds(10);
    QCOMPARE(hub.powerSmoothingSeconds(), 10);
    QCOMPARE(changed.count(), 1);
    // only samples kept by the 3 s window remain; new data fills the window
    for (int i = 0; i < 10; ++i) {
        hub.updatePower(i < 5 ? 100 : 300);
        clock += 1000;
    }
    hub.refreshDisplayPower();
    QCOMPARE(hub.displayPower(), 200);
    hub.setPowerSmoothingSeconds(0);
    QCOMPARE(hub.powerSmoothingSeconds(), 1);
    hub.setPowerSmoothingSeconds(99);
    QCOMPARE(hub.powerSmoothingSeconds(), 30);
}

void TstPowerSmoothing::hubTrainerStops()
{
    SensorHub hub;
    qint64 clock = 0;
    hub.setClockForTesting([&clock] { return clock; });
    hub.setTrainerConnected(true);
    hub.updatePower(250);
    clock = 1000;
    hub.updatePower(250);
    clock = 3000;
    hub.refreshDisplayPower();
    QCOMPARE(hub.displayPower(), 250);
    clock = 4500;                       // > 3 s without data
    hub.refreshDisplayPower();
    QCOMPARE(hub.displayPower(), 0);
}

void TstPowerSmoothing::hubDisconnectClears()
{
    SensorHub hub;
    qint64 clock = 0;
    hub.setClockForTesting([&clock] { return clock; });
    hub.setTrainerConnected(true);
    hub.updatePower(300);
    clock = 1000;
    hub.updatePower(300);
    hub.setTrainerConnected(false);
    QCOMPARE(hub.power(), 0);
    QCOMPARE(hub.displayPower(), 0);
}

void TstPowerSmoothing::hubRawPowerUnchanged()
{
    // the raw power (recording, ERG, statistics) never sees the average
    SensorHub hub;
    qint64 clock = 0;
    hub.setClockForTesting([&clock] { return clock; });
    QSignalSpy raw(&hub, &SensorHub::powerChanged);
    hub.updatePower(100);
    clock = 1000;
    hub.updatePower(500);
    QCOMPARE(hub.power(), 500);
    QCOMPARE(raw.count(), 2);
    QVERIFY(hub.displayPower() < 500);
}

QTEST_GUILESS_MAIN(TstPowerSmoothing)
#include "tst_powersmoothing.moc"
