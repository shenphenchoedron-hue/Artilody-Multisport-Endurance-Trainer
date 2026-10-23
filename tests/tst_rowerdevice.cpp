#include "DeviceManager.h"
#include "FtmsProtocol.h"
#include "Pm5Protocol.h"
#include "SimulatedDevices.h"

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
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
    // general status as the PM5 sends it: state first, then counters
    void general(int ws, double t, double d, int totalWork)
    {
        applyWorkoutState(ws, true);
        applyTotalWorkDistance(totalWork);
        applyElapsed(t);
        applyDistance(d);
        commitMeasurement();
    }
    void hr(int bpm) { applyHeartRate(bpm); commitMeasurement(); }
    void rate(int spm) { applyStrokeRate(spm); commitMeasurement(); }
    void applyBatch(double t, double d, int strokeCount, int watts)
    {
        applyElapsed(t);
        applyDistance(d);
        applyStroke(strokeCount, watts);
        commitMeasurement();
    }
};

static QBluetoothDeviceInfo leDevice(const QString &name, const QList<QBluetoothUuid> &uuids)
{
    QBluetoothDeviceInfo info(QBluetoothUuid(QUuid::createUuid()), name, 0);
    info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    info.setServiceUuids(uuids);
    return info;
}

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

    void hasDataAndValuesBeforeSignals()
    {
        FakeRower r;
        r.connectDevice();
        QVERIFY(!r.hasData());
        double distAtStroke = -1;
        bool dataAtStroke = false;
        connect(&r, &RowerDevice::strokeMeasured, &r, [&] {
            distAtStroke = r.distanceMeters();
            dataAtStroke = r.hasData();
        });
        r.disconnectDevice();   // clearing live values is no measurement
        QVERIFY(!r.hasData());
        r.connectDevice();
        // one batch: distance and a stroke; handlers see the complete values
        r.applyBatch(12.0, 340.0, 3, 180);
        QVERIFY(r.hasData());
        QVERIFY(dataAtStroke);
        QCOMPARE(distAtStroke, 340.0);
    }

    void intervalRestartIsNoReset()
    {
        FakeRower r;
        r.connectDevice();
        QSignalSpy reset(&r, &RowerDevice::workoutReset);
        // interval workout: elapsed/distance restart per interval, total work grows
        r.general(int(pm5::WorkoutState::IntervalWorkDistance), 100, 500, 0);
        r.general(int(pm5::WorkoutState::IntervalWorkDistanceToRest), 101, 500, 500);
        r.general(int(pm5::WorkoutState::IntervalRest), 1, 0, 500);
        r.general(int(pm5::WorkoutState::IntervalRestEndToWorkDistance), 60, 0, 500);
        r.general(int(pm5::WorkoutState::IntervalWorkDistance), 0.5, 2, 500);
        QCOMPARE(reset.count(), 0);
        // total work distance drops: a new workout even in an interval state
        r.general(int(pm5::WorkoutState::IntervalWorkDistance), 0.5, 1, 0);
        QCOMPARE(reset.count(), 1);
        // a plain row: a jump backwards is a new workout
        r.general(int(pm5::WorkoutState::WorkoutRow), 300, 1200, 0);
        r.general(int(pm5::WorkoutState::WaitToBegin), 0, 0, 0);
        QCOMPARE(reset.count(), 2);
    }

    void firstStrokeBeforeActiveKeepsPower()
    {
        FakeRower r;
        r.connectDevice();
        r.status(int(pm5::WorkoutState::WaitToBegin), false);
        r.stroke(1, 210);              // stroke data arrives before general status says "active"
        r.status(int(pm5::WorkoutState::WaitToBegin), false);
        QCOMPARE(r.watts(), 210);      // only active -> inactive drops it
        r.status(int(pm5::WorkoutState::WorkoutRow), true);
        QCOMPARE(r.watts(), 210);
        r.status(int(pm5::WorkoutState::WorkoutRow), false);
        QCOMPARE(r.watts(), 0);
    }

    void powerHoldFollowsStrokeRate()
    {
        FakeRower r;
        r.connectDevice();
        QCOMPARE(r.effectivePowerHoldMs(), 5000);
        r.rate(16);                    // 3.75 s per stroke -> hold 5.625 s
        QCOMPARE(r.effectivePowerHoldMs(), 5625);
        r.stroke(1, 150);
        r.checkPowerHold(5500);
        QCOMPARE(r.watts(), 150);
        r.checkPowerHold(5700);
        QCOMPARE(r.watts(), 0);
        r.rate(30);
        QCOMPARE(r.effectivePowerHoldMs(), 5000);
    }

    void strokeAfterResetWithSameCountIsNew()
    {
        FakeRower r;
        r.connectDevice();
        QSignalSpy strokes(&r, &RowerDevice::strokeMeasured);
        r.progress(100, 400);
        r.stroke(5, 200);
        QCOMPARE(strokes.count(), 1);
        r.progress(0.5, 1);            // new workout on the monitor (separate packet)
        r.stroke(0, 0);                // count 0: no stroke yet
        QCOMPARE(strokes.count(), 1);
        r.stroke(1, 120);
        QCOMPARE(strokes.count(), 2);
        r.progress(30, 100);
        r.progress(0.5, 2);            // another reset; stroke count of the old workout == 1
        r.stroke(1, 130);              // first stroke of the new workout, same count
        QCOMPARE(strokes.count(), 3);
        QCOMPARE(r.watts(), 130);
    }

    void classifyDevices()
    {
        using K = DeviceManager::Kind;
        const QBluetoothUuid rowing(pm5::uuid(pm5::kRowingService));
        const QBluetoothUuid info(pm5::uuid(pm5::kDeviceInfoService));
        const QBluetoothUuid ftms(ftms::kServiceUuid);
        QCOMPARE(DeviceManager::classify(leDevice(QStringLiteral("Something"), { rowing })), K::Rower);
        // any Concept2 CE06xxxx service identifies a PM5
        QCOMPARE(DeviceManager::classify(leDevice(QString(), { info })), K::Rower);
        QCOMPARE(DeviceManager::classify(leDevice(QStringLiteral("PM5 430012345 Row"), { ftms })), K::Rower);
        QCOMPARE(DeviceManager::classify(leDevice(QStringLiteral("PM5 430012345"), {})), K::Rower);
        // SkiErg / BikeErg monitors are not offered (also not as FTMS trainer)
        QCOMPARE(DeviceManager::classify(leDevice(QStringLiteral("PM5 430012345 Ski"), { rowing })), K::Unknown);
        QCOMPARE(DeviceManager::classify(leDevice(QStringLiteral("PM5 430012345 Bike"), { ftms })), K::Unknown);
        QCOMPARE(DeviceManager::classify(leDevice(QStringLiteral("KICKR CORE"), { ftms })), K::Trainer);
    }
};

QTEST_GUILESS_MAIN(TestRowerDevice)
#include "tst_rowerdevice.moc"
