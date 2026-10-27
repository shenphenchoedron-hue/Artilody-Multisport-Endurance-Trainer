// Zwift Click protocol (advertising, handshake, protobuf button/battery
// messages, press edges + auto-repeat), the ZwiftClick device and the
// controller classification in DeviceManager.
#include "Database.h"
#include "DeviceManager.h"
#include "SensorHub.h"
#include "ZwiftClick.h"
#include "SimulatedDevices.h"
#include "ZwiftClickProtocol.h"

#include <QBluetoothDeviceInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;
using namespace watt::zwift;

namespace {
QByteArray hex(const char *h)
{
    return QByteArray::fromHex(QByteArray(h).replace(' ', ""));
}

QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        quint8 b = v & 0x7F;
        v >>= 7;
        if (v)
            b |= 0x80;
        out.append(char(b));
    } while (v);
    return out;
}

// 0x23 RideKeyPadStatus with the given pressed buttons (active-low bitmap)
QByteArray rideKeys(quint32 pressed)
{
    const quint32 mask = ~pressed;
    return QByteArray(1, char(0x23)) + QByteArray(1, char(0x08)) + varint(mask)
           + hex("12 18 0a04080010000a04080110000a04080210000a0408031000");
}

QBluetoothDeviceInfo clickInfo(const QString &address, const QString &name, int type, const QList<QBluetoothUuid> &uuids = {})
{
    QBluetoothDeviceInfo info(QBluetoothAddress(address), name, 0);
    info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    if (type >= 0)
        info.setManufacturerData(kManufacturerId, QByteArray(1, char(type)) + hex("a1 6b"));
    info.setServiceUuids(uuids);
    info.setRssi(-60);
    return info;
}

struct ManagerFixture
{
    QTemporaryDir dir;
    Database db;
    SensorHub hub;
    DeviceManager mgr { &db, &hub };
    ManagerFixture() { db.open(dir.filePath(QStringLiteral("t.db"))); }
};
} // namespace

class TestZwiftClick : public QObject
{
    Q_OBJECT
private slots:
    void uuidsAndConstants()
    {
        QCOMPARE(serviceUuid(), QUuid(QStringLiteral("00000001-19CA-4651-86E5-FA29DCDD09D1")));
        QCOMPARE(asyncUuid(), QUuid(QStringLiteral("00000002-19CA-4651-86E5-FA29DCDD09D1")));
        QCOMPARE(syncRxUuid(), QUuid(QStringLiteral("00000003-19CA-4651-86E5-FA29DCDD09D1")));
        QCOMPARE(syncTxUuid(), QUuid(QStringLiteral("00000004-19CA-4651-86E5-FA29DCDD09D1")));
        QCOMPARE(int(kManufacturerId), 2378);
        QCOMPARE(rideOn(), hex("52 69 64 65 4f 6e"));
        QCOMPARE(unlockAck(), hex("ff 04 00"));
        QVERIFY(isControllerType(TypeClick));
        QVERIFY(isControllerType(TypeRideLeft));
        // whitelist of the known controller types only
        for (int t : { 0x02, 0x03, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0E })
            QVERIFY2(isControllerType(t), qPrintable(QString::number(t, 16)));
        for (int t : { 0x00, 0x01, 0x04, 0x05, 0x06, 0x0C, 0x0D, 0x0F, 0x10, 0x42, 0xFF, -1, 0x109 })
            QVERIFY2(!isControllerType(t), qPrintable(QString::number(t, 16)));
        for (int t : { 0x07, 0x08, 0x0A, 0x0B, 0x0E })
            QVERIFY(isV2ControllerType(t));
        for (int t : { 0x01, 0x02, 0x03, 0x09, 0x0C })
            QVERIFY(!isV2ControllerType(t));
        QCOMPARE(deviceTypeText(TypeClick), QStringLiteral("Zwift Click"));
        QVERIFY(nameLooksLikeController(QStringLiteral("Zwift Click")));
        QVERIFY(nameLooksLikeController(QStringLiteral("zwift click v2")));
        QVERIFY(nameLooksLikeController(QStringLiteral("Zwift SF2")));
        QVERIFY(!nameLooksLikeController(QStringLiteral("Zwift Hub")));
    }

