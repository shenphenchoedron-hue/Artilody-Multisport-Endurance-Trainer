// Zwift trainer ("hub") protocol and Zwift Click virtual shifting:
//  * protocol bytes (verified examples) and round trips (ZwiftHubProtocol)
//  * handshake state machine with a scripted fake trainer (ZwiftHubLink),
//    fallback on timeout, keep-alive, only changed fields, rate limits
//  * BleTrainer: detection only on the connected trainer's own services, no
//    FTMS 0x11 / 0x05 while the hub path is active, immediate Physical on a
//    gear change, FTMS gradient after a failed handshake (never ERG)
//  * Click-tied activation (VirtualGears + SensorHub + RideResistance)
//  * Zwift's 24-gear table and the migration of an old gear setup
//  * the virtual trainer's Zwift hub capability (WATT_SIM_TRAINER_ZWIFT)
#include "BleTrainer.h"
#include "Database.h"
#include "FakeGattLink.h"
#include "FtmsProtocol.h"
#include "Gearing.h"
#include "ProfileManager.h"
#include "RideResistance.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"
#include "VirtualGears.h"
#include "ZwiftHubLink.h"
#include "ZwiftHubProtocol.h"

#include <QSignalSpy>
#include <QTest>

using namespace watt;
using watt::test::FakeGattLink;

namespace {
QBluetoothUuid u16(quint16 v) { return QBluetoothUuid(v); }
const QBluetoothUuid kCp = u16(ftms::kControlPointUuid);
constexpr quint8 kOpSim = 0x11, kOpPower = 0x05;
const QBluetoothUuid kAsync = ZwiftHubLink::asyncUuid();
const QBluetoothUuid kRx = ZwiftHubLink::syncRxUuid();
const QBluetoothUuid kTx = ZwiftHubLink::syncTxUuid();
const QBluetoothUuid kZwift128 = QBluetoothUuid(QUuid(QStringLiteral("{00000001-19ca-4651-86e5-fa29dcdd09d1}")));

QByteArray hex(const char *h) { return QByteArray::fromHex(h); }

QList<QBluetoothUuid> victoryFtms()
{
    return { u16(0x2AD2), u16(0x2ACC), u16(0x2AD6), u16(0x2AD8), u16(0x2ADA), u16(0x2AD9) };
}
QList<QBluetoothUuid> hubChars() { return { kAsync, kRx, kTx }; }

// A trainer that answers the handshake ("RideOn" on 0004) and starts the
// riding data stream (0x03 on 0002) after 41 08 05; FTMS CP answered "Success".
void hubResponder(FakeGattLink *link, bool answer = true)
{
    link->responder = [answer](const QBluetoothUuid &c, const QByteArray &d) -> QVector<FakeGattLink::Notification> {
        if (c == kCp && !d.isEmpty())
            return { { kCp, QByteArray::fromHex("80") + d.left(1) + QByteArray::fromHex("01") } };
        if (!answer || c != kRx)
            return {};
        if (zhub::isRideOn(d))
            return { { kTx, QByteArray("RideOn") } };
        if (d == zhub::startStream()) {
            zhub::RidingData r;
            r.power = 180;
            r.cadence = 85;
            r.speedKmh = 30.5;
            return { { kAsync, zhub::encodeRidingData(r) } };
        }
        return {};
    };
}

int countPrefix(const FakeGattLink *link, quint8 prefix)
{
    int n = 0;
    for (const QByteArray &w : link->writesTo(kRx))
        if (!w.isEmpty() && quint8(w.at(0)) == prefix && !zhub::isRideOn(w))
            ++n;
    return n;
}

QVector<zhub::Command> commands(const FakeGattLink *link)
{
    QVector<zhub::Command> out;
    for (const QByteArray &w : link->writesTo(kRx))
        if (!w.isEmpty() && quint8(w.at(0)) == zhub::OutCommand && !zhub::isRideOn(w))
            if (auto c = zhub::decodeCommand(w.mid(1)))
                out << *c;
    return out;
}

HubSimulation params(double incline, double ratio)
{
    HubSimulation p;
    p.inclinePercent = incline;
    p.gearRatio = ratio;
    p.cw = 0.51;
    p.crr = 0.004;
    p.riderKg = 75;
    p.bikeKg = 10;
    return p;
}

// BleTrainer on a fake Victory (FTMS + the Zwift service), wired to a
// SensorHub like DeviceManager::attachTrainer().
struct TrainerRig
{
    FakeGattLink *link = nullptr;
    BleTrainer *trainer = nullptr;
    SensorHub hub;

