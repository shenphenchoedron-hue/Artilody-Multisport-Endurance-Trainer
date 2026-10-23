#include "SimulatedDevices.h"

#include <QSignalSpy>
#include <QtTest>

using namespace watt;

// Exposes the protected feed API of RowerDevice.
class FakeRower : public RowerDevice
{
public:
    QString id() const override { return QStringLiteral("fake"); }
    QString name() const override { return QStringLiteral("Fake"); }
    QString protocol() const override { return QStringLiteral("test"); }
    void connectDevice() override { setState(LinkState::Connected); }
    void disconnectDevice() override { setState(LinkState::Disconnected); }

    void stroke(int count, int watts) { applyStroke(count, watts); commitMeasurement(); }
    void status(int ws, bool active) { applyWorkoutState(ws, active); commitMeasurement(); }
    void progress(double t, double d) { applyElapsed(t); applyDistance(d); commitMeasurement(); }
    void hr(int bpm) { applyHeartRate(bpm); commitMeasurement(); }
};

class TestRowerDevice : public QObject
{
    Q_OBJECT
private slots:
    void holdsStrokePowerThenDecays()
    {
        FakeRower r;
        r.connectDevice();
        QSignalSpy strokes(&r, &RowerDevice::strokeMeasured);
        QSignalSpy power(&r, &RowerDevice::powerMeasured);
        r.status(1, true);
        r.stroke(1, 180);
        QCOMPARE(r.watts(), 180);
        QCOMPARE(strokes.count(), 1);
        QCOMPARE(power.count(), 1);
        r.stroke(1, 999);   // repeated packet for the same stroke is ignored
        QCOMPARE(r.watts(), 180);
        QCOMPARE(strokes.count(), 1);
        r.checkPowerHold(4000);
        QCOMPARE(r.watts(), 180);
        r.stroke(2, 195);
        QCOMPARE(r.watts(), 195);
        QCOMPARE(r.strokeCount(), 2);
        r.checkPowerHold(5000);
        QCOMPARE(r.watts(), 0);
        QCOMPARE(power.last().at(0).toInt(), 0);
    }

    void inactiveRowingZeroesPower()
    {
        FakeRower r;
        r.connectDevice();
        QSignalSpy active(&r, &RowerDevice::rowingActiveChanged);
        r.status(1, true);
        r.stroke(5, 200);
        r.status(1, false);
        QCOMPARE(r.watts(), 0);
        QVERIFY(!r.rowingActive());
        QCOMPARE(active.count(), 2);
    }

    void workoutResetAndDisconnectKeepsTotals()
    {
        FakeRower r;
        r.connectDevice();
        QSignalSpy reset(&r, &RowerDevice::workoutReset);
        r.progress(100, 400);
        r.progress(100.2, 400.5);
        QCOMPARE(reset.count(), 0);
        r.progress(0.5, 1);
        QCOMPARE(reset.count(), 1);
        r.progress(30, 120);
        r.stroke(9, 150);
        r.disconnectDevice();
        QVERIFY(!r.connected());
        QCOMPARE(r.watts(), 0);
        QCOMPARE(r.distanceMeters(), 120.0);
        QCOMPARE(r.strokeCount(), 9);
    }

    void heartRateOnlyWhenValid()
    {
        FakeRower r;
        QSignalSpy hr(&r, &RowerDevice::heartRateMeasured);
        r.hr(0);
        QCOMPARE(hr.count(), 0);
        r.hr(130);
        QCOMPARE(hr.count(), 1);
        QCOMPARE(r.heartRateBpm(), 130);
    }

    void simulatedRowerRows()
    {
        SimulatedRower r;
        QCOMPARE(r.id(), QStringLiteral("sim:rower"));
        QSignalSpy strokes(&r, &RowerDevice::strokeMeasured);
        for (int i = 0; i < 4 * 60; ++i)   // one simulated minute
            r.step(0.25);
        QVERIFY2(strokes.count() >= 20 && strokes.count() <= 30, qPrintable(QString::number(strokes.count())));
        QVERIFY(r.watts() >= 120 && r.watts() <= 240);
        QVERIFY(r.strokesPerMinute() >= 20 && r.strokesPerMinute() <= 30);
        QVERIFY(r.distanceMeters() > 150 && r.distanceMeters() < 350);
        QVERIFY(r.pace500Seconds() > 95 && r.pace500Seconds() < 140);
        QVERIFY(r.rowingActive());
        QCOMPARE(r.elapsedSeconds(), 60.0);

        r.setTargetPower(250);
        for (int i = 0; i < 4 * 20; ++i)
            r.step(0.25);
        QVERIFY(r.watts() > 215 && r.watts() < 285);

        r.setRowing(false);
        for (int i = 0; i < 4 * 6; ++i)
            r.step(0.25);
        QVERIFY(!r.rowingActive());
        QCOMPARE(r.watts(), 0);
        QCOMPARE(r.strokesPerMinute(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRowerDevice)
#include "tst_rowerdevice.moc"