    void handshakeResponses()
    {
        QCOMPARE(parseSyncResponse(QByteArray()), SyncResponse::None);
        QCOMPARE(parseSyncResponse(QByteArrayLiteral("RideOn")), SyncResponse::RideOn);
        // RideOn + 01 03 + 64-byte public key: the Click wants the encrypted mode
        QCOMPARE(parseSyncResponse(QByteArrayLiteral("RideOn") + hex("01 03") + QByteArray(64, '\x11')),
                 SyncResponse::PublicKey);
        // Click v2 left pod without Zwift unlock answers with a vendor challenge
        QCOMPARE(parseSyncResponse(hex("ff 03 0a 10 22 33")), SyncResponse::Challenge);
        QCOMPARE(parseSyncResponse(hex("3c 08 00")), SyncResponse::Other);
    }

    void protobufDecoding()
    {
        int pos = 0;
        quint64 v = 0;
        QVERIFY(readVarint(hex("ac 02"), pos, v));
        QCOMPARE(v, quint64(300));
        QCOMPARE(pos, 2);
        pos = 0;
        QVERIFY(!readVarint(hex("ff"), pos, v));   // truncated
        const auto f = decodeFields(hex("08 96 01 12 03 61 62 63 1d 01 00 00 00"));
        QVERIFY(f);
        QCOMPARE(f->size(), 3);
        QCOMPARE(varintField(*f, 1).value(), quint64(150));
        QCOMPARE(f->at(1).bytes, QByteArray("abc"));
        QCOMPARE(f->at(2).value, quint64(1));
        QVERIFY(!decodeFields(hex("12 05 61")));   // length beyond the data
        QVERIFY(!decodeFields(hex("0b")));          // group wire type
        QVERIFY(decodeFields(QByteArray())->isEmpty());
    }

    void clickV1Buttons()
    {
        // ClickKeyPadStatus: Button_Plus (1), Button_Minus (2); ON = 0 = pressed
        Message m = parseAsync(hex("37 08 00 10 01"));
        QCOMPARE(m.kind, Message::Kind::Buttons);
        QCOMPARE(int(m.type), 0x37);
        QVERIFY(m.buttons.plus);
        QVERIFY(!m.buttons.minus);
        m = parseAsync(hex("37 08 01 10 00"));
        QVERIFY(!m.buttons.plus);
        QVERIFY(m.buttons.minus);
        m = parseAsync(hex("37 08 01 10 01"));
        QCOMPARE(m.kind, Message::Kind::Buttons);
        QVERIFY(!m.buttons.plus && !m.buttons.minus);
        m = parseAsync(hex("37 10 00"));   // plus field missing = released
        QVERIFY(!m.buttons.plus && m.buttons.minus);
        QCOMPARE(parseAsync(hex("37 08")).kind, Message::Kind::Invalid);
        QCOMPARE(parseAsync(hex("37")).kind, Message::Kind::Invalid);
    }

