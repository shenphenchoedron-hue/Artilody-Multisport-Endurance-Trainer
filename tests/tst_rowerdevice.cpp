#include "Database.h"
#include "DeviceManager.h"
#include "FtmsProtocol.h"
#include "SensorHub.h"
#include "BleTrainer.h"
#include "Pm5Protocol.h"
#include "Pm5Rower.h"
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
    void strokeWithRate(int count, int watts, int spm)
    {
        applyStroke(count, watts);
        applyStrokeRate(spm);
        commitMeasurement();
    }
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

static QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray b;
    for (int v : values)
        b.append(char(quint8(v)));
    return b;
}

// FTMS advertisement: 0x1826 service data = flags + machine type (LE).
static QBluetoothDeviceInfo ftmsDevice(const QString &address, const QString &name, quint16 machineType,
                                       bool listUuid = true)
{
    QBluetoothDeviceInfo info = bluezDevice(address, name, listUuid ? QList<QBluetoothUuid> { QBluetoothUuid(ftms::kServiceUuid) }
                                                                     : QList<QBluetoothUuid> {});
    info.setServiceData(QBluetoothUuid(ftms::kServiceUuid),
                        bytes({ 0x01, machineType & 0xFF, (machineType >> 8) & 0xFF }));
    return info;
}

static QByteArray le(quint32 v, int bytes)
{
    QByteArray b;
    for (int i = 0; i < bytes; ++i)
        b.append(char((v >> (8 * i)) & 0xFF));
    return b;
}

// 0x0031 general status (19 bytes): rowing active, workout row
static QByteArray pmGeneral(double t, double d, bool active = true)
{
    QByteArray b = le(quint32(std::lround(t * 100)), 3) + le(quint32(std::lround(d * 10)), 3);
    b += bytes({ 0, 0, active ? 1 : 0, active ? 1 : 0, 1 });
    b += le(0, 3) + le(0, 3) + bytes({ 0, 120 });
    return b;
}

// 0x0032 additional status 1: 17 bytes (direct) or 19 (multiplexed / current firmware)
static QByteArray pmStatus1(double t, int spm, int size = 17)
{
    QByteArray b = le(quint32(std::lround(t * 100)), 3) + le(4250, 2);
    b += bytes({ spm, 255 });             // spm, hr invalid
    b += le(11765, 2) + le(12010, 2);     // pace, avg pace
    b += le(0, 2) + le(0, 3);             // rest distance/time
    if (size >= 18)
        b += le(187, 2);                  // average power
    b += bytes({ 0 });                    // machine type
    return b.left(size);
}

// 0x0036 additional stroke data (15 bytes)
static QByteArray pmStroke(double t, int count, int watts)
{
    return le(quint32(std::lround(t * 100)), 3) + le(quint32(watts), 2) + le(1000, 2) + le(quint32(count), 2) + le(0, 3) + le(0, 3);
}