    explicit TrainerRig(bool zwiftService = true, bool answer = true, bool fc82 = true)
    {
        QList<QBluetoothUuid> chars = victoryFtms();
        QList<QBluetoothUuid> services { u16(0x1826), u16(0x1818) };
        if (zwiftService) {
            chars += hubChars();
            services << (fc82 ? u16(0xFC82) : kZwift128);
        }
        link = new FakeGattLink(chars, services);
        link->readValues.insert(u16(ftms::kFeatureUuid), QByteArray::fromHex("024000000c200000"));
        hubResponder(link, answer);
        trainer = new BleTrainer(link, QStringLiteral("E7:45:CF:5F:0F:D9"), QStringLiteral("Victory 0FD9"));
        trainer->setTimingForTesting(100, 50, 100);
        trainer->zwiftHub()->setTimingForTesting(80, 80, 400, 100);
        QObject::connect(trainer, &TrainerDevice::zwiftHubChanged, &hub,
                         [this] { hub.setTrainerZwiftHub(trainer->zwiftHubState(), trainer->zwiftHubStatus()); });
        QObject::connect(&hub, &SensorHub::zwiftHubChanged, trainer,
                         [this] { trainer->setZwiftHubEnabled(hub.zwiftHubWanted()); });
        QObject::connect(&hub, &SensorHub::hubSimulationRequested, trainer,
                         [this](const HubSimulation &p) { trainer->setHubSimulation(p); });
        QObject::connect(&hub, &SensorHub::simulationRequested, trainer,
                         [this](double g, double w, double crr, double cw) { trainer->setSimulation(g, w, crr, cw); });
        QObject::connect(&hub, &SensorHub::targetPowerRequested, trainer, [this](int w) { trainer->setTargetPower(w); });
    }
    ~TrainerRig() { delete trainer; }
    void connect()
    {
        trainer->connectDevice();
        hub.setTrainerConnected(true);
        hub.setTrainerSimulationSupport(1);
        link->finishOtherServices();   // BleLink: the other services were inspected
    }
};
} // namespace

class TestZwiftHub : public QObject
{
    Q_OBJECT

private slots:
    // --- protocol bytes ----------------------------------------------------------

    void verifiedByteExamples()
    {
        zhub::Physical phys;
        phys.gearRatio = 1.0;
        phys.bikeKg = 10;
        phys.riderKg = 75;
        QCOMPARE(zhub::encodePhysical(phys), hex("042a0910904e20e80728cc3a"));
        zhub::Simulation sim;
        sim.windMps = 0;
        sim.inclinePercent = -4.0;
        sim.cw = 0.51;
        sim.crr = 0.004;
        QCOMPARE(zhub::encodeSimulation(sim), hex("04220b0800109f0618ec27209003"));
        // only the set fields; 0 % goes out as 0.01 % (InclineX100 1, like Zwift / Incyclist)
        zhub::Simulation flat;
        flat.inclinePercent = 0.0;
        QCOMPARE(zhub::encodeSimulation(flat), hex("0422021002"));
        zhub::Simulation up;
        up.inclinePercent = 1.5;   // 150 -> zigzag 300 = ac 02
        QCOMPARE(zhub::encodeSimulation(up), hex("04220310ac02"));
        zhub::Physical gear12;
        gear12.gearRatio = zwiftgears::ratio(12);   // 2.40 -> 24000 = c0 bb 01
        QCOMPARE(zhub::encodePhysical(gear12), hex("042a0410c0bb01"));
        QCOMPARE(zhub::encodeRequest(512), hex("00088004"));
        QCOMPARE(zhub::handshake(), QByteArray("RideOn") + hex("0201"));
        QCOMPARE(zhub::startStream(), hex("410805"));
        QCOMPARE(zhub::encodePowerTarget(200), hex("0418c801"));
        QCOMPARE(zhub::inclineX100(0.004), 1);
        QCOMPARE(zhub::inclineX100(-0.004), 1);
        QCOMPARE(zhub::inclineX100(-4.0), -400);
    }