    void rideAndClickV2Buttons()
    {
        // Makinolo's example: left arrow pressed (bit 0 cleared) -> no shift
        Message m = parseAsync(hex("23 08 feffffff0f 12 18 0a04080010000a04080110000a04080210000a0408031000"));
        QCOMPARE(m.kind, Message::Kind::Buttons);
        QCOMPARE(m.rideMask, 0xFFFFFFFEu);
        QVERIFY(!m.buttons.plus && !m.buttons.minus);
        // nothing pressed
        m = parseAsync(rideKeys(0));
        QVERIFY(!m.buttons.plus && !m.buttons.minus);
        // right shifter (Click v2 "+") -> plus
        m = parseAsync(rideKeys(RideShiftUpRight));
        QVERIFY(m.buttons.plus && !m.buttons.minus);
        m = parseAsync(rideKeys(RideShiftDownRight));
        QVERIFY(m.buttons.plus);
        // left shifter (Click v2 "−") and B (right pod only) -> minus
        m = parseAsync(rideKeys(RideShiftUpLeft));
        QVERIFY(!m.buttons.plus && m.buttons.minus);
        m = parseAsync(rideKeys(RideB));
        QVERIFY(m.buttons.minus);
        m = parseAsync(rideKeys(RideA | RideY));
        QVERIFY(!m.buttons.plus && !m.buttons.minus);
        QCOMPARE(parseAsync(hex("23 12 00")).kind, Message::Kind::Invalid);   // no bitmap
    }

    void batteryAndIdle()
    {
        Message m = parseAsync(hex("19 10 5d"));   // field 2 = 93 %
        QCOMPARE(m.kind, Message::Kind::Battery);
        QCOMPARE(m.battery, 93);
        m = parseAsync(hex("19 08 01 10 40"));     // type 1, level 64
        QCOMPARE(m.kind, Message::Kind::Battery);
        QCOMPARE(m.battery, 64);
        // field 1 is the battery TYPE, never a level: without field 2 it is idle
        m = parseAsync(hex("19 08 40"));
        QCOMPARE(m.kind, Message::Kind::Idle);
        QCOMPARE(m.battery, -1);
        QCOMPARE(parseAsync(hex("19 08 01")).kind, Message::Kind::Idle);
        QCOMPARE(parseAsync(hex("19")).kind, Message::Kind::Idle);
        QCOMPARE(parseAsync(hex("19 10 c8 01")).kind, Message::Kind::Idle);   // 200: not a level
        QCOMPARE(parseAsync(hex("15")).kind, Message::Kind::Idle);
        QCOMPARE(parseAsync(hex("3c 08 00")).kind, Message::Kind::Other);
        QCOMPARE(parseAsync(QByteArray()).kind, Message::Kind::Invalid);
    }

    void buttonEdgesAndRepeat()
    {
        ButtonTracker t;
        ShiftButtons plus;
        plus.plus = true;
        ShiftButtons minus;
        minus.minus = true;
        const ShiftButtons none;
        QCOMPARE(t.update(plus, 0), QVector<int>({ 1 }));
        QCOMPARE(t.update(plus, 100), QVector<int>());       // repeated report: no new press
        QCOMPARE(t.poll(400), QVector<int>());
        QCOMPARE(t.poll(500), QVector<int>({ 1 }));           // auto-repeat after 500 ms
        QCOMPARE(t.poll(600), QVector<int>());
        QCOMPARE(t.poll(750), QVector<int>({ 1 }));           // then every 250 ms
        QCOMPARE(t.update(none, 800), QVector<int>());        // release
        QCOMPARE(t.poll(2000), QVector<int>());
        QCOMPARE(t.update(minus, 2100), QVector<int>({ -1 }));
        QCOMPARE(t.update(none, 2150), QVector<int>());
        QCOMPARE(t.update(minus, 2200), QVector<int>({ -1 })); // quick double press
        // both buttons: nothing, also no repeat
        ShiftButtons both;
        both.plus = both.minus = true;
        QCOMPARE(t.update(both, 2300), QVector<int>());
        QCOMPARE(t.poll(5000), QVector<int>());
        QCOMPARE(t.update(plus, 5100), QVector<int>());      // minus let go: plus was already down
        QCOMPARE(t.update(none, 5200), QVector<int>());
        // a stalled timer gives one step, not a burst
        QCOMPARE(t.update(plus, 6000), QVector<int>({ 1 }));
        QCOMPARE(t.poll(9000), QVector<int>({ 1 }));
        QCOMPARE(t.poll(9001), QVector<int>());
    }

