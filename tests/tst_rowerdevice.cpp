#include "Database.h"
#include "DeviceManager.h"
#include "FtmsProtocol.h"
#include "SensorHub.h"
#include "Pm5Protocol.h"
#include "SimulatedDevices.h"

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QSignalSpy>
#include <QTemporaryDir>
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

// As reported by Qt's BlueZ backend: address-based, LE (no Class property).
static QBluetoothDeviceInfo bluezDevice(const QString &address, const QString &name, const QList<QBluetoothUuid> &uuids,
                                        int rssi = -60)
{
    QBluetoothDeviceInfo info(QBluetoothAddress(address), name, 0);
    info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    info.setServiceUuids(uuids);
    info.setRssi(qint16(rssi));
    return info;
}

// DeviceManager without Bluetooth: initialize() is not called, so no adapter
// is opened and no simulated entries are added; scan results are fed through
// handleDeviceInfo().
struct ManagerFixture
{
    QTemporaryDir dir;
    Database db;
    SensorHub hub;
    DeviceManager mgr { &db, &hub };
    ManagerFixture() { db.open(dir.filePath(QStringLiteral("t.db"))); }
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

    void classifyPm5Names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("rower");
        QTest::newRow("plain") << "PM5 430123456" << true;
        QTest::newRow("row suffix") << "PM5 430123456 Row" << true;
        QTest::newRow("lower case") << "pm5 1234" << true;
        QTest::newRow("concept2") << "Concept2 PM5" << true;
        QTest::newRow("concept2 lower") << "concept2 rower" << true;
        QTest::newRow("padded") << "  PM5 430123456 Row  " << true;
        QTest::newRow("ski") << "PM5 430123456 Ski" << false;
        QTest::newRow("bike") << "PM5 430123456 Bike" << false;
        QTest::newRow("skierg") << "PM5 SkiErg" << false;
        QTest::newRow("bikeerg") << "PM5 BikeErg" << false;
        QTest::newRow("other") << "Galaxy Watch" << false;
        QTest::newRow("empty") << "" << false;
        // "Ski"/"Bike" inside the name must not exclude a rower
        QTest::newRow("skip word") << "PM5 Skipper Row" << true;
        QTest::newRow("bikeshed") << "PM5 Bikeshed" << true;
    }

    void classifyPm5Names()
    {
        QFETCH(QString, name);
        QFETCH(bool, rower);
        const auto expected = rower ? DeviceManager::Kind::Rower : DeviceManager::Kind::Unknown;
        QCOMPARE(DeviceManager::classifyName(name), expected);
        QCOMPARE(DeviceManager::classify(bluezDevice(QStringLiteral("D4:36:39:AA:BB:01"), name, {})), expected);
    }

    void classifyPm5Uuids()
    {
        using K = DeviceManager::Kind;
        // The PM5 advertises its discovery service CE060000-...; base must accept 0x0000.
        const QBluetoothUuid discovery(QUuid(QStringLiteral("{CE060000-43E5-11E4-916C-0800200C9A66}")));
        const QBluetoothUuid rowing(QUuid(QStringLiteral("{ce060030-43e5-11e4-916c-0800200c9a66}")));
        QCOMPARE(pm5::shortId(QUuid(discovery)), std::optional<quint16>(0x0000));
        QCOMPARE(pm5::shortId(QUuid(rowing)), std::optional<quint16>(0x0030));
        QVERIFY(!pm5::shortId(QUuid(QStringLiteral("{CE060030-43E5-11E4-916C-0800200C9A67}"))).has_value());
        QCOMPARE(DeviceManager::classify(bluezDevice(QStringLiteral("D4:36:39:AA:BB:02"), QString(), { discovery })), K::Rower);
        QCOMPARE(DeviceManager::classify(bluezDevice(QStringLiteral("D4:36:39:AA:BB:03"), QString(), { rowing })), K::Rower);
        // BlueZ alias placeholder (address) is not a name
        QCOMPARE(DeviceManager::classify(bluezDevice(QStringLiteral("D4:36:39:AA:BB:04"), QStringLiteral("D4-36-39-AA-BB-04"),
                                                     { discovery })), K::Rower);
        // 16-bit-only data (e.g. GAP/GATT, battery) is no rower
        QCOMPARE(DeviceManager::classify(bluezDevice(QStringLiteral("D4:36:39:AA:BB:05"), QString(),
                                                     { QBluetoothUuid(quint16(0x180F)) })), K::Unknown);
        // also without a core configuration flag (cached BlueZ objects)
        QBluetoothDeviceInfo noCore(QBluetoothAddress(QStringLiteral("D4:36:39:AA:BB:06")), QStringLiteral("PM5 1"), 0);
        QCOMPARE(DeviceManager::classify(noCore), K::Rower);
    }

    void effectiveNameIgnoresBluezAlias()
    {
        QCOMPARE(DeviceManager::effectiveName(bluezDevice(QStringLiteral("D4:36:39:AA:BB:07"), QStringLiteral("D4-36-39-AA-BB-07"), {})),
                 QString());
        QCOMPARE(DeviceManager::effectiveName(bluezDevice(QStringLiteral("D4:36:39:AA:BB:07"), QStringLiteral(" PM5 430123456 "), {})),
                 QStringLiteral("PM5 430123456"));
    }

    void reclassifiesWhenNameArrives()
    {
        ManagerFixture f;
        const QString addr = QStringLiteral("D4:36:39:AA:BB:10");
        // first report: no scan response yet -> BlueZ alias, no services
        f.mgr.handleDeviceInfo(bluezDevice(addr, QStringLiteral("D4-36-39-AA-BB-10"), {}), false);
        QCOMPARE(f.mgr.rowers()->rowCount(), 0);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 1);
        // later report (deviceUpdated / re-discovery) carries the name
        f.mgr.handleDeviceInfo(bluezDevice(addr, QStringLiteral("PM5 430123456 Row"), {}, -55), true);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 0);
        QCOMPARE(f.mgr.rowers()->find(addr)->name, QStringLiteral("PM5 430123456 Row"));
        // an RSSI-only update with the stale alias keeps the name and the class
        f.mgr.handleDeviceInfo(bluezDevice(addr, QStringLiteral("D4-36-39-AA-BB-10"), {}, -70), true);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        QCOMPARE(f.mgr.rowers()->find(addr)->name, QStringLiteral("PM5 430123456 Row"));
        QCOMPARE(f.mgr.rowers()->find(addr)->rssi, -70);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 0);
    }

    void reclassifiesWhenServiceArrives()
    {
        ManagerFixture f;
        const QString addr = QStringLiteral("D4:36:39:AA:BB:11");
        f.mgr.handleDeviceInfo(bluezDevice(addr, QString(), {}), false);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 1);
        f.mgr.handleDeviceInfo(bluezDevice(addr, QString(), { QBluetoothUuid(pm5::uuid(0x0000)) }), true);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 0);
        // a later report without services keeps the merged UUIDs
        f.mgr.handleDeviceInfo(bluezDevice(addr, QString(), {}), true);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
    }

    void skiErgStaysOther()
    {
        ManagerFixture f;
        f.mgr.handleDeviceInfo(bluezDevice(QStringLiteral("D4:36:39:AA:BB:12"), QStringLiteral("PM5 430123456 Ski"),
                                           { QBluetoothUuid(pm5::uuid(0x0000)) }), false);
        QCOMPARE(f.mgr.rowers()->rowCount(), 0);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 1);
        f.mgr.handleDeviceInfo(bluezDevice(QStringLiteral("C0:00:00:00:00:01"), QStringLiteral("KICKR CORE"),
                                           { QBluetoothUuid(ftms::kServiceUuid) }), false);
        QCOMPARE(f.mgr.trainers()->rowCount(), 1);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 1);
    }

    void diagnosticsAndErrors()
    {
        ManagerFixture f;
        f.mgr.handleDeviceInfo(bluezDevice(QStringLiteral("D4:36:39:AA:BB:13"), QStringLiteral("PM5 430123456"),
                                           { QBluetoothUuid(pm5::uuid(0x0000)) }), false);
        const QString report = f.mgr.diagnosticsReport();
        QVERIFY(report.contains(QStringLiteral("PM5 430123456")));
        QVERIFY(report.contains(QStringLiteral("class: Rower")));
        QVERIFY(report.contains(QStringLiteral("CE060000-43E5-11E4-916C-0800200C9A66")));
        QVERIFY(report.contains(QStringLiteral("Qt: ")));

        QVERIFY(DeviceManager::scanErrorText(int(QBluetoothDeviceDiscoveryAgent::PoweredOffError), {})
                    .contains(QStringLiteral("bluetoothctl power on")));
        QVERIFY(!DeviceManager::scanErrorText(int(QBluetoothDeviceDiscoveryAgent::InputOutputError), QStringLiteral("x")).isEmpty());
        QVERIFY(!DeviceManager::scanErrorText(int(QBluetoothDeviceDiscoveryAgent::UnsupportedDiscoveryMethod), {}).isEmpty());
        QVERIFY(!DeviceManager::scanErrorText(int(QBluetoothDeviceDiscoveryAgent::MissingPermissionsError), {}).isEmpty());
        QVERIFY(DeviceManager::scanErrorText(int(QBluetoothDeviceDiscoveryAgent::NoError), {}).isEmpty());

        QVERIFY(f.mgr.scanError().isEmpty());
        f.mgr.connectAsRower(QStringLiteral("00:00:00:00:00:00"));   // unknown device
        QVERIFY(!f.mgr.scanError().isEmpty());
        f.mgr.clearScanError();
        QVERIFY(f.mgr.scanError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRowerDevice)
#include "tst_rowerdevice.moc"