static QBluetoothUuid pmChar(quint16 id) { return QBluetoothUuid(pm5::uuid(id)); }

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

    // --- FTMS advertisement classification -----------------------------------

    void classifyFtmsServiceData()
    {
        using K = DeviceManager::Kind;
        // PM5 advertising only FTMS with the Rower bit, no name yet
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:01"), QString(), 0x0010)), K::Rower);
        // service data without the UUID in the service list (BlueZ ServiceData only)
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:02"), QString(), 0x0010, false)), K::Rower);
        // indoor bike
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:03"), QStringLiteral("KICKR"), 0x0020)), K::Trainer);
        // rower + bike bits: ambiguous, stays a trainer
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:04"), QString(), 0x0030)), K::Trainer);
        // treadmill only: a treadmill (read only, Treadmill Run)
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:05"), QString(), 0x0001)), K::Treadmill);
        // cross trainer only: not offered
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:08"), QString(), 0x0002)), K::Unknown);
        // type 0 (not filled in): falls back to the UUID -> trainer
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:06"), QString(), 0x0000)), K::Trainer);
        // malformed service data: falls back to the UUID -> trainer
        QBluetoothDeviceInfo bad = bluezDevice(QStringLiteral("D4:36:39:AA:CC:07"), QString(), { QBluetoothUuid(ftms::kServiceUuid) });
        bad.setServiceData(QBluetoothUuid(ftms::kServiceUuid), bytes({ 0x01 }));
        QCOMPARE(DeviceManager::classify(bad), K::Trainer);
        // the PM5 name wins over bike bits (FTMS present)
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:08"), QStringLiteral("PM5 430123456 Row"), 0x0020)), K::Rower);
        // a SkiErg PM5 with the rower bit is still not a rower
        QCOMPARE(DeviceManager::classify(ftmsDevice(QStringLiteral("D4:36:39:AA:CC:09"), QStringLiteral("PM5 430123456 Ski"), 0x0010)), K::Unknown);
    }

    void ftmsRowerListedUnderRowers()
    {
        ManagerFixture f;
        const QString addr = QStringLiteral("D4:36:39:AA:CC:10");
        // first report: FTMS UUID only -> trainer
        f.mgr.handleDeviceInfo(bluezDevice(addr, QString(), { QBluetoothUuid(ftms::kServiceUuid) }), false);
        QCOMPARE(f.mgr.trainers()->rowCount(), 1);
        // service data arrives -> moves to the rowers
        f.mgr.handleDeviceInfo(ftmsDevice(addr, QString(), 0x0010), true);
        QCOMPARE(f.mgr.trainers()->rowCount(), 0);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        QCOMPARE(f.mgr.rowers()->find(addr)->protocol, QStringLiteral("Bluetooth FTMS rower"));
        // a later report without service data keeps the merged value
        f.mgr.handleDeviceInfo(bluezDevice(addr, QString(), {}), true);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        // diagnostics show the parsed advertisement
        const QString report = f.mgr.diagnosticsReport();
        QVERIFY2(report.contains(QStringLiteral("FTMS advertisement: machine available, type 0x0010 (Rower) -> rower")), qPrintable(report));
        QVERIFY(report.contains(QStringLiteral("service data: 0x1826=01 10 00")));
    }

    void trainerMovedToRowers()
    {
        ManagerFixture f;
        const QString addr = QStringLiteral("D4:36:39:AA:CC:11");
        f.mgr.handleDeviceInfo(bluezDevice(addr, QStringLiteral("Unknown FTMS"), { QBluetoothUuid(ftms::kServiceUuid) }), false);
        QCOMPARE(f.mgr.trainers()->rowCount(), 1);
        f.db.setSetting(QStringLiteral("ble/knownTrainer"), addr);
        f.mgr.moveToRowers(addr);
        QCOMPARE(f.mgr.trainers()->rowCount(), 0);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        QCOMPARE(f.db.setting(QStringLiteral("ble/rowerIds")).toString(), addr);
        // further advertisements keep it among the rowers
        f.mgr.handleDeviceInfo(bluezDevice(addr, QStringLiteral("Unknown FTMS"), { QBluetoothUuid(ftms::kServiceUuid) }, -50), true);
        QCOMPARE(f.mgr.trainers()->rowCount(), 0);
        QCOMPARE(f.mgr.rowers()->rowCount(), 1);
        QVERIFY(f.mgr.diagnosticsReport().contains(QStringLiteral("remembered rower")));
    }

    void trainerRowerDetection()
    {
        const QBluetoothUuid rowing(pm5::uuid(pm5::kRowingService));
        const QBluetoothUuid ftmsSvc(ftms::kServiceUuid);
        QVERIFY(BleTrainer::looksLikeRower({ ftmsSvc }, true, false));       // FTMS Rower Data only
        QVERIFY(!BleTrainer::looksLikeRower({ ftmsSvc }, true, true));       // also Indoor Bike Data
        QVERIFY(!BleTrainer::looksLikeRower({ ftmsSvc }, false, true));      // a trainer
        QVERIFY(BleTrainer::looksLikeRower({ ftmsSvc, rowing }, false, true));   // Concept2 rowing service
        QVERIFY(BleTrainer::looksLikeRower({ rowing }, false, false));
        QVERIFY(!BleTrainer::looksLikeRower({}, false, false));
    }

    // --- Pm5Rower data paths ---------------------------------------------------

    void pm5RowerFtmsRowerData()
    {
        QBluetoothDeviceInfo info = ftmsDevice(QStringLiteral("D4:36:39:AA:CC:20"), QStringLiteral("PM5 430123456"), 0x0010);
        Pm5Rower r(info);
        QSignalSpy strokes(&r, &RowerDevice::strokeMeasured);
        QSignalSpy active(&r, &RowerDevice::rowingActiveChanged);
        const QBluetoothUuid rowerData(ftms::kRowerDataUuid);
        // split record: More Data packet first, then stroke rate/count
        r.processNotification(rowerData, bytes({ 0x2D, 0x08, 0x88, 0x13, 0x00, 0x78, 0x00, 0xCB, 0x00, 0x2C, 0x01 }));
        QVERIFY(!r.hasData());
        r.processNotification(rowerData, bytes({ 0x00, 0x01, 48, 0x10, 0x00, 0x2A, 0x00, 0x20, 0x03, 0x0E }));
        QVERIFY(r.hasData());
        QCOMPARE(r.distanceMeters(), 5000.0);
        QCOMPARE(r.pace500Seconds(), 120.0);
        QCOMPARE(r.speedMetersPerSecond(), 500.0 / 120.0);
        QCOMPARE(r.elapsedSeconds(), 300.0);
        QCOMPARE(r.strokesPerMinute(), 24);
        QCOMPARE(r.strokeCount(), 16);
        QCOMPARE(r.watts(), 203);
        QCOMPARE(r.calories(), 42);
        QVERIFY(r.rowingActive());
        QCOMPARE(strokes.count(), 1);
        QCOMPARE(active.count(), 1);
        // single-packet record without power: power from pace
        r.processNotification(rowerData, bytes({ 0x08, 0x00, 48, 0x11, 0x00, 0x78, 0x00 }));
        QCOMPARE(r.strokeCount(), 17);
        QCOMPARE(r.watts(), 203);   // 2.80 / (120/500)^3 = 202.5
        // stroke rate 0, power 0: rowing stopped
        r.processNotification(rowerData, bytes({ 0x20, 0x00, 0, 0x11, 0x00, 0x00, 0x00 }));
        QVERIFY(!r.rowingActive());
        QCOMPARE(r.watts(), 0);
    }

    void pm5RowerLenientConcept2Packets()
    {
        Pm5Rower r(bluezDevice(QStringLiteral("D4:36:39:AA:CC:21"), QStringLiteral("PM5 430123456"), {}));
        // direct 0x0032 with 19 bytes (current firmware): average power at 16-17
        QByteArray s1;
        s1 += bytes({ 0x70, 0x17, 0x00 });   // 60.00 s
        s1 += bytes({ 0x9A, 0x10 });         // 4.250 m/s
        s1 += bytes({ 24, 142 });            // spm, hr
        s1 += bytes({ 0xF5, 0x2D });         // 117.65 s
        s1 += bytes({ 0xEA, 0x2E });         // 120.10 s
        s1 += bytes({ 0x2C, 0x01 });         // rest distance 300 m
        s1 += bytes({ 0x94, 0x11, 0x00 });   // rest time 45.00 s
        s1 += bytes({ 0xBB, 0x00 });         // avg power 187
        s1 += bytes({ 0x00 });               // machine type
        QCOMPARE(s1.size(), 19);
        r.processNotification(QBluetoothUuid(pm5::uuid(pm5::kAdditionalStatus1)), s1);
        QVERIFY(r.hasData());
        QCOMPARE(r.averageWatts(), 187);
        QCOMPARE(r.strokesPerMinute(), 24);
        QCOMPARE(r.heartRateBpm(), 142);
        // direct 0x0036 with 17 bytes (work per stroke appended)
        QByteArray st;
        st += bytes({ 0x58, 0x1B, 0x00, 0xD4, 0x00, 0x4C, 0x04, 0x39, 0x00, 0x08, 0x07, 0x00, 0x64, 0x19, 0x00, 0xD7, 0x11 });
        r.processNotification(QBluetoothUuid(pm5::uuid(pm5::kAdditionalStrokeData)), st);
        QCOMPARE(r.strokeCount(), 57);
        QCOMPARE(r.watts(), 212);
        QCOMPARE(r.protocol(), QStringLiteral("Concept2 PM5 Bluetooth"));
    }

    // --- stroke rate (1.1.4) ----------------------------------------------------

    void concept2DirectStatus1StrokeRate_data()
    {
        QTest::addColumn<int>("size");
        QTest::newRow("17 bytes") << 17;
        QTest::newRow("19 bytes") << 19;
    }

    void concept2DirectStatus1StrokeRate()
    {
        QFETCH(int, size);
        Pm5Rower r(bluezDevice(QStringLiteral("D4:36:39:AA:DD:01"), QStringLiteral("PM5 430123456"), {}));
        r.processNotification(pmChar(pm5::kAdditionalStatus1), pmStatus1(10, 27, size));
        QCOMPARE(r.strokesPerMinute(), 27);
        QCOMPARE(r.deviceStrokesPerMinute(), 27);
        QVERIFY(!r.strokeRateEstimated());
        QCOMPARE(r.lastDecodedStrokeRate(), 27);
        QCOMPARE(r.channels().value(QStringLiteral("0x0032")).packets, 1);
    }

    void concept2MultiplexedStrokeRate_data()
    {
        QTest::addColumn<int>("size");
        QTest::newRow("19 bytes (documented)") << 19;
        QTest::newRow("18 bytes") << 18;
        QTest::newRow("17 bytes (short firmware)") << 17;
    }

    void concept2MultiplexedStrokeRate()
    {
        QFETCH(int, size);
        Pm5Rower r(bluezDevice(QStringLiteral("D4:36:39:AA:DD:02"), QStringLiteral("PM5 430123456"), {}));
        r.processNotification(pmChar(pm5::kMultiplexed), QByteArray(1, char(0x32)) + pmStatus1(10, 22, size));
        QCOMPARE(r.strokesPerMinute(), 22);
        const auto ch = r.channels();
        QCOMPARE(ch.value(QStringLiteral("0x0080")).packets, 1);
        QCOMPARE(ch.value(QStringLiteral("0x0080/0x32")).packets, 1);
        QCOMPARE(ch.value(QStringLiteral("0x0080/0x32")).undecoded, 0);
        QVERIFY(r.dataSourceText().contains(QStringLiteral("0x0080 multiplex 0x32")));
    }

    void concept2SequenceKeepsStrokeRate()
    {
        Pm5Rower r(bluezDevice(QStringLiteral("D4:36:39:AA:DD:03"), QStringLiteral("PM5 430123456"), {}));
        qint64 now = 0;
        r.setClockForTesting([&now] { return now; });
        r.processNotification(pmChar(pm5::kGeneralStatus), pmGeneral(9.8, 40));
        r.processNotification(pmChar(pm5::kAdditionalStatus1), pmStatus1(10, 26));
        QCOMPARE(r.strokesPerMinute(), 26);
        now += 100;
        r.processNotification(pmChar(pm5::kAdditionalStrokeData), pmStroke(10.1, 5, 190));
        QCOMPARE(r.strokesPerMinute(), 26);
        QCOMPARE(r.watts(), 190);
        now += 100;
        r.processNotification(pmChar(pm5::kGeneralStatus), pmGeneral(10.2, 41));
        QCOMPARE(r.strokesPerMinute(), 26);
        // multiplexed copies of the other ids do not touch it either
        r.processNotification(pmChar(pm5::kMultiplexed), QByteArray(1, char(0x31)) + pmGeneral(10.3, 41.5));
        r.processNotification(pmChar(pm5::kMultiplexed), QByteArray(1, char(0x36)) + pmStroke(10.3, 5, 190) + le(0, 2));
        QCOMPARE(r.strokesPerMinute(), 26);
        // power hold decay leaves the rate alone
        r.checkPowerHold(10000);
        QCOMPARE(r.watts(), 0);
        QCOMPARE(r.strokesPerMinute(), 26);
        QVERIFY(r.dataSourceText().startsWith(QStringLiteral("Concept2 rowing service (direct 0x31 0x32 0x36")));
    }

    void ftmsSplitPacketsKeepStrokeRate()
    {
        Pm5Rower r(ftmsDevice(QStringLiteral("D4:36:39:AA:DD:04"), QStringLiteral("PM5 430123456"), 0x0010));
        const QBluetoothUuid rowerData(ftms::kRowerDataUuid);
        // bit0 = 1: distance + power, no stroke rate
        const QByteArray more = bytes({ 0x25, 0x00, 0xE8, 0x03, 0x00, 0xC8, 0x00 });   // 1000 m, 200 W
        // bit0 = 0: stroke rate 48 (0.5 /min) -> 24 spm, stroke count 30, elapsed 240 s
        const QByteArray rate = bytes({ 0x00, 0x08, 48, 30, 0x00, 0xF0, 0x00 });
        r.processNotification(rowerData, more);
        r.processNotification(rowerData, rate);
        QCOMPARE(r.strokesPerMinute(), 24);
        QCOMPARE(r.distanceMeters(), 1000.0);
        QCOMPARE(r.watts(), 200);
        QCOMPARE(r.elapsedSeconds(), 240.0);
        // the next record starts with packets without stroke rate: kept
        r.processNotification(rowerData, bytes({ 0x25, 0x00, 0xE9, 0x03, 0x00, 0xCA, 0x00 }));
        QCOMPARE(r.strokesPerMinute(), 24);
        r.processNotification(rowerData, bytes({ 0x25, 0x00, 0xEA, 0x03, 0x00, 0xCC, 0x00 }));   // overlapping: releases the first
        QCOMPARE(r.strokesPerMinute(), 24);
        QCOMPARE(r.distanceMeters(), 1001.0);
        // a packet with only stroke rate/count is applied on its own
        r.processNotification(rowerData, bytes({ 0x00, 0x00, 52, 31, 0x00 }));
        QCOMPARE(r.strokesPerMinute(), 26);
        QCOMPARE(r.strokeCount(), 31);
        QCOMPARE(r.lastDecodedStrokeRate(), 26);
        // average stroke rate (bit 1) is not taken as stroke rate
        r.processNotification(rowerData, bytes({ 0x03, 0x00, 80 }));
        QCOMPARE(r.strokesPerMinute(), 26);
        QCOMPARE(r.dataSourceText(), QStringLiteral("FTMS Rower Data (0x2AD1)"));
        QCOMPARE(r.channels().value(QStringLiteral("0x2AD1")).packets, 6);
    }

    void ftmsStrokeRateFirstThenMoreData()
    {
        // order the other way round: stroke rate packet first, More Data after it
        Pm5Rower r(ftmsDevice(QStringLiteral("D4:36:39:AA:DD:05"), QStringLiteral("PM5 430123456"), 0x0010));
        const QBluetoothUuid rowerData(ftms::kRowerDataUuid);
        r.processNotification(rowerData, bytes({ 0x00, 0x00, 48, 10, 0x00 }));
        QCOMPARE(r.strokesPerMinute(), 24);
        r.processNotification(rowerData, bytes({ 0x21, 0x00, 0xC8, 0x00 }));   // power only, More Data
        r.processNotification(rowerData, bytes({ 0x05, 0x00, 0xE8, 0x03, 0x00 }));
        QCOMPARE(r.strokesPerMinute(), 24);
        r.processNotification(rowerData, bytes({ 0x00, 0x00, 50, 11, 0x00 }));
        QCOMPARE(r.strokesPerMinute(), 25);
        QCOMPARE(r.distanceMeters(), 1000.0);
        QCOMPARE(r.watts(), 200);
    }

    void strokeRateFallbackFromStrokeTiming()
    {
        FakeRower r;
        qint64 now = 0;
        r.setClockForTesting([&now] { return now; });
        r.connectDevice();
        QSignalSpy changed(&r, &RowerDevice::measurementChanged);
        r.stroke(1, 180);
        QCOMPARE(r.strokesPerMinute(), 0);   // one stroke: no interval yet
        now += 2500;
        r.stroke(2, 180);
        QCOMPARE(r.strokesPerMinute(), 24);
        QVERIFY(r.strokeRateEstimated());
        QCOMPARE(r.strokeRateSource(), RowerDevice::StrokeRateSource::Strokes);
        // smoothed over the last 4 intervals: 2.5, 2.0, 2.0, 2.0 s -> 2.125 s -> 28
        for (int i = 3; i <= 5; ++i) {
            now += 2000;
            r.stroke(i, 180);
        }
        QCOMPARE(r.strokesPerMinute(), int(std::lround(60000.0 / ((2500 + 3 * 2000) / 4.0))));
        now += 2000;
        r.stroke(6, 180);   // window drops the 2.5 s interval
        QCOMPARE(r.strokesPerMinute(), 30);
        // a missed stroke packet (count jumps by 2) does not halve the rate
        now += 4000;
        r.stroke(8, 180);
        QCOMPARE(r.strokesPerMinute(), 30);
        // slowing down: after > 1.5 intervals the running gap bounds the rate
        now += 2500;
        r.checkStrokeRate();
        QCOMPARE(r.strokesPerMinute(), 30);
        now += 1500;   // 4 s since the last stroke
        r.checkStrokeRate();
        QCOMPARE(r.strokesPerMinute(), 15);
        now += 2001;   // > 6 s without a stroke
        r.checkStrokeRate();
        QCOMPARE(r.strokesPerMinute(), 0);
        QVERIFY(!r.strokeRateEstimated());
        QVERIFY(changed.count() > 0);
        // a new piece starts a fresh window
        now += 10000;
        r.stroke(9, 150);
        QCOMPARE(r.strokesPerMinute(), 0);
        now += 3000;
        r.stroke(10, 150);
        QCOMPARE(r.strokesPerMinute(), 20);
    }

    void strokeRateFallbackClamps()
    {
        FakeRower r;
        qint64 now = 0;
        r.setClockForTesting([&now] { return now; });
        r.connectDevice();
        for (int i = 1; i <= 5; ++i) {
            r.stroke(i, 100);
            now += 500;   // 120 spm: implausible for rowing
        }
        now -= 500;
        r.checkStrokeRate();
        QCOMPARE(r.strokesPerMinute(), RowerDevice::kMaxComputedSpm);
        // simultaneous packets (no time between strokes) give no rate
        FakeRower z;
        z.setClockForTesting([] { return qint64(1000); });
        z.stroke(1, 100);
        z.stroke(2, 100);
        QCOMPARE(z.strokesPerMinute(), 0);
    }

    void deviceStrokeRatePreferred()
    {
        FakeRower r;
        qint64 now = 0;
        r.setClockForTesting([&now] { return now; });
        r.connectDevice();
        // strokes every 2 s (30 spm computed), the monitor says 28: monitor wins
        for (int i = 1; i <= 4; ++i) {
            r.strokeWithRate(i, 200, 28);
            now += 2000;
        }
        QCOMPARE(r.strokesPerMinute(), 28);
        QCOMPARE(r.computedStrokesPerMinute(), 30);
        QVERIFY(!r.strokeRateEstimated());
        // the monitor stops reporting a rate (characteristic lost) while strokes go on
        for (int i = 5; i <= 8; ++i) {
            r.stroke(i, 200);
            now += 2000;
        }
        r.checkStrokeRate();
        QCOMPARE(r.strokesPerMinute(), 30);
        QVERIFY(r.strokeRateEstimated());
        // reporting again
        r.strokeWithRate(9, 200, 27);
        QCOMPARE(r.strokesPerMinute(), 27);
        // the monitor reports 0 right after a positive rate: rowing stopped -> 0
        now += 500;
        r.rate(0);
        QCOMPARE(r.strokesPerMinute(), 0);
        QCOMPARE(r.strokeRateSource(), RowerDevice::StrokeRateSource::Device);
    }

    void deviceReportingZeroUsesFallback()
    {
        // e.g. a firmware whose stroke rate field stays 0 while strokes come in
        FakeRower r;
        qint64 now = 0;
        r.setClockForTesting([&now] { return now; });
        r.connectDevice();
        for (int i = 1; i <= 4; ++i) {
            r.strokeWithRate(i, 200, 0);
            now += 2400;
        }
        now -= 2400;
        QCOMPARE(r.strokesPerMinute(), 25);
        QVERIFY(r.strokeRateEstimated());
        r.disconnectDevice();
        QCOMPARE(r.strokesPerMinute(), 0);
        QCOMPARE(r.computedStrokesPerMinute(), 0);
    }

    void pm5WithoutStatus1ComputesStrokeRate()
    {
        // 0x0032 never arrives (subscription lost): 0x31 + 0x36 only
        Pm5Rower r(bluezDevice(QStringLiteral("D4:36:39:AA:DD:06"), QStringLiteral("PM5 430123456"), {}));
        qint64 now = 0;
        r.setClockForTesting([&now] { return now; });
        for (int i = 1; i <= 5; ++i) {
            const double t = 10 + 2.0 * i;
            r.processNotification(pmChar(pm5::kGeneralStatus), pmGeneral(t, 8.0 * i));
            r.processNotification(pmChar(pm5::kAdditionalStrokeData), pmStroke(t, i, 200));
            now += 2000;
        }
        now -= 2000;
        QCOMPARE(r.strokesPerMinute(), 30);
        QVERIFY(r.strokeRateEstimated());
        QCOMPARE(r.lastDecodedStrokeRate(), -1);
        const QString diag = r.dataDiagnostics();
        QVERIFY2(diag.contains(QStringLiteral("Additional Status 1 (stroke rate) via: none received")), qPrintable(diag));
        QVERIFY2(diag.contains(QStringLiteral("computed from stroke timing")), qPrintable(diag));
        QVERIFY2(diag.contains(QStringLiteral("0x0036")), qPrintable(diag));
        QVERIFY2(diag.contains(QString::fromLatin1(pmStroke(20, 5, 200).toHex(' '))), qPrintable(diag));
        // the monitor's rate takes over as soon as 0x0032 arrives
        r.processNotification(pmChar(pm5::kAdditionalStatus1), pmStatus1(20.1, 29));
        QCOMPARE(r.strokesPerMinute(), 29);
        QVERIFY(!r.strokeRateEstimated());
        QVERIFY(r.dataDiagnostics().contains(QStringLiteral("via: 0x0032")));
    }

    void pm5DiagnosticsCountersAndRawHex()
    {
        Pm5Rower r(bluezDevice(QStringLiteral("D4:36:39:AA:DD:07"), QStringLiteral("PM5 430123456"), {}));
        const QByteArray s1 = pmStatus1(5, 21, 19);
        r.processNotification(pmChar(pm5::kAdditionalStatus1), s1);
        r.processNotification(pmChar(pm5::kAdditionalStatus1), s1);
        r.processNotification(pmChar(pm5::kAdditionalStatus1), s1.left(10));   // truncated: counted, not decoded
        r.processNotification(pmChar(pm5::kMultiplexed), QByteArray(1, char(0x35)) + QByteArray(18, '\0'));
        const auto ch = r.channels();
        QCOMPARE(ch.value(QStringLiteral("0x0032")).packets, 3);
        QCOMPARE(ch.value(QStringLiteral("0x0032")).undecoded, 1);
        QCOMPARE(ch.value(QStringLiteral("0x0032")).last, s1.left(10));
        QCOMPARE(ch.value(QStringLiteral("0x0080/0x35")).packets, 1);
        QCOMPARE(r.strokesPerMinute(), 21);
        const QString diag = r.dataDiagnostics();
        QVERIFY2(diag.contains(QStringLiteral("Last decoded stroke rate: 21 SPM")), qPrintable(diag));
        QVERIFY2(diag.contains(QStringLiteral("(1 undecoded)")), qPrintable(diag));
        QVERIFY2(diag.contains(QString::fromLatin1(s1.left(10).toHex(' '))), qPrintable(diag));
        QVERIFY2(diag.contains(QStringLiteral("Stroke rate: 21 SPM (source: monitor)")), qPrintable(diag));
        QCOMPARE(Pm5Rower::channelKey(ftms::kRowerDataUuid), QStringLiteral("0x2AD1"));
        QCOMPARE(Pm5Rower::channelKey(pm5::kGeneralStatus), QStringLiteral("0x0031"));
    }
};

QTEST_GUILESS_MAIN(TestRowerDevice)
#include "tst_rowerdevice.moc"