    void deviceHandlesNotifications()
    {
        ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:A1"), QStringLiteral("Zwift Click"), TypeClick));
        QCOMPARE(click.id(), QStringLiteral("DE:53:77:EB:6B:A1"));
        QCOMPARE(click.name(), QStringLiteral("Zwift Click"));
        QCOMPARE(click.deviceType(), int(TypeClick));
        QCOMPARE(click.batteryLevel(), -1);
        QVERIFY(!click.isV2());
        QSignalSpy shifts(&click, &ControllerDevice::shiftRequested);
        QSignalSpy battery(&click, &ControllerDevice::batteryChanged);
        const QBluetoothUuid async(asyncUuid());
        click.handleNotification(QBluetoothUuid(syncTxUuid()), QByteArrayLiteral("RideOn"));
        QVERIFY(click.handshakeDone());
        click.handleNotification(async, hex("37 08 00 10 01"));   // plus pressed
        click.handleNotification(async, hex("37 08 00 10 01"));
        click.handleNotification(async, hex("37 08 01 10 01"));   // released
        click.handleNotification(async, hex("37 08 01 10 00"));   // minus pressed
        click.handleNotification(async, hex("37 08 01 10 01"));
        QCOMPARE(shifts.count(), 2);
        QCOMPARE(shifts.at(0).at(0).toInt(), 1);
        QCOMPARE(shifts.at(1).at(0).toInt(), -1);
        click.handleNotification(async, hex("19 10 5d"));
        QCOMPARE(click.batteryLevel(), 93);
        QCOMPARE(battery.count(), 1);
        // a Click v2 frame switches to the v2 behaviour
        click.handleNotification(async, rideKeys(RideShiftUpRight));
        QVERIFY(click.isV2());
        QCOMPARE(shifts.count(), 3);
        QCOMPARE(shifts.last().at(0).toInt(), 1);
        QVERIFY(click.protocol().contains(QStringLiteral("v2")));
        // notifications of other characteristics are ignored
        click.handleNotification(QBluetoothUuid(quint16(0x2a19)), hex("37 08 00 10 01"));
        QCOMPARE(shifts.count(), 3);
    }

    void deviceAutoRepeat()
    {
        ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:A2"), QStringLiteral("Zwift Click"), TypeClick));
        QSignalSpy shifts(&click, &ControllerDevice::shiftRequested);
        click.handleNotification(QBluetoothUuid(asyncUuid()), hex("37 08 00 10 01"));
        QCOMPARE(shifts.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(shifts.count() >= 3, 2000);   // held: repeats
        click.handleNotification(QBluetoothUuid(asyncUuid()), hex("37 08 01 10 01"));
        const int n = shifts.count();
        QTest::qWait(600);
        QCOMPARE(shifts.count(), n);
    }

    // A held button is re-reported with every frame (Click v2: 10-20/s).
    // Restarting the 50 ms repeat timer on each frame kept it from firing.
    void deviceAutoRepeatWithFastFrames()
    {
        ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:B1"), QStringLiteral("Zwift Click"), TypeClick));
        QSignalSpy shifts(&click, &ControllerDevice::shiftRequested);
        const QBluetoothUuid async(asyncUuid());
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1300) {   // press held, a frame every ~20 ms
            click.handleNotification(async, hex("37 08 00 10 01"));
            QVERIFY(click.repeatTimerActive());
            QTest::qWait(20);
        }
        // first press + repeats at ~500, 750, 1000, 1250 ms
        QVERIFY2(shifts.count() >= 3, qPrintable(QString::number(shifts.count())));
        for (const auto &a : shifts)
            QCOMPARE(a.at(0).toInt(), 1);
        click.handleNotification(async, hex("37 08 01 10 01"));
        QVERIFY(!click.repeatTimerActive());
        const int n = shifts.count();
        QTest::qWait(400);
        QCOMPARE(shifts.count(), n);
        // same with Click v2 frames
        shifts.clear();
        t.restart();
        while (t.elapsed() < 1100) {
            click.handleNotification(async, rideKeys(RideShiftUpLeft));
            QTest::qWait(15);
        }
        QVERIFY2(shifts.count() >= 3, qPrintable(QString::number(shifts.count())));
        QCOMPARE(shifts.first().at(0).toInt(), -1);
        click.handleNotification(async, rideKeys(0));
    }