    void roundTrips()
    {
        zhub::Simulation sim;
        sim.inclinePercent = -2.37;
        sim.windMps = 1.25;
        sim.cw = 0.392;
        sim.crr = 0.0045;
        const auto c = zhub::decodeCommand(zhub::encodeSimulation(sim).mid(1));
        QVERIFY(c && c->simulation && !c->physical && !c->powerTarget);
        QCOMPARE(*c->simulation->inclinePercent, -2.37);
        QCOMPARE(*c->simulation->windMps, 1.25);
        QCOMPARE(*c->simulation->cw, 0.392);
        QCOMPARE(*c->simulation->crr, 0.0045);
        zhub::Physical p;
        p.gearRatio = 5.49;
        const auto c2 = zhub::decodeCommand(zhub::encodePhysical(p).mid(1));
        QVERIFY(c2 && c2->physical);
        QCOMPARE(*c2->physical->gearRatio, 5.49);
        QVERIFY(!c2->physical->bikeKg && !c2->physical->riderKg);   // not sent
        QCOMPARE(zhub::decodeRequest(zhub::encodeRequest(512).mid(1)), std::optional<quint32>(512));

        zhub::RidingData r;
        r.power = 187;
        r.cadence = 88;
        r.speedKmh = 36.12;
        r.heartRate = 141;
        const QByteArray riding = zhub::encodeRidingData(r);
        QCOMPARE(quint8(riding.at(0)), quint8(zhub::InRidingData));
        const auto back = zhub::decodeRidingData(riding.mid(1));
        QVERIFY(back);
        QCOMPARE(back->power, 187);
        QCOMPARE(back->cadence, 88);
        QCOMPARE(back->speedKmh, 36.12);
        QCOMPARE(back->heartRate, 141);
        // hand-made HubRidingData: 08 b4 01 (180 W) 10 55 (85 rpm) 18 ea 17 (30.50 km/h) 20 00
        const auto hand = zhub::decodeRidingData(hex("08b401105518ea172000"));
        QVERIFY(hand);
        QCOMPARE(hand->power, 180);
        QCOMPARE(hand->speedKmh, 30.5);
        QVERIFY(!zhub::decodeRidingData(hex("08")));   // truncated

        zhub::Settings st;
        st.inclinePercent = -1.5;
        st.gearRatio = 2.4;
        st.riderKg = 75;
        st.bikeKg = 10;
        st.crr = 0.004;
        st.windMps = 0;
        const QByteArray echo = zhub::encodeSettingsEcho(st);
        QCOMPARE(quint8(echo.at(0)), quint8(zhub::InDeviceData));
        const auto s2 = zhub::decodeDeviceData(echo.mid(1));
        QVERIFY(s2);
        QCOMPARE(s2->messageType, 512);
        QCOMPARE(*s2->inclinePercent, -1.5);
        QCOMPARE(*s2->gearRatio, 2.4);
        QCOMPARE(*s2->riderKg, 75.0);
        QCOMPARE(*s2->bikeKg, 10.0);
        QCOMPARE(*s2->crr, 0.004);
        // device info (type < 16) is not a settings echo
        QVERIFY(!zhub::decodeDeviceData(hex("0801120400000000")));
        QVERIFY(zhub::describe(echo, false).startsWith(QStringLiteral("settings echo (type 512): incline -1.50 %")));
        QCOMPARE(zhub::describe(hex("042a0410c0bb01"), true), QStringLiteral("command Physical: gear ratio 2.4000"));
        QCOMPARE(zhub::describe(hex("04220b0800109f0618ec27209003"), true),
                 QStringLiteral("command Simulation: incline -4.00 %, wind 0.00 m/s, CWa 0.5100 kg/m, Crr 0.00400"));
        QCOMPARE(zhub::describe(hex("1910"), false), QStringLiteral("keep-alive"));
        QVERIFY(zhub::describe(QByteArray("RideOn"), false).contains(QStringLiteral("RideOn")));
    }