    void buttonsChangedOnlyOnChange()
    {
        ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:B2"), QStringLiteral("Zwift Click"), TypeClick));
        QSignalSpy buttons(&click, &ControllerDevice::buttonsChanged);
        const QBluetoothUuid async(asyncUuid());
        click.handleNotification(async, hex("37 08 00 10 01"));
        click.handleNotification(async, hex("37 08 00 10 01"));
        QCOMPARE(buttons.count(), 1);
        // both pressed (no shift, "held" is false) - still a state change
        click.handleNotification(async, hex("37 08 00 10 00"));
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.last().at(0).toBool(), true);
        QCOMPARE(buttons.last().at(1).toBool(), true);
        click.handleNotification(async, hex("37 08 01 10 01"));
        QCOMPARE(buttons.count(), 3);
        QCOMPARE(buttons.last().at(0).toBool(), false);
        QCOMPARE(buttons.last().at(1).toBool(), false);
    }

    void disconnectReleasesButtons()
    {
#ifdef Q_OS_MACOS
        QSKIP("needs a Bluetooth stack (not in a bare macOS test binary)");
#endif
        // both buttons pressed: ButtonTracker::held() is false, the release
        // must still be reported when the link drops
        ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:B3"), QStringLiteral("Zwift Click"), TypeClick));
        QSignalSpy buttons(&click, &ControllerDevice::buttonsChanged);
        click.handleNotification(QBluetoothUuid(asyncUuid()), hex("37 08 00 10 00"));
        QCOMPARE(buttons.count(), 1);
        click.connectDevice();      // Connecting (no adapter in the test)
        click.disconnectDevice();   // -> Disconnected
        QTRY_COMPARE(buttons.count(), 2);
        QCOMPARE(buttons.last().at(0).toBool(), false);
        QCOMPARE(buttons.last().at(1).toBool(), false);
    }

    void v2DetectedFromDeviceType()
    {
        // Ride / Click v2 types: v2 (keep-alive, unlock ack) from the advertisement
        for (int t : { 0x07, 0x08, 0x0A, 0x0B, 0x0E }) {
            ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:C1"), QString(), t));
            QVERIFY2(click.isV2(), qPrintable(QString::number(t, 16)));
            QCOMPARE(click.deviceType(), t);
            QVERIFY(click.protocol().contains(QStringLiteral("v2")));
        }
        for (int t : { 0x09, 0x02, -1 }) {
            ZwiftClick click(clickInfo(QStringLiteral("DE:53:77:EB:6B:C2"), QStringLiteral("Zwift Click"), t));
            QVERIFY(!click.isV2());
        }
    }

    void classification()
    {
        using K = DeviceManager::Kind;
        const QBluetoothUuid ftms(quint16(0x1826));
        // manufacturer data 0x094A, type 9
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A1"), QStringLiteral("Zwift Click"), TypeClick)),
                 K::Controller);
        // name only (manufacturer data missing, e.g. cached device) – not a trainer despite "ZWIFT"
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A3"), QStringLiteral("Zwift Click"), -1)),
                 K::Controller);
        // Click v2 pods without a name
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A4"), QString(), 0x0b)), K::Controller);
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A8"), QString(), 0x0e)), K::Controller);
        // unknown Zwift type without name / 0xFC82: never a controller
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A9"), QString(), 0x0c)), K::Unknown);
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:AA"), QString(), 0x42)), K::Unknown);
        // ... unless the name or the service says so
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:AB"), QStringLiteral("Zwift Click"), 0x0c)),
                 K::Controller);
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:AC"), QString(), 0x42,
                                                   { QBluetoothUuid(quint16(0xFC82)) })),
                 K::Controller);
        // newer firmware: 0xFC82 service only
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A5"), QString(), -1,
                                                   { QBluetoothUuid(quint16(0xFC82)) })),
                 K::Controller);
        // the Zwift Hub trainer (type 1, FTMS) stays a trainer
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A6"), QStringLiteral("Zwift Hub"), TypeHub, { ftms })),
                 K::Trainer);
        QCOMPARE(DeviceManager::classify(clickInfo(QStringLiteral("DE:53:77:EB:6B:A7"), QStringLiteral("Zwift Hub"), TypeHub)),
                 K::Trainer);
        QCOMPARE(DeviceManager::kindName(K::Controller), QStringLiteral("Controller"));
    }

    void managerListsControllers()
    {
        ManagerFixture f;
        QCOMPARE(f.mgr.controllers()->rowCount(), 0);
        QVERIFY(!f.mgr.controllerConnected());
        QCOMPARE(f.mgr.controllerBattery(), -1);
        QCOMPARE(f.mgr.controllerName(), QString());
        f.mgr.handleDeviceInfo(clickInfo(QStringLiteral("DE:53:77:EB:6B:A1"), QStringLiteral("Zwift Click"), TypeClick));
        QCOMPARE(f.mgr.controllers()->rowCount(), 1);
        QCOMPARE(f.mgr.trainers()->rowCount(), 0);
        QCOMPARE(f.mgr.otherDevices()->rowCount(), 0);
        const QModelIndex idx = f.mgr.controllers()->index(0);
        QCOMPARE(idx.data(DeviceListModel::KindRole).toString(), QStringLiteral("controller"));
        QCOMPARE(idx.data(DeviceListModel::NameRole).toString(), QStringLiteral("Zwift Click"));
        QVERIFY(idx.data(DeviceListModel::ProtocolRole).toString().contains(QStringLiteral("Zwift Click")));
        const QString report = f.mgr.diagnosticsReport();
        QVERIFY(report.contains(QStringLiteral("[Controllers]")));
        QVERIFY(report.contains(QStringLiteral("class: Controller")));
        QVERIFY(report.contains(QStringLiteral("0x094a=09 a1 6b")));
    }

    void managerConnectsController()
    {
#ifdef Q_OS_MACOS
        // CoreBluetooth aborts processes without an Info.plist usage string.
        QSKIP("needs a Bluetooth stack (not in a bare macOS test binary)");
#endif
        ManagerFixture f;
        f.mgr.handleDeviceInfo(clickInfo(QStringLiteral("DE:53:77:EB:6B:A1"), QStringLiteral("Zwift Click"), TypeClick));
        // connecting creates the device (no adapter here: it stays connecting)
        f.mgr.connectDevice(QStringLiteral("DE:53:77:EB:6B:A1"));
        QCOMPARE(f.mgr.controllerId(), QStringLiteral("DE:53:77:EB:6B:A1"));
        QCOMPARE(f.mgr.controllerDevices().size(), 1);
        QCOMPARE(f.db.setting(QStringLiteral("ble/knownControllers")).toString(), QStringLiteral("DE:53:77:EB:6B:A1"));
        QSignalSpy shift(&f.mgr, &DeviceManager::controllerShift);
        QSignalSpy up(&f.mgr, &DeviceManager::controllerShiftUp);
        auto *click = qobject_cast<ZwiftClick *>(f.mgr.controllerDevices().first());
        QVERIFY(click);
        click->handleNotification(QBluetoothUuid(asyncUuid()), hex("37 08 00 10 01"));
        QCOMPARE(shift.count(), 1);
        QCOMPARE(up.count(), 1);
        click->handleNotification(QBluetoothUuid(asyncUuid()), hex("37 08 01 10 01"));
        f.mgr.disconnectDevice(QStringLiteral("DE:53:77:EB:6B:A1"));
        QCOMPARE(f.mgr.controllerDevices().size(), 0);
        QCOMPARE(f.db.setting(QStringLiteral("ble/knownControllers")).toString(), QString());
    }

    void knownControllersCapped()
    {
#ifdef Q_OS_MACOS
        QSKIP("initialize() queries the Bluetooth adapter (not in a bare macOS test binary)");
#endif
        ManagerFixture f;
        f.db.setSetting(QStringLiteral("ble/knownControllers"), QStringLiteral("a;b;c;b;d;e;f"));
        f.mgr.setAutoConnect(false);
        f.mgr.initialize();
        QCOMPARE(f.mgr.knownControllers(), QStringList({ QStringLiteral("c"), QStringLiteral("d"),
                                                         QStringLiteral("e"), QStringLiteral("f") }));
        QCOMPARE(f.db.setting(QStringLiteral("ble/knownControllers")).toString(), QStringLiteral("c;d;e;f"));
    }

    // Both pods of a Click v2 / Zwift Ride may report one press: a ±1 equal
    // to one of the other controller within kShiftDedupeMs is dropped.
    void managerDedupesBothPods()
    {
        ManagerFixture f;
        f.mgr.connectSimulatedController(QStringLiteral("sim:click"));
        f.mgr.connectSimulatedController(QStringLiteral("sim:click2"));
        QCOMPARE(f.mgr.controllerCount(), 2);
        QSignalSpy shift(&f.mgr, &DeviceManager::controllerShift);
        QSignalSpy down(&f.mgr, &DeviceManager::controllerShiftDown);
        QVERIFY(f.mgr.pressSimulatedController(-1, QStringLiteral("sim:click")));
        QVERIFY(f.mgr.pressSimulatedController(-1, QStringLiteral("sim:click2")));   // same press, other pod
        QCOMPARE(shift.count(), 1);
        QCOMPARE(down.count(), 1);
        // opposite direction from the other pod: a real press
        QVERIFY(f.mgr.pressSimulatedController(1, QStringLiteral("sim:click2")));
        QCOMPARE(shift.count(), 2);
        // a quick double press on the SAME controller is never merged
        QVERIFY(f.mgr.pressSimulatedController(1, QStringLiteral("sim:click2")));
        QCOMPARE(shift.count(), 3);
        // the other pod after the window: counts
        QTest::qWait(DeviceManager::kShiftDedupeMs + 40);
        QVERIFY(f.mgr.pressSimulatedController(1, QStringLiteral("sim:click")));
        QCOMPARE(shift.count(), 4);
        QCOMPARE(shift.last().at(0).toInt(), 1);
    }

    void managerOrsButtonStates()
    {
        ManagerFixture f;
        f.mgr.connectSimulatedController(QStringLiteral("sim:click"));
        f.mgr.connectSimulatedController(QStringLiteral("sim:click2"));
        auto sim = [&](const QString &id) {
            for (ControllerDevice *c : f.mgr.controllerDevices())
                if (c->id() == id)
                    return qobject_cast<SimulatedController *>(c);
            return static_cast<SimulatedController *>(nullptr);
        };
        SimulatedController *a = sim(QStringLiteral("sim:click"));
        SimulatedController *b = sim(QStringLiteral("sim:click2"));
        QVERIFY(a && b);
        QSignalSpy buttons(&f.mgr, &DeviceManager::controllerButtonsChanged);
        a->setButtons(true, false);
        QCOMPARE(buttons.count(), 1);
        QCOMPARE(buttons.last().at(0).toBool(), true);
        b->setButtons(true, false);   // the same button on the other pod: no change
        QCOMPARE(buttons.count(), 1);
        a->setButtons(false, false);  // b still holds it
        QCOMPARE(buttons.count(), 1);
        b->setButtons(true, true);
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.last().at(1).toBool(), true);
        a->setButtons(true, false);
        // releasing one controller only clears its own state
        f.mgr.disconnectDevice(QStringLiteral("sim:click2"));
        QCOMPARE(buttons.count(), 3);
        QCOMPARE(buttons.last().at(0).toBool(), true);
        QCOMPARE(buttons.last().at(1).toBool(), false);
        f.mgr.disconnectDevice(QStringLiteral("sim:click"));
        QCOMPARE(buttons.count(), 4);
        QCOMPARE(buttons.last().at(0).toBool(), false);
        QCOMPARE(buttons.last().at(1).toBool(), false);
    }

    // Single-sensor mode: controllers do not count toward the exclusivity.
    void singleSensorModeKeepsControllers()
    {
        ManagerFixture f;
        f.mgr.setSimulatorEnabled(false);
        f.mgr.setSimulatorEnabled(true);   // virtual devices without Bluetooth
        f.mgr.setAllowMultiple(false);
        f.mgr.connectDevice(QStringLiteral("sim:trainer"));
        QTRY_COMPARE_WITH_TIMEOUT(f.mgr.trainerState(), int(LinkState::Connected), 3000);
        // a Click next to the trainer, and both Click v2 pods
        f.mgr.connectSimulatedController(QStringLiteral("sim:click"));
        f.mgr.connectSimulatedController(QStringLiteral("sim:click2"));
        QCOMPARE(f.mgr.controllerCount(), 2);
        QCOMPARE(f.mgr.trainerState(), int(LinkState::Connected));
        // the HR sensor replaces the trainer (single sensor) but not the controllers
        f.mgr.connectDevice(QStringLiteral("sim:hr"));
        QCOMPARE(f.mgr.trainerId(), QString());
        QCOMPARE(f.mgr.controllerCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(f.mgr.heartRateState(), int(LinkState::Connected), 3000);
        // the limit still applies: a third controller is refused while both are connected
        f.mgr.connectSimulatedController(QStringLiteral("sim:click3"));
        QCOMPARE(f.mgr.controllerDevices().size(), DeviceManager::kMaxControllers);
        QCOMPARE(f.mgr.controllerCount(), 2);
        // a released sim:click can be connected again from the list
        f.mgr.disconnectDevice(QStringLiteral("sim:click"));
        QCOMPARE(f.mgr.controllerCount(), 1);
        QVERIFY(f.mgr.controllers()->contains(QStringLiteral("sim:click")));
        f.mgr.connectDevice(QStringLiteral("sim:click"));
        QCOMPARE(f.mgr.controllerCount(), 2);
        // simulated controllers are not remembered as known controllers
        QVERIFY(f.mgr.knownControllers().isEmpty());
    }

    void managerDoesNotEvictForUnknownId()
    {
        ManagerFixture f;
        f.mgr.connectSimulatedController(QStringLiteral("sim:click"));
        f.mgr.connectSimulatedController(QStringLiteral("sim:click2"));
        auto *a = qobject_cast<SimulatedController *>(f.mgr.controllerDevices().first());
        QVERIFY(a);
        a->disconnectDevice();   // not connected: would be dropped to make room
        QCOMPARE(f.mgr.controllerDevices().size(), 2);
        // a listed controller without scan info (nothing to connect to) must
        // not evict the sleeping one
        DeviceEntry e;
        e.id = QStringLiteral("DE:53:77:EB:6B:D1");
        e.name = QStringLiteral("Zwift Click");
        e.kind = QStringLiteral("controller");
        f.mgr.controllers()->upsert(e);
        f.mgr.connectDevice(e.id);
        QCOMPARE(f.mgr.controllerDevices().size(), 2);
        QVERIFY(f.mgr.controllerDevices().contains(a));
    }
};

QTEST_GUILESS_MAIN(TestZwiftClick)
#include "tst_zwiftclick.moc"