    void serviceDetection()
    {
        QVERIFY(zhub::isZwiftService(QUuid(QStringLiteral("{0000fc82-0000-1000-8000-00805f9b34fb}"))));
        QVERIFY(zhub::isZwiftService(QUuid(QStringLiteral("{00000001-19ca-4651-86e5-fa29dcdd09d1}"))));
        QVERIFY(!zhub::isZwiftService(QUuid(QStringLiteral("{00001826-0000-1000-8000-00805f9b34fb}"))));
        // only the connected trainer's own services count: FTMS only -> absent
        FakeGattLink ftmsOnly(victoryFtms(), { u16(0x1826), u16(0x1818) });
        QVERIFY(!ZwiftHubLink::linkHasHub(&ftmsOnly));
        // the service without the three characteristics -> absent
        FakeGattLink noChars(victoryFtms(), { u16(0x1826), u16(0xFC82) });
        QVERIFY(!ZwiftHubLink::linkHasHub(&noChars));
        FakeGattLink fc82(victoryFtms() + hubChars(), { u16(0x1826), u16(0xFC82) });
        QVERIFY(ZwiftHubLink::linkHasHub(&fc82));
        FakeGattLink custom(victoryFtms() + hubChars(), { u16(0x1826), kZwift128 });
        QVERIFY(ZwiftHubLink::linkHasHub(&custom));
    }

    void zwiftGearTable()
    {
        const double expected[24] = { 0.75, 0.87, 0.99, 1.11, 1.23, 1.38, 1.53, 1.68, 1.86, 2.04, 2.22, 2.40,
                                      2.61, 2.82, 3.03, 3.24, 3.49, 3.74, 3.99, 4.24, 4.54, 4.84, 5.14, 5.49 };
        QCOMPARE(zwiftgears::kCount, 24);
        QCOMPARE(zwiftgears::kStartGear, 12);
        for (int i = 0; i < 24; ++i) {
            QCOMPARE(zwiftgears::kRatios[i], expected[i]);
            if (i > 0)
                QVERIFY(zwiftgears::kRatios[i] > zwiftgears::kRatios[i - 1]);
        }
        Gearing g = Gearing::zwift(GearSetup::defaults());
        QVERIFY(g.isZwift());
        QCOMPARE(g.gearCount(), 24);
        QCOMPARE(g.gearIndex(), 11);
        QCOMPARE(g.ratio(), 2.40);
        QCOMPARE(g.label(), QStringLiteral("12"));
        QCOMPARE(g.frontTeeth(), 0);
        QVERIFY(!g.setGear(1, 6));
        QVERIFY(g.setGearIndex(0));
        QVERIFY(!g.canShiftDown());
        QVERIFY(!g.shiftDown());
        QCOMPARE(g.ratio(), 0.75);
        QVERIFY(g.shiftFrontUp());   // jump 3
        QCOMPARE(g.zwiftGear(), 4);
        QVERIFY(g.setGearIndex(23));
        QVERIFY(!g.shiftUp());
        QCOMPARE(g.ratio(), 5.49);
        QCOMPARE(g.sequence().size(), 24);
        // a setup change keeps the Zwift gear, uses the new wheel
        GearSetup s = GearSetup::defaults();
        s.circumferenceMm = 2096;
        g.setSetup(s);
        QCOMPARE(g.zwiftGear(), 24);
        QCOMPARE(g.virtualSpeed(60), 5.49 * 2.096);
    }

    // The rider's old setup (chainring 44, 10–44, reference 44×17) is kept in
    // the database but virtual shifting uses Zwift's 24 gears.
    void oldGearSetupIsIgnoredButKept()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        GearSetup old = GearSetup::defaults();
        old.chainrings = { 44 };
        old.cogs = { 10, 11, 13, 15, 17, 19, 21, 24, 28, 32, 38, 44 };
        old.frontIndex = 0;
        old.rearIndex = 3;
        old.referenceFrontTeeth = 44;
        old.referenceRearTeeth = 17;
        old.sequential = false;
        const QString stored = old.normalized().toJsonText();
        QVERIFY(db.setGearSetupJson(profiles.profileId(), stored));
        VirtualGears g(&db, &profiles);
        g.setManualSave(true);
        QCOMPARE(g.gearCount(), 24);
        QCOMPARE(g.gearText(), QStringLiteral("Gear 12/24 (2.40)"));
        QCOMPARE(g.gearRatio(), 2.40);
        QCOMPARE(g.referenceLabel(), QStringLiteral("44×17"));   // still the FTMS-gradient reference
        QCOMPARE(g.gears().size(), 24);
        QVERIFY(g.shiftUp());
        QCOMPARE(g.gearText(), QStringLiteral("Gear 13/24 (2.61)"));
        g.flushSettings();
        QCOMPARE(GearSetup::fromJsonText(db.gearSetupJson(profiles.profileId())), GearSetup::fromJsonText(stored));
        QCOMPARE(g.setup().chainrings, QVector<int>({ 44 }));
    }

    // --- the driver ----------------------------------------------------------------

    void handshakeStateMachine()
    {
        FakeGattLink link(victoryFtms() + hubChars(), { u16(0x1826), u16(0xFC82) });
        hubResponder(&link);
        link.connectToDevice();
        ZwiftHubLink z(&link);
        QObject::connect(&link, &GattLink::characteristicChanged, &z, &ZwiftHubLink::handleNotification);
        QCOMPARE(z.state(), ZwiftHubLink::State::Absent);
        z.start();   // absent: nothing
        QVERIFY(link.writes.isEmpty());
        z.detect();
        QCOMPARE(z.state(), ZwiftHubLink::State::Available);
        QCOMPARE(z.hubState(), int(SensorHub::HubAvailable));
        z.update(params(1.5, 2.40));   // remembered, not sent before the handshake
        QVERIFY(link.writes.isEmpty());
        z.start();
        QVERIFY(link.subscribed.contains(kAsync));
        QVERIFY(link.subscribed.contains(kTx));
        QCOMPARE(z.state(), ZwiftHubLink::State::Handshake);
        QCOMPARE(link.writesTo(kRx).first(), zhub::handshake());
        QTRY_COMPARE(z.state(), ZwiftHubLink::State::Active);
        const QVector<QByteArray> w = link.writesTo(kRx);
        QCOMPARE(w.at(1), zhub::startStream());
        // then everything at once: Physical, Simulation, settings request
        QCOMPARE(w.at(2), hex("042a0a10c0bb0120e80728cc3a"));
        QCOMPARE(w.at(3), hex("04220b080010ac0218ec27209003"));
        QCOMPARE(w.at(4), zhub::encodeRequest(512));
        QVERIFY(z.lastRidingData());
        QCOMPARE(z.lastRidingData()->power, 180);
        // the settings echo is decoded and kept
        zhub::Settings st;
        st.gearRatio = 2.4;
        st.inclinePercent = 1.5;
        link.notify(kAsync, zhub::encodeSettingsEcho(st));
        QVERIFY(z.lastSettings());
        QCOMPARE(*z.lastSettings()->gearRatio, 2.4);
    }

    void handshakeTimeoutFallsBack()
    {
        FakeGattLink link(victoryFtms() + hubChars(), { u16(0x1826), u16(0xFC82) });
        hubResponder(&link, false);   // silent trainer
        link.connectToDevice();
        ZwiftHubLink z(&link);
        z.setTimingForTesting(40, 40, 400, 100);
        QObject::connect(&link, &GattLink::characteristicChanged, &z, &ZwiftHubLink::handleNotification);
        QSignalSpy failed(&z, &ZwiftHubLink::failed);
        z.detect();
        z.start();
        QTRY_COMPARE(z.state(), ZwiftHubLink::State::Failed);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(z.hubState(), int(SensorHub::HubFailed));
        QCOMPARE(z.failureReason(), QStringLiteral("no answer to the handshake"));
        // two handshake attempts, then the stream was tried anyway
        int rideOn = 0;
        for (const QByteArray &d : link.writesTo(kRx))
            rideOn += zhub::isRideOn(d) ? 1 : 0;
        QCOMPARE(rideOn, ZwiftHubLink::kHandshakeAttempts);
        QCOMPARE(link.writesTo(kRx).constLast(), zhub::startStream());
        // nothing is sent while failed
        const int before = link.writes.size();
        z.update(params(3, 3.03));
        QCOMPARE(link.writes.size(), before);
    }

    void keepAliveRetriesThenFails()
    {
        FakeGattLink link(victoryFtms() + hubChars(), { u16(0x1826), u16(0xFC82) });
        hubResponder(&link);
        link.connectToDevice();
        ZwiftHubLink z(&link);
        z.setTimingForTesting(80, 80, 60, 1000);
        QObject::connect(&link, &GattLink::characteristicChanged, &z, &ZwiftHubLink::handleNotification);
        z.detect();
        z.start();
        QTRY_COMPARE(z.state(), ZwiftHubLink::State::Active);
        // the responder answers the stream re-request: stays active
        QTest::qWait(100);
        QCOMPARE(z.state(), ZwiftHubLink::State::Active);
        QVERIFY(countPrefix(&link, zhub::OutStartStream) >= 2);
        // now the trainer goes silent for good
        link.responder = nullptr;
        QTRY_COMPARE(z.state(), ZwiftHubLink::State::Failed);
        QCOMPARE(z.failureReason(), QStringLiteral("the trainer stopped sending Zwift data"));
    }

    void onlyChangedFieldsAndRateLimits()
    {
        FakeGattLink link(victoryFtms() + hubChars(), { u16(0x1826), u16(0xFC82) });
        hubResponder(&link);
        link.connectToDevice();
        qint64 now = 0;
        ZwiftHubLink z(&link);
        z.setClockForTesting([&now] { return now; });
        z.setTimingForTesting(80, 80, 100000, 100000);   // no timer refresh here: tick() by hand
        QObject::connect(&link, &GattLink::characteristicChanged, &z, &ZwiftHubLink::handleNotification);
        z.detect();
        z.update(params(1.0, 2.40));
        z.start();
        QTRY_COMPARE(z.state(), ZwiftHubLink::State::Active);
        const int base = commands(&link).size();
        QCOMPARE(base, 2);   // Physical + Simulation (all fields)

        // gear change: Physical with only the gear ratio, at once (no rate limit)
        now = 100;
        z.update(params(1.0, 2.61));
        QVector<zhub::Command> c = commands(&link);
        QCOMPARE(c.size(), base + 1);
        QVERIFY(c.last().physical && !c.last().simulation);
        QCOMPARE(*c.last().physical->gearRatio, 2.61);
        QVERIFY(!c.last().physical->riderKg && !c.last().physical->bikeKg);
        now = 150;
        z.update(params(1.0, 2.82));
        QCOMPARE(commands(&link).size(), base + 2);   // every gear change goes out

        // incline: < 0.1 % -> nothing; >= 0.1 % but within 1 s of the last -> later
        now = 300;
        z.update(params(1.05, 2.82));
        QCOMPARE(commands(&link).size(), base + 2);
        now = 600;
        z.update(params(1.3, 2.82));
        QCOMPARE(commands(&link).size(), base + 2);   // < kMinSimIntervalMs since the first Simulation
        now = 1000;
        z.update(params(1.3, 2.82));
        c = commands(&link);
        QCOMPARE(c.size(), base + 3);
        QVERIFY(c.last().simulation && !c.last().physical);
        QCOMPARE(*c.last().simulation->inclinePercent, 1.3);
        QVERIFY(!c.last().simulation->cw && !c.last().simulation->crr && !c.last().simulation->windMps);
        // refresh: the incline again after kRefreshMs, nothing else
        now = 1000 + ZwiftHubLink::kRefreshMs;
        z.tick();
        c = commands(&link);
        QCOMPARE(c.size(), base + 4);
        QCOMPARE(*c.last().simulation->inclinePercent, 1.3);
        QVERIFY(!c.last().simulation->crr);
        // weights / CWa: only the changed field
        now += 100;
        HubSimulation p = params(1.3, 2.82);
        p.riderKg = 80;
        p.cw = 0.40;
        z.update(p);
        c = commands(&link);
        QCOMPARE(c.size(), base + 6);
        QVERIFY(c.at(c.size() - 2).physical);
        QCOMPARE(*c.at(c.size() - 2).physical->riderKg, 80.0);
        QVERIFY(!c.at(c.size() - 2).physical->gearRatio);
        QVERIFY(c.last().simulation && c.last().simulation->cw && !c.last().simulation->inclinePercent);
        // settings echo requests are rate limited (kEchoIntervalMs)
        QVERIFY(countPrefix(&link, zhub::OutRequest) >= 2);
        QVERIFY(countPrefix(&link, zhub::OutRequest) <= 4);
    }

    // --- BleTrainer + SensorHub + RideResistance + VirtualGears -------------------

    // No Zwift Click: plain FTMS gradient even though the trainer has the
    // Zwift service; the handshake never starts.
    void noClickMeansPlainFtmsGradient()
    {
        TrainerRig r;
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears gears(&db, &profiles);
        gears.setHub(&r.hub);
        r.connect();
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubAvailable));
        QTest::qWait(150);
        QCOMPARE(r.link->writesTo(kRx).size(), 0);   // no handshake
        QVERIFY(!r.hub.zwiftHubWanted());
        QVERIFY(!gears.enabled());
        QCOMPARE(gears.shiftingMode(), QStringLiteral("bike"));
        QCOMPARE(gears.shiftingStatus(), QStringLiteral("Shifting: your bike's gears"));
        QVERIFY(gears.zwiftAvailable());
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()), RideResistance::Path::Simulation);
        // even a (hypothetically) active hub never makes it a hub path without a Click
        SensorHub other;
        other.setTrainerZwiftHub(SensorHub::HubActive, QString());
        QCOMPARE(RideResistance::choose(false, false, &other), RideResistance::Path::Simulation);
        // control was granted with no session command: the neutral state
        // (0x11 flat 0 %) went out
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), 1);
        RideResistance res(&r.hub);
        QVERIFY(res.sendGrade(2.5, true, road::TrainerModel()));
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), 2);
    }

    // Click connected + Zwift service: hub protocol automatically; FTMS
    // still holds control, but no 0x11 / 0x05 while the hub path is active;
    // a gear change goes out at once. Click gone: back to FTMS.
    void clickActivatesHubAutomatically()
    {
        TrainerRig r;
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears gears(&db, &profiles);
        gears.setHub(&r.hub);
        r.connect();
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubAvailable));
        QSignalSpy shifting(&gears, &VirtualGears::shiftingChanged);
        gears.setClickConnected(true);
        QVERIFY(r.hub.zwiftHubWanted());
        QCOMPARE(gears.shiftingMode(), QStringLiteral("ftms"));   // handshake running: FTMS gradient meanwhile
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubActive));
        QCOMPARE(gears.shiftingMode(), QStringLiteral("zwift"));
        QCOMPARE(gears.shiftingStatus(), QStringLiteral("Virtual shifting: Zwift protocol"));
        QVERIFY(shifting.count() >= 2);
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()), RideResistance::Path::GearHub);

        // the session path: RideResistance::sendHub
        RideResistance res(&r.hub);
        RideResistance::GearDrive d;
        d.road = gears.roadModel(1.5);
        d.virtualRatio = gears.gearRatio();
        d.riderKg = gears.riderWeightKg();
        const int simBefore = r.link->countOp(kCp, kOpSim), powerBefore = r.link->countOp(kCp, kOpPower);
        res.sendHub(d);
        QTRY_VERIFY(!commands(r.link).isEmpty());
        QTRY_VERIFY(r.trainer->trainerMode() == QStringLiteral("zwift"));
        const HubSimulation sent = RideResistance::hubSimulationFor(d);
        QCOMPARE(sent.inclinePercent, 1.5);
        QCOMPARE(sent.gearRatio, 2.40);
        QCOMPARE(sent.riderKg, gears.riderWeightKg());
        QCOMPARE(sent.bikeKg, gears.setup().bikeWeightKg);
        QCOMPARE(sent.cw, road::trainerModelFor(d.road).cwKgPerM);
        // shift: a Physical with the new ratio at once
        const int cmds = commands(r.link).size();
        QVERIFY(gears.shiftUp());
        d.virtualRatio = gears.gearRatio();
        res.sendHub(d);
        QVector<zhub::Command> c = commands(r.link);
        QCOMPARE(c.size(), cmds + 1);
        QCOMPARE(*c.last().physical->gearRatio, 2.61);
        // a few seconds of riding: refreshes, but never FTMS 0x11 / 0x05
        QTest::qWait(300);
        for (int i = 0; i < 5; ++i)
            res.sendHub(d);
        QCOMPARE(r.link->countOp(kCp, kOpSim), simBefore);
        QCOMPARE(r.link->countOp(kCp, kOpPower), powerBefore);
        QVERIFY(r.hub.hubSimulationActive());

        // the Click goes away: hub stopped, plain FTMS gradient again
        gears.setClickConnected(false);
        QVERIFY(!r.hub.zwiftHubWanted());
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubAvailable));
        QCOMPARE(gears.shiftingMode(), QStringLiteral("bike"));
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()), RideResistance::Path::Simulation);
        QVERIFY(res.sendGrade(1.5, false, road::trainerModelFor(d.road)));   // leaving the hub path: at once
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), simBefore + 1);

        // advanced override "off": never the Zwift protocol, even with a Click
        gears.setHubOverride(QStringLiteral("off"));
        gears.setClickConnected(true);
        QVERIFY(!r.hub.zwiftHubWanted());
        QCOMPARE(gears.shiftingMode(), QStringLiteral("ftms"));
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()), RideResistance::Path::GearSimulation);
        gears.setHubOverride(QStringLiteral("auto"));
        QVERIFY(r.hub.zwiftHubWanted());
    }

    // Click connected, trainer silent on the Zwift service: FTMS gradient
    // virtual gears (simulation, never ERG).
    void failedHandshakeFallsBackToFtmsGradient()
    {
        TrainerRig r(true, false);
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears gears(&db, &profiles);
        gears.setHub(&r.hub);
        r.connect();
        gears.setClickConnected(true);
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubFailed));
        QCOMPARE(gears.shiftingMode(), QStringLiteral("ftms"));
        QVERIFY(gears.shiftingProblem());
        QVERIFY(gears.shiftingDetail().startsWith(QStringLiteral("Zwift protocol failed")));
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()),
                 RideResistance::Path::GearSimulation);   // not GearErg
        QVERIFY(r.trainer->zwiftHubStatus().contains(QStringLiteral("handshake")));
    }

    // A trainer without the Zwift service + Click: FTMS gradient virtual gears.
    void clickWithoutHubUsesFtmsGradient()
    {
        TrainerRig r(false);
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears gears(&db, &profiles);
        gears.setHub(&r.hub);
        r.connect();
        gears.setClickConnected(true);
        QTest::qWait(150);
        QCOMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubNone));
        QCOMPARE(r.link->writesTo(kRx).size(), 0);
        QCOMPARE(gears.shiftingMode(), QStringLiteral("ftms"));
        QCOMPARE(gears.shiftingStatus(), QStringLiteral("Virtual shifting: FTMS gradient"));
        QVERIFY(!gears.zwiftAvailable());
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()), RideResistance::Path::GearSimulation);
        // the 128-bit Zwift service is detected like 0xFC82
        TrainerRig r128(true, true, false);
        r128.connect();
        QTRY_COMPARE(r128.hub.trainerZwiftHubState(), int(SensorHub::HubAvailable));
    }

    // The virtual trainer with WATT_SIM_TRAINER_ZWIFT: the real driver end to
    // end, the trainer simulates with the hub's incline and gear.
    void virtualTrainerZwiftCapability()
    {
        SimulatedTrainer t;
        t.setNoise(false);
        t.setZwiftHubCapability(true);
        t.connectDevice();
        QTRY_COMPARE(t.zwiftHubState(), int(SensorHub::HubAvailable));
        t.setZwiftHubEnabled(true);
        QTRY_COMPARE(t.zwiftHubState(), int(SensorHub::HubActive));
        t.setHubSimulation(params(4.0, 3.03));
        QTRY_VERIFY(t.hubSimulating());
        QCOMPARE(t.hubGearRatio(), 3.03);
        QCOMPARE(t.trainerMode(), QStringLiteral("zwift"));
        QVERIFY(t.controlStatus().startsWith(QStringLiteral("Zwift protocol")));
        QTRY_VERIFY(t.zwiftHub()->lastSettings().has_value());
        QCOMPARE(*t.zwiftHub()->lastSettings()->gearRatio, 3.03);
        // FTMS again
        t.setSimulation(1.0, 0, 0.004, 0.39);
        QVERIFY(!t.hubSimulating());
        t.setZwiftHubEnabled(false);
        QCOMPARE(t.zwiftHubState(), int(SensorHub::HubAvailable));
        // =fail: the handshake is never answered
        SimulatedTrainer silent;
        silent.setZwiftHubCapability(true, false);
        silent.zwiftHub()->setTimingForTesting(60, 60, 1000, 500);
        silent.connectDevice();
        silent.setZwiftHubEnabled(true);
        QTRY_COMPARE(silent.zwiftHubState(), int(SensorHub::HubFailed));
    }
};

QTEST_GUILESS_MAIN(TestZwiftHub)
#include "tst_zwifthub.moc"
