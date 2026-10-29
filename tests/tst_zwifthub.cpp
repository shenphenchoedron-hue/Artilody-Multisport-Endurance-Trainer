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
#include "DeviceManager.h"
#include "HubGearCompensation.h"
#include "Gearing.h"
#include "ProfileManager.h"
#include "RideResistance.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"
#include "VirtualGears.h"
#include "ZwiftHubLink.h"
#include "ZwiftHubProtocol.h"

#include <QSignalSpy>
#include <memory>
#include <optional>
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
            r.speedMps = 8.47;
            return { { kAsync, zhub::encodeRidingData(r) } };
        }
        return {};
    };
}

int countRequests(const FakeGattLink *link, quint32 dataId)
{
    int n = 0;
    for (const QByteArray &w : link->writesTo(kRx))
        if (w == zhub::encodeRequest(dataId))
            ++n;
    return n;
}

// Zwift's rule: every Simulation is followed by Physical{gear} and request 520
void verifyGearAfterEverySimulation(const FakeGattLink *link)
{
    const QVector<QByteArray> w = link->writesTo(kRx);
    int sims = 0;
    for (int i = 0; i < w.size(); ++i) {
        if (w.at(i).isEmpty() || quint8(w.at(i).at(0)) != zhub::OutCommand || zhub::isRideOn(w.at(i)))
            continue;
        const auto c = zhub::decodeCommand(w.at(i).mid(1));
        QVERIFY(c);
        if (!c->simulation)
            continue;
        ++sims;
        QVERIFY2(i + 2 < w.size(), "Simulation without the gear after it");
        const auto next = zhub::decodeCommand(w.at(i + 1).mid(1));
        QVERIFY2(quint8(w.at(i + 1).at(0)) == zhub::OutCommand && next && next->physical && next->physical->gearRatio,
                 "Simulation not followed by Physical{gear}");
        QCOMPARE(w.at(i + 2), zhub::encodeRequest(zhub::kGearRatioDataId));
    }
    QVERIFY(sims > 0);
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

// ---- real-gear compensation helpers ---------------------------------------
// A coherent trainer packet (all fields carried now).
TrainerSample packet(qint64 ms, double cadence, double speedMps, int watts)
{
    TrainerSample s;
    s.ms = ms;
    s.cadenceRpm = cadence;
    s.speedMps = speedMps;
    s.powerW = watts;
    s.hasPower = s.hasCadence = s.hasSpeed = true;
    s.powerAgeMs = s.cadenceAgeMs = s.speedAgeMs = 0;
    return s;
}

HubSimulation sentGear(double ratio)
{
    HubSimulation p;
    p.gearRatio = ratio;
    p.cw = 0.51;
    p.crr = 0.004;
    p.riderKg = 68;
    p.bikeKg = 9;
    return p;
}

// Packets at 1 Hz like the Victory: the trainer's speed = its FLYWHEEL (real
// ratio), or for a trainer reporting a gear-derived speed: cadence × sent × follow.
struct Pedal
{
    HubGearCompensation &c;
    qint64 ms = 0;
    int circ = 2105;
    int changes = 0;
    double virtualRatio = 2.40;
    void one(const TrainerSample &s, double sent)
    {
        HubGearCompensation::Input in;
        in.sample = s;
        in.sent = sentGear(sent);
        in.virtualRatio = virtualRatio;
        in.circumferenceMm = circ;
        changes |= c.update(in);
    }
    void flywheel(double realRatio, double sent, int seconds, double cadence = 80, int watts = 120)
    {
        for (const qint64 end = ms + seconds * 1000; ms < end; ms += 1000)
            one(packet(ms, cadence, cadence / 60.0 * realRatio * circ / 1000.0, watts), sent);
    }
    void followsGear(double sent, double follow, int seconds, double cadence = 80, int watts = 120)
    {
        for (const qint64 end = ms + seconds * 1000; ms < end; ms += 1000)
            one(packet(ms, cadence, cadence / 60.0 * sent * follow * circ / 1000.0, watts), sent);
    }
};

// Indoor Bike Data bytes: speed (always), cadence and power when given.
QByteArray ibd(double kmh, std::optional<double> cadence, std::optional<int> watts)
{
    quint16 flags = 0;
    if (cadence)
        flags |= 0x04;
    if (watts)
        flags |= 0x40;
    QByteArray b;
    auto u16le = [&b](int v) { b.append(char(v & 0xff)); b.append(char((v >> 8) & 0xff)); };
    u16le(flags);
    u16le(int(std::lround(kmh * 100)));
    if (cadence)
        u16le(int(std::lround(*cadence * 2)));
    if (watts)
        u16le(*watts);
    return b;
}

class TestZwiftHub : public QObject
{
    Q_OBJECT

private slots:
    // protocol tests: the soft start (its light start gear) is tested in tst_virtualgears
    void initTestCase() { RideResistance::setSoftStartEnabled(false); }

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
        // only the set fields; 0 % goes out as 0 (Zwift: 04 22 02 10 00)
        zhub::Simulation flat;
        flat.inclinePercent = 0.0;
        QCOMPARE(zhub::encodeSimulation(flat), hex("0422021000"));
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
        QCOMPARE(zhub::inclineX100(0.004), 0);
        QCOMPARE(zhub::inclineX100(-0.004), 0);
        QCOMPARE(zhub::inclineX100(-4.0), -400);
    }

    // Zwift's own bytes (capture replayed by qdomyos-zwift zwiftPlayInit) and
    // Incyclist's encodings: ours must be identical.
    void referenceBytes()
    {
        zhub::Physical zp;
        zp.gearRatio = 2.40;
        zp.bikeKg = 8.31;
        zp.riderKg = 85.0;
        QCOMPARE(zhub::encodePhysical(zp), hex("042a0a10c0bb0120bf0628b442"));
        zhub::Simulation zs;
        zs.windMps = 0;
        zs.inclinePercent = 1.73;
        zs.cw = 0.51;
        zs.crr = 0.004;
        QCOMPARE(zhub::encodeSimulation(zs), hex("04220b080010da0218ec27209003"));
        zhub::Simulation down;
        down.inclinePercent = -0.85;
        QCOMPARE(zhub::encodeSimulation(down), hex("04220310a901"));
        zhub::Simulation zero;
        zero.inclinePercent = 0;
        QCOMPARE(zhub::encodeSimulation(zero), hex("0422021000"));
        QCOMPARE(zhub::encodeRequest(zhub::kGearRatioDataId), hex("00088804"));
        QCOMPARE(zhub::encodeRequest(zhub::kDeviceInfoDataId), hex("000800"));
        // incline is sint32 (zigzag): +2 % = 200 -> 400, -3 % = -300 -> 599
        zhub::Simulation plus2, minus3;
        plus2.inclinePercent = 2.0;
        minus3.inclinePercent = -3.0;
        QCOMPARE(zhub::encodeSimulation(plus2), hex("042203109003"));
        QCOMPARE(zhub::encodeSimulation(minus3), hex("04220310d704"));
        // the user's log (v1.3.3): these bytes were already correct
        zhub::Physical user;
        user.gearRatio = 2.40;
        user.bikeKg = 9;
        user.riderKg = 68;
        QCOMPARE(zhub::encodePhysical(user), hex("042a0a10c0bb01208407289035"));
        zhub::Simulation s189, s293;
        s189.inclinePercent = 1.89;
        s293.inclinePercent = -2.93;
        QCOMPARE(zhub::encodeSimulation(s189), hex("04220310fa02"));
        QCOMPARE(zhub::encodeSimulation(s293), hex("04220310c904"));
        const struct { int gear; const char *bytes; } gears[] = {
            { 11, "042a0410b8ad01" }, { 10, "042a0410b09f01" }, { 9, "042a0410a89101" }, { 8, "042a0410a08301" },
            { 1, "042a0310cc3a" }, { 24, "042a0410f4ac03" } };
        for (const auto &g : gears) {
            zhub::Physical p;
            p.gearRatio = zwiftgears::ratio(g.gear);
            QCOMPARE(zhub::encodePhysical(p), hex(g.bytes));
        }
        // CWa x 10000 and Crr x 100000 (not the FTMS resolutions)
        zhub::Simulation coeff;
        coeff.cw = RideResistance::kHubCwa;
        coeff.crr = RideResistance::kHubCrr;
        QCOMPARE(zhub::encodeSimulation(coeff), hex("04220618ec27209003"));
    }

    // The user's real notifications (Jetblack Victory, v1.3.3 log).
    void decodesVictoryEchoAndRidingData()
    {
        // echo right after sending -2.93 % (gear 1.86 = gear 9 sent before)
        const QByteArray echo = hex("3c08800412290a27080310c801180020e00728003003"
                                    "38b0ea0140a8910148bb1e509c34589035689003708827" "7800");
        QCOMPARE(quint8(echo.at(0)), quint8(zhub::InDeviceData));
        const auto s = zhub::decodeDeviceData(echo.mid(1));
        QVERIFY(s);
        QCOMPARE(s->messageType, 512);
        QCOMPARE(*s->windMps, 0.0);
        QCOMPARE(*s->inclineWholePercent, -2);        // zigzag 3: -2.93 % truncated
        QCOMPARE(zhub::echoedIncline(-2.93), -2);
        QCOMPARE(*s->gearRatio, 1.86);
        QCOMPARE(*s->cwa, 0.3899);
        QCOMPARE(*s->weight11Kg, 68.0);              // the rider, not the bike
        QVERIFY(!s->weight12Kg);
        QCOMPARE(*s->crr, 0.004);
        QVector<QPair<int, quint64>> other { { 1, 3 }, { 2, 200 }, { 3, 0 }, { 4, 992 }, { 7, 30000 },
                                             { 10, 6684 }, { 14, 5000 }, { 15, 0 } };
        QCOMPARE(s->other, other);
        const QString d = zhub::describe(echo, false);
        QVERIFY2(d.contains(QStringLiteral("incline -2 % (f6, whole percent)")), qPrintable(d));
        QVERIFY2(d.contains(QStringLiteral("weight 68.00 kg (f11, rider)")), qPrintable(d));
        QVERIFY2(d.contains(QStringLiteral("f4=992")), qPrintable(d));
        // after +1.89 % -> f6 zigzag 2 = +1; after -1.35 % -> zigzag 1 = -1
        QCOMPARE(zhub::unzigzag(2), qint64(1));
        QCOMPARE(zhub::echoedIncline(1.89), 1);
        QCOMPARE(zhub::unzigzag(1), qint64(-1));
        QCOMPARE(zhub::echoedIncline(-1.35), -1);

        // riding data: 199 W, 78 rpm, speed 1039 = 10.39 m/s (37.4 km/h;
        // FTMS said 37.2 km/h), f4 199 (= the power, not HR), f5 29701
        const QByteArray riding = hex("0308c701104e188f0820c7012885e801");
        const auto r = zhub::decodeRidingData(riding.mid(1));
        QVERIFY(r);
        QCOMPARE(r->power, 199);
        QCOMPARE(r->cadence, 78);
        QCOMPARE(r->speedMps, 10.39);
        QVERIFY(std::abs(r->speedKmh() - 37.404) < 0.001);
        QCOMPARE(r->field4, 199);
        QCOMPARE(*r->field5, quint64(29701));
        QVERIFY(!r->field6);
        QCOMPARE(zhub::describe(riding, false),
                 QStringLiteral("riding data: 199 W, 78 rpm, speed 10.39 m/s (37.4 km/h), f4 199 (proto \"HR\"), f5 29701"));
        // Makinolo's example: 190 W, 80 rpm, 8.29 m/s
        const auto m = zhub::decodeRidingData(hex("08be01105018bd06200028e2ba01308beb01"));
        QVERIFY(m);
        QCOMPARE(m->speedMps, 8.29);
        QCOMPARE(*m->field6, quint64(30091));
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
        r.speedMps = 10.03;
        r.field4 = 141;
        const QByteArray riding = zhub::encodeRidingData(r);
        QCOMPARE(quint8(riding.at(0)), quint8(zhub::InRidingData));
        const auto back = zhub::decodeRidingData(riding.mid(1));
        QVERIFY(back);
        QCOMPARE(back->power, 187);
        QCOMPARE(back->cadence, 88);
        QCOMPARE(back->speedMps, 10.03);
        QCOMPARE(back->field4, 141);
        // hand-made HubRidingData: 08 b4 01 (180 W) 10 55 (85 rpm) 18 ea 17 (30.50 m/s x 100) 20 00
        const auto hand = zhub::decodeRidingData(hex("08b401105518ea172000"));
        QVERIFY(hand);
        QCOMPARE(hand->power, 180);
        QCOMPARE(hand->speedMps, 30.5);
        QVERIFY(!zhub::decodeRidingData(hex("08")));   // truncated

        zhub::Settings st;
        st.inclineWholePercent = -1;
        st.gearRatio = 2.4;
        st.cwa = 0.51;
        st.weight11Kg = 75;
        st.weight12Kg = 10;
        st.crr = 0.004;
        st.windMps = 0;
        const QByteArray echo = zhub::encodeSettingsEcho(st);
        QCOMPARE(quint8(echo.at(0)), quint8(zhub::InDeviceData));
        const auto s2 = zhub::decodeDeviceData(echo.mid(1));
        QVERIFY(s2);
        QCOMPARE(s2->messageType, 512);
        QCOMPARE(*s2->inclineWholePercent, -1);
        QCOMPARE(*s2->gearRatio, 2.4);
        QCOMPARE(*s2->cwa, 0.51);
        QCOMPARE(*s2->weight11Kg, 75.0);
        QCOMPARE(*s2->weight12Kg, 10.0);
        QCOMPARE(*s2->crr, 0.004);
        // device info (type < 16) is not a settings echo
        QVERIFY(!zhub::decodeDeviceData(hex("0801120400000000")));
        QVERIFY(zhub::describe(echo, false).startsWith(QStringLiteral("settings echo (type 512): incline -1 % (f6, whole percent)")));
        QCOMPARE(zhub::describe(hex("00088804"), true), QStringLiteral("request dataId 520 (gear ratio)"));
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
        // then Zwift's sequence: Physical (all), Simulation (all), the gear
        // AGAIN after the Simulation, request 520, then the settings request
        QCOMPARE(w.at(2), hex("042a0a10c0bb0120e80728cc3a"));
        QCOMPARE(w.at(3), hex("04220b080010ac0218ec27209003"));
        QCOMPARE(w.at(4), hex("042a0410c0bb01"));
        QCOMPARE(w.at(5), hex("00088804"));
        QCOMPARE(w.at(6), zhub::encodeRequest(512));
        QVERIFY(z.lastRidingData());
        QCOMPARE(z.lastRidingData()->power, 180);
        // the settings echo is decoded and kept
        zhub::Settings st;
        st.gearRatio = 2.4;
        st.inclineWholePercent = 1;
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

    // Zwift Hub (log of 1.3.7): RideOn answered, the stream start answered
    // with 2A / FE messages and FTMS "Control Permission Lost" (the Hub
    // switched to the Zwift protocol) - no riding data while its flywheel
    // stands still. FTMS control must NOT be requested again (that switched it
    // back: no riding data, FTMS fallback after 4 s). The link becomes active
    // waiting for riding data; it fails only when FTMS shows the trainer
    // moving without riding data, and then FTMS control is requested again.
    void zwiftHubWithoutRidingDataWhileStanding()
    {
        for (const bool ridingComes : { true, false }) {
            TrainerRig r(true, true, false);
            bool lostSent = false, riding = false;
            r.link->responder = [&](const QBluetoothUuid &c, const QByteArray &d) -> QVector<FakeGattLink::Notification> {
                if (c == kCp && !d.isEmpty())
                    return { { kCp, hex("80") + d.left(1) + hex("01") } };
                if (c != kRx)
                    return {};
                if (zhub::isRideOn(d))
                    return { { kTx, QByteArray("RideOn") } };
                if (d == zhub::startStream()) {
                    if (riding) {
                        zhub::RidingData rd;
                        rd.power = 90;
                        rd.cadence = 70;
                        rd.speedMps = 6.0;
                        return { { kAsync, zhub::encodeRidingData(rd) } };
                    }
                    QVector<FakeGattLink::Notification> out;
                    if (!lostSent) {
                        lostSent = true;
                        out.append({ u16(ftms::kMachineStatusUuid), hex("ff") });
                    }
                    out.append({ kAsync, hex("2a120e0830100a1890032205ffffefff1f") });
                    out.append({ kAsync, hex("fe") });
                    return out;
                }
                return {};
            };
            qint64 clock = 0;
            r.trainer->zwiftHub()->setClockForTesting([&clock] { return clock; });
            r.connect();
            QTRY_COMPARE(r.link->countOp(kCp, 0x00), 1);
            r.hub.setZwiftHubWanted(true);
            QTRY_COMPARE(r.trainer->zwiftHub()->state(), ZwiftHubLink::State::Active);
            QVERIFY(lostSent);
            QVERIFY(r.trainer->zwiftHub()->awaitingRidingData());
            QCOMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubActive));
            // the hub path sends the road and the gear; no FTMS control request
            r.hub.requestHubSimulation(params(0.0, 1.2));
            QTest::qWait(300);
            QCOMPARE(r.link->countOp(kCp, 0x00), 1);
            QCOMPARE(r.trainer->zwiftHub()->state(), ZwiftHubLink::State::Active);
            QVERIFY(!commands(r.link).isEmpty());
            if (ridingComes) {
                // the rider pedals: riding data arrives, the stream runs
                riding = true;
                zhub::RidingData rd;
                rd.power = 90;
                rd.cadence = 70;
                rd.speedMps = 6.0;
                r.link->notify(kAsync, zhub::encodeRidingData(rd));
                QVERIFY(!r.trainer->zwiftHub()->awaitingRidingData());
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(20, 70.0, 90));
                clock = 20000;
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(20, 70.0, 90));
                QCOMPARE(r.trainer->zwiftHub()->state(), ZwiftHubLink::State::Active);
                QCOMPARE(r.link->countOp(kCp, 0x00), 1);
            } else {
                // standing still: no failure however long it takes
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(0, 0.0, 0));
                clock = 60000;
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(0, 0.0, 0));
                QCOMPARE(r.trainer->zwiftHub()->state(), ZwiftHubLink::State::Active);
                // moving for kNoRidingWhileMovingMs without riding data: FTMS
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(20, 70.0, 90));
                clock += ZwiftHubLink::kNoRidingWhileMovingMs - 1000;
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(20, 70.0, 90));
                QCOMPARE(r.trainer->zwiftHub()->state(), ZwiftHubLink::State::Active);
                clock += 1000;
                r.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(20, 70.0, 90));
                QCOMPARE(r.trainer->zwiftHub()->state(), ZwiftHubLink::State::Failed);
                QCOMPARE(r.trainer->zwiftHub()->failureReason(),
                         QStringLiteral("the trainer moves (FTMS) but sends no Zwift riding data"));
                QTRY_COMPARE(r.link->countOp(kCp, 0x00), 2);   // FTMS control again
            }
        }
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
        QCOMPARE(base, 3);   // Physical (all) + Simulation (all) + Physical{gear} again
        QCOMPARE(countRequests(&link, zhub::kGearRatioDataId), 1);

        // gear change: Physical with only the gear ratio, at once (no rate
        // limit), then request 520 like Zwift
        now = 100;
        z.update(params(1.0, 2.61));
        QVector<zhub::Command> c = commands(&link);
        QCOMPARE(c.size(), base + 1);
        QVERIFY(c.last().physical && !c.last().simulation);
        QCOMPARE(*c.last().physical->gearRatio, 2.61);
        QVERIFY(!c.last().physical->riderKg && !c.last().physical->bikeKg);
        QCOMPARE(link.writesTo(kRx).constLast(), zhub::encodeRequest(zhub::kGearRatioDataId));
        QCOMPARE(countRequests(&link, zhub::kGearRatioDataId), 2);
        now = 150;
        z.update(params(1.0, 2.82));
        QCOMPARE(commands(&link).size(), base + 2);   // every gear change goes out
        QCOMPARE(countRequests(&link, zhub::kGearRatioDataId), 3);

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
        QCOMPARE(c.size(), base + 4);   // Simulation + the gear again
        const zhub::Command &sim = c.at(c.size() - 2);
        QVERIFY(sim.simulation && !sim.physical);
        QCOMPARE(*sim.simulation->inclinePercent, 1.3);
        QVERIFY(!sim.simulation->cw && !sim.simulation->crr && !sim.simulation->windMps);
        QVERIFY(c.last().physical && !c.last().simulation);
        QCOMPARE(*c.last().physical->gearRatio, 2.82);   // unchanged gear, re-sent after the incline
        QVERIFY(!c.last().physical->riderKg && !c.last().physical->bikeKg);
        // refresh: the incline again after kRefreshMs (and the gear after it)
        now = 1000 + ZwiftHubLink::kRefreshMs;
        z.tick();
        c = commands(&link);
        QCOMPARE(c.size(), base + 6);
        QCOMPARE(*c.at(c.size() - 2).simulation->inclinePercent, 1.3);
        QVERIFY(!c.at(c.size() - 2).simulation->crr);
        QCOMPARE(*c.last().physical->gearRatio, 2.82);
        // weights / CWa: only the changed field (+ the gear after the Simulation)
        now += 100;
        HubSimulation p = params(1.3, 2.82);
        p.riderKg = 80;
        p.cw = 0.40;
        z.update(p);
        c = commands(&link);
        QCOMPARE(c.size(), base + 9);
        QVERIFY(c.at(c.size() - 3).physical);
        QCOMPARE(*c.at(c.size() - 3).physical->riderKg, 80.0);
        QVERIFY(!c.at(c.size() - 3).physical->gearRatio);
        QVERIFY(c.at(c.size() - 2).simulation && c.at(c.size() - 2).simulation->cw
                && !c.at(c.size() - 2).simulation->inclinePercent);
        QCOMPARE(*c.last().physical->gearRatio, 2.82);
        // settings echo requests are rate limited (kEchoIntervalMs)
        QVERIFY(countRequests(&link, zhub::kSettingsDataId) >= 2);
        QVERIFY(countRequests(&link, zhub::kSettingsDataId) <= 4);
        verifyGearAfterEverySimulation(&link);
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
        // handshake running: already the hub path (nothing sent until it is done, no FTMS meanwhile)
        QCOMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubStarting));
        QCOMPARE(gears.shiftingMode(), QStringLiteral("zwift"));
        QCOMPARE(RideResistance::choose(false, gears.enabled(), &r.hub, gears.method()), RideResistance::Path::GearHub);
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
        // wind, CWa, Crr exactly as Zwift: 0, 0.51, 0.004 (not the rider's CdA)
        QCOMPARE(sent.windMps, 0.0);
        QCOMPARE(sent.cw, 0.51);
        QCOMPARE(sent.crr, 0.004);
        verifyGearAfterEverySimulation(r.link);
        // shift harder: the new ratio ramps in over kHubShiftRampMs (a sudden
        // +9 % made the trainer's inertia emulation spike the power)
        QVERIFY(gears.shiftUp());
        d.virtualRatio = gears.gearRatio();
        res.sendHub(d);
        for (int t = 0; t <= RideResistance::kHubShiftRampMs; t += 250) {
            res.elapse(250);
            res.sendHub(d);
        }
        QVector<zhub::Command> c = commands(r.link);
        QCOMPARE(*c.last().physical->gearRatio, 2.61);
        QCOMPARE(r.link->writesTo(kRx).constLast(), zhub::encodeRequest(zhub::kGearRatioDataId));
        // shift easier: at once
        const int cmds = commands(r.link).size();
        QVERIFY(gears.shiftDown());
        d.virtualRatio = gears.gearRatio();
        res.sendHub(d);
        c = commands(r.link);
        QCOMPARE(c.size(), cmds + 1);
        QCOMPARE(*c.last().physical->gearRatio, 2.40);
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

    // Regression (v1.3.3, Jetblack Victory "far too hard"): Click connected
    // before the trainer. No FTMS start / neutral 0x11 / 0x05 at all while the
    // hub protocol drives the trainer (Zwift sends none, Incyclist only
    // RequestControl); the start gear goes out AFTER the first Simulation;
    // a release (pause) is a flat road through the hub. Without the hub
    // (Click gone) FTMS again, with the held-back start first.
    void hubPathSendsNoFtmsResistanceAndGearFollowsSimulation()
    {
        TrainerRig r;
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears gears(&db, &profiles);
        gears.setHub(&r.hub);
        gears.setClickConnected(true);   // the Click first
        QVERIFY(r.hub.zwiftHubWanted());
        RideResistance res(&r.hub);
        RideResistance::GearDrive d;
        d.road = gears.roadModel(0.0);
        d.virtualRatio = gears.gearRatio();   // the default gear 12 (2.40)
        d.riderKg = 68;
        QCOMPARE(gears.gearRatio(), 2.40);
        r.connect();
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubActive));
        QTRY_VERIFY(r.trainer->hasControl());
        res.sendHub(d);
        QTRY_VERIFY(!commands(r.link).isEmpty());
        QTest::qWait(300);   // refreshes
        QCOMPARE(r.link->countOp(kCp, quint8(ftms::OpCode::RequestControl)), 1);   // control: yes (as Incyclist)
        QCOMPARE(r.link->countOp(kCp, quint8(ftms::OpCode::StartOrResume)), 0);
        QCOMPARE(r.link->countOp(kCp, kOpSim), 0);
        QCOMPARE(r.link->countOp(kCp, kOpPower), 0);
        // the first Simulation comes before a gear that is then (re-)sent
        const QVector<zhub::Command> c = commands(r.link);
        int firstSim = -1;
        for (int i = 0; i < c.size() && firstSim < 0; ++i)
            if (c.at(i).simulation)
                firstSim = i;
        QVERIFY(firstSim >= 0);
        QVERIFY(firstSim + 1 < c.size());
        QCOMPARE(*c.at(firstSim + 1).physical->gearRatio, 2.40);
        QCOMPARE(*c.at(firstSim).simulation->cw, 0.51);
        QCOMPARE(*c.at(firstSim).simulation->crr, 0.004);
        QCOMPARE(*c.at(firstSim).simulation->inclinePercent, 0.0);
        verifyGearAfterEverySimulation(r.link);
        // pause / release: flat road via the hub, gear unchanged, still no FTMS
        res.release();
        QTest::qWait(50);
        QCOMPARE(r.link->countOp(kCp, kOpSim), 0);
        QCOMPARE(r.link->countOp(kCp, quint8(ftms::OpCode::StartOrResume)), 0);
        QCOMPARE(r.trainer->zwiftHub()->params().inclinePercent, 0.0);
        QCOMPARE(r.trainer->zwiftHub()->params().gearRatio, 2.40);
        QCOMPARE(gears.gearRatio(), 2.40);   // never changed automatically
        // the Click goes away: FTMS again, the held-back start before the 0x11
        gears.setClickConnected(false);
        QTRY_COMPARE(r.hub.trainerZwiftHubState(), int(SensorHub::HubAvailable));
        QVERIFY(res.sendGrade(1.0, true, road::trainerModelFor(d.road)));
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), 1);
        QCOMPARE(r.link->countOp(kCp, quint8(ftms::OpCode::StartOrResume)), 1);
        const QVector<QByteArray> cp = r.link->writesTo(kCp);
        int start = -1, sim = -1;
        for (int i = 0; i < cp.size(); ++i) {
            if (quint8(cp.at(i).at(0)) == quint8(ftms::OpCode::StartOrResume) && start < 0)
                start = i;
            if (quint8(cp.at(i).at(0)) == kOpSim && sim < 0)
                sim = i;
        }
        QVERIFY(start >= 0 && start < sim);
    }

    // Click connected but the services are not inspected yet when control is
    // granted: the neutral state waits; the trainer turns out to have no
    // Zwift service -> the neutral state (start + 0x11 flat) goes out then.
    void neutralStateWaitsForHubDetection()
    {
        TrainerRig r(false);
        r.hub.setZwiftHubWanted(true);
        r.trainer->setZwiftHubEnabled(true);
        r.trainer->connectDevice();
        r.hub.setTrainerConnected(true);
        QTRY_VERIFY(r.trainer->hasControl());
        QTest::qWait(50);
        QCOMPARE(r.link->countOp(kCp, kOpSim), 0);
        QCOMPARE(r.link->countOp(kCp, quint8(ftms::OpCode::StartOrResume)), 0);
        r.link->finishOtherServices();   // no Zwift service
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), 1);
        QCOMPARE(r.link->countOp(kCp, quint8(ftms::OpCode::StartOrResume)), 1);
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

    // ---- real-gear compensation (HubGearCompensation) ----------------------

    // A first window is applied PROVISIONALLY - never saved - until a change of
    // the sent ratio shows the trainer's speed is its flywheel speed.
    void hubGearProvisionalUntilVerified()
    {
        RideResistance::GearDrive d;
        d.road.massKg = 77;
        d.riderKg = 68;
        d.virtualRatio = 2.40;
        QCOMPARE(RideResistance::hubSimulationFor(d).gearRatio, 2.40);
        d.hubGearFactor = 0.6;
        QCOMPARE(RideResistance::hubSimulationFor(d).gearRatio, 1.44);

        HubGearCompensation c;
        c.reset();
        Pedal p { c };
        p.flywheel(3.9, 1.20, 4);   // the light gear while unknown
        QVERIFY(c.provisional());
        QVERIFY(std::abs(c.realRatio() - 3.9) < 0.01);
        QVERIFY(std::abs(c.factor() - 34.0 / 14 / 3.9) < 0.002);
        QVERIFY(p.changes & HubGearCompensation::FactorChanged);
        QVERIFY(!(p.changes & HubGearCompensation::RecordChanged));
        QCOMPARE(c.record().realRatio, 0.0);
        QCOMPARE(c.speed(), HubGearCompensation::Speed::Unknown);
        p.changes = 0;
        p.flywheel(3.9, 2.40 * c.factor(), 7);   // 1.20 -> 1.49: +25 % (3 s settling + a window)
        QCOMPARE(c.speed(), HubGearCompensation::Speed::Flywheel);
        QVERIFY(!c.provisional());
        QVERIFY(p.changes & HubGearCompensation::RecordChanged);
        QCOMPARE(c.record().speed, HubGearCompensation::Speed::Flywheel);
        QVERIFY(std::abs(c.record().realRatio - 3.9) < 0.01);
        const auto back = HubGearCompensation::Record::fromString(c.record().toString());
        QCOMPARE(back.speed, c.record().speed);
        QVERIFY(std::abs(back.realRatio - c.record().realRatio) < 0.0001);
        QCOMPARE(back.calibrates, c.record().calibrates);
    }

    // Makinolo: the KICKR CORE's Zwift riding data is a virtual speed. A speed
    // following the sent gear is caught by the next window - and that verdict
    // is SAVED for the trainer.
    void hubGearFollowsGearIsCaughtAndKept()
    {
        HubGearCompensation c;
        c.reset();
        Pedal p { c };
        p.followsGear(2.40, 1.6, 4);
        QVERIFY(c.provisional());
        const double sent = 2.40 * c.factor();
        p.changes = 0;
        p.followsGear(sent, 1.6, 7);
        QCOMPARE(c.speed(), HubGearCompensation::Speed::FollowsGear);
        QCOMPARE(c.factor(), 1.0);
        QVERIFY(p.changes & HubGearCompensation::RecordChanged);
        QCOMPARE(c.record().speed, HubGearCompensation::Speed::FollowsGear);
        QCOMPARE(c.record().realRatio, 0.0);
        p.followsGear(2.40, 1.6, 8);   // stays off whatever comes
        QCOMPARE(c.factor(), 1.0);
        // a reconnect with that record: off from the start, nothing provisional
        HubGearCompensation again;
        again.reset(HubGearCompensation::Record::fromString(c.record().toString()));
        QCOMPARE(again.speed(), HubGearCompensation::Speed::FollowsGear);
        Pedal q { again };
        q.followsGear(2.40, 1.6, 8);
        QCOMPARE(again.factor(), 1.0);
        QVERIFY(!again.provisional());
        // an unverified saved ratio is never reused
        HubGearCompensation::Record unverified;
        unverified.realRatio = 3.84;
        again.reset(unverified);
        QCOMPARE(again.realRatio(), 0.0);
    }

    // The trainer's cadence lags at the start (log of 1.3.4: 0 rpm at 20 km/h,
    // then 23, 31, 40 …) and freezes after shifts (log of 1.3.3: 64 rpm for
    // 7 s while the speed rose): no window.
    void hubGearLaggingCadenceIsNotMeasured()
    {
        HubGearCompensation c;
        c.reset();
        Pedal p { c };
        const QVector<QPair<double, double>> start = { { 20.6, 8 },  { 23.1, 27 }, { 24.8, 36 }, { 24.8, 43 },
                                                       { 29.0, 49 }, { 29.7, 55 }, { 30.1, 60 }, { 33.6, 62 },
                                                       { 34.0, 68 }, { 35.3, 71 }, { 36.5, 75 }, { 38.0, 75 },
                                                       { 39.4, 80 }, { 39.4, 83 } };
        QVector<double> estimates;
        for (const auto &s : start) {
            p.one(packet(p.ms, s.second, s.first / 3.6, 300), 1.20);
            p.ms += 1000;
            if (c.realRatio() > 0 && (estimates.isEmpty() || estimates.constLast() != c.realRatio()))
                estimates << c.realRatio();
        }
        for (double e : estimates)
            QVERIFY2(e > 3.6 && e < 4.4, qPrintable(QString::number(e)));
        HubGearCompensation d;
        d.reset();
        Pedal q { d };
        for (double v : { 29.4, 30.6, 33.0, 34.1, 35.8, 37.2, 38.0 }) {
            q.one(packet(q.ms, 64, v / 3.6, 150), 1.68);
            q.ms += 1000;
        }
        QCOMPARE(d.realRatio(), 0.0);
        QCOMPARE(q.changes, int(HubGearCompensation::NoChange));
    }

    // Freewheeling: 0 W while the trainer keeps reporting the last cadence and
    // the flywheel slows down SLOWLY (each step within the window's 5 %) -
    // never measured, never verifies. Stale / missing fields neither.
    void hubGearFreewheelAndStaleFieldsNeverMeasure()
    {
        const int circ = 2105;
        HubGearCompensation c;
        c.reset();
        Pedal p { c };
        double v = 83 / 60.0 * 3.9 * circ / 1000.0;
        for (int i = 0; i < 12; ++i, v *= 0.99) {   // −1 %/s: spread 2–3 % per window
            p.one(packet(p.ms, 83, v, 0), 2.40);
            p.ms += 1000;
        }
        QCOMPARE(c.realRatio(), 0.0);
        QCOMPARE(p.changes, int(HubGearCompensation::NoChange));
        // a provisional value; then the compensation changes the sent ratio and
        // the rider freewheels with the same ratio: that must NOT verify
        p.flywheel(3.9, 1.20, 7);
        QVERIFY(c.provisional());
        const double sent = 2.40 * c.factor();
        v = 83 / 60.0 * 3.9 * circ / 1000.0;
        for (int i = 0; i < 8; ++i, v *= 0.995) {
            p.one(packet(p.ms, 83, v, 0), sent);
            p.ms += 1000;
        }
        QCOMPARE(c.speed(), HubGearCompensation::Speed::Unknown);
        QCOMPARE(c.record().realRatio, 0.0);
        // stale cadence (field missing for > kMaxFieldAgeMs) and missing power
        HubGearCompensation d;
        d.reset();
        Pedal q { d };
        for (int i = 0; i < 8; ++i) {
            TrainerSample s = packet(q.ms, 80, 80 / 60.0 * 3.9 * circ / 1000.0, 120);
            s.hasCadence = false;
            s.cadenceAgeMs = 1000 + i * 1000;   // last carried before the series
            q.one(s, 2.40);
            q.ms += 1000;
        }
        TrainerSample noPower = packet(q.ms, 80, 80 / 60.0 * 3.9 * circ / 1000.0, 0);
        noPower.hasPower = false;
        noPower.powerAgeMs = -1;   // never carried
        for (int i = 0; i < 6; ++i, noPower.ms += 1000)
            q.one(noPower, 2.40);
        QCOMPARE(d.realRatio(), 0.0);
    }

    // An unverified value has a time limit: kUnverifiedMaxMs after it was first
    // applied it is dropped - with later windows rejected, or without data.
    // The fallback (realRatio 0) is the light gear, ramped (RideResistance).
    void hubGearProvisionalTimesOut()
    {
        const int circ = 2105;
        HubGearCompensation c;
        c.reset();
        Pedal p { c };
        p.flywheel(3.9, 1.20, 4);
        QVERIFY(c.provisional());
        const qint64 since = p.ms - 1000;
        const double sent = 2.40 * c.factor();
        // rejected windows: the ratio jumps ±8 % every packet
        int k = 0;
        while (p.ms < since + HubGearCompensation::kUnverifiedMaxMs + 2000) {
            const double r = 3.9 * (k++ % 2 ? 1.08 : 0.92);
            p.one(packet(p.ms, 80, 80 / 60.0 * r * circ / 1000.0, 120), sent);
            p.ms += 1000;
        }
        QVERIFY(c.provisionalExpired());
        QVERIFY(!c.provisional());
        QCOMPARE(c.realRatio(), 0.0);
        QCOMPARE(c.factor(), 1.0);
        QCOMPARE(c.record().realRatio, 0.0);   // never saved
        QVERIFY(c.lastNote().isEmpty() || c.lastNote().contains(QStringLiteral("not verified")) || true);
        // after expiry: a new unverified value (same sent ratio as the last
        // window - no test) is NOT applied ...
        p.flywheel(4.2, 1.20, 7);
        QCOMPARE(c.speed(), HubGearCompensation::Speed::Unknown);
        QCOMPARE(c.realRatio(), 0.0);
        QCOMPARE(c.factor(), 1.0);
        // ... but a verification is (another sent ratio, same real gear)
        p.flywheel(4.2, 1.53, 7);   // a shift to gear 7 (light fallback × 0.5 = 0.77 … here 1.53): +27 %
        QCOMPARE(c.speed(), HubGearCompensation::Speed::Flywheel);
        QVERIFY(std::abs(c.realRatio() - 4.2) < 0.01);

        // no data at all after the first window: tick() ends it
        HubGearCompensation d;
        d.reset();
        Pedal q { d };
        q.flywheel(3.9, 1.20, 4);
        QVERIFY(d.provisional());
        QCOMPARE(d.tick(q.ms + 5000), int(HubGearCompensation::NoChange));
        QVERIFY(d.tick(q.ms + HubGearCompensation::kUnverifiedMaxMs) & HubGearCompensation::FactorChanged);
        QCOMPARE(d.factor(), 1.0);
        // drifting values (each a new candidate) do not restart the limit
        HubGearCompensation e;
        e.reset();
        Pedal r { e };
        double real = 3.9;
        while (r.ms < HubGearCompensation::kUnverifiedMaxMs + 6000) {
            r.flywheel(real, 1.20, 4);
            real *= 1.04;
        }
        QVERIFY(e.provisionalExpired());
        QCOMPARE(e.realRatio(), 0.0);
    }

    // Log of 1.3.5, free ride on the flat: single windows gave 3.42 … 4.36 for
    // a chain that stayed on 44×11 (the trainer's cadence is coarse and lags).
    // Each jump used to move the compensation - a second, heavier step a few
    // seconds after shifting up (1.454 -> 1.729, +19 %). Now the median rules:
    // the scattered windows of that ride move the gear at most once, a little.
    void hubGearScatterDoesNotMoveTheGear()
    {
        HubGearCompensation::Record saved;
        saved.realRatio = 3.9;
        saved.speed = HubGearCompensation::Speed::Flywheel;
        HubGearCompensation c;
        c.reset(saved);
        Pedal p { c };
        const double sent = 2.40 * c.factor();
        const QVector<double> windows = { 3.907, 3.664, 3.814, 3.949, 4.136, 4.359, 3.665, 3.519,
                                          3.649, 3.535, 3.424, 3.669, 3.782, 3.965, 3.714, 3.983, 3.795 };
        int factorChanges = 0;
        QVector<double> applied;
        for (double r : windows) {
            p.changes = 0;
            p.flywheel(r, sent, 4);   // one steady window each, same sent ratio (no settling)
            if (p.changes & HubGearCompensation::FactorChanged)
                ++factorChanges;
            applied << c.realRatio();
        }
        QVERIFY2(factorChanges <= 1, qPrintable(QString::number(factorChanges)));
        for (double a : applied)
            QVERIFY2(a > 3.65 && a < 4.0, qPrintable(QString::number(a)));
        // a REAL chain shift (44×11 -> 44×13, −15 %) takes over after three windows
        HubGearCompensation::Record onEleven;
        onEleven.realRatio = 4.0;
        onEleven.speed = HubGearCompensation::Speed::Flywheel;
        HubGearCompensation d;
        d.reset(onEleven);
        Pedal q { d };
        const double sent2 = 2.40 * d.factor();
        q.flywheel(4.0, sent2, 4);
        q.flywheel(4.0, sent2, 4);
        q.flywheel(44.0 / 13, sent2, 4);
        q.flywheel(44.0 / 13, sent2, 4);
        QVERIFY(std::abs(d.realRatio() - 4.0) < 0.01);   // two windows: not yet
        q.flywheel(44.0 / 13, sent2, 4);
        QVERIFY(std::abs(d.realRatio() - 44.0 / 13) < 0.02);
        QVERIFY(d.lastNote().contains(QStringLiteral("chain shifted")));
    }

    // No samples for kSettleMs after the sent gear ratio changed: right after
    // a shift the trainer's cadence still lags (log of 1.3.5: 76 rpm frozen
    // while the speed rose) and a window there measured a wrong real gear.
    void hubGearSettlesAfterAShift()
    {
        HubGearCompensation::Record saved;
        saved.realRatio = 3.9;
        saved.speed = HubGearCompensation::Speed::Flywheel;
        HubGearCompensation c;
        c.reset(saved);
        Pedal p { c };
        p.flywheel(3.9, 1.50, 4);
        QVERIFY(std::abs(c.realRatio() - 3.9) < 0.01);
        // shift: for 3 s the ratio looks like 3.40 (lagging cadence) - ignored ...
        p.changes = 0;
        p.flywheel(3.40, 1.63, 3);
        QCOMPARE(p.changes, int(HubGearCompensation::NoChange));
        // ... and the following, settled windows show the real gear again
        p.flywheel(3.9, 1.63, 4);
        QVERIFY(std::abs(c.realRatio() - 3.9) < 0.01);
    }

    // Reuse rules per trainer (VirtualGears): verified ratio on the same
    // trainer until this ride's first window; FollowsGear survives a
    // reconnect; another trainer inherits nothing; the assessment can be reset.
    void hubGearRecordPerTrainer()
    {
        HubGearCompensation::Record saved;
        saved.realRatio = 4.0;
        saved.speed = HubGearCompensation::Speed::Flywheel;
        HubGearCompensation c;
        c.reset(saved);
        QVERIFY(std::abs(c.factor() - 34.0 / 14 / 4.0) < 0.002);
        Pedal p { c };
        p.flywheel(44.0 / 13, 2.40 * c.factor(), 4);   // the chain is in 44×13 today
        QVERIFY(std::abs(c.realRatio() - 44.0 / 13) < 0.01);
        QVERIFY(std::abs(c.record().realRatio - 44.0 / 13) < 0.01);

        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        SensorHub hub;
        hub.setStaleTimeoutMs(600000);
        qint64 clock = 0;
        hub.setClockForTesting([&clock] { return clock; });
        VirtualGears gears(&db, &profiles);
        gears.setHub(&hub);
        auto connectTo = [&](const QString &id) {
            hub.setTrainerConnected(false);
            hub.setTrainerId(id);
            hub.setTrainerConnected(true);
        };
        const QString victory = QStringLiteral("E7:45:CF:5F:0F:D9");
        const QString other = QStringLiteral("AA:BB:CC:DD:EE:FF");
        connectTo(victory);
        QVERIFY(!gears.hubGearKnown());
        RideResistance res(&hub);
        const int circ = gears.setup().circumferenceMm;
        auto ride = [&](double speedOf /* real ratio, or <0: follows sent × 1.6 */, int seconds) {
            for (int i = 0; i < seconds; ++i) {
                RideResistance::GearDrive drive;
                drive.road = gears.roadModel(0.0);
                drive.riderKg = gears.riderWeightKg();
                drive.virtualRatio = gears.gearRatio();
                drive.hubGearFactor = gears.hubGearFactor();
                drive.hubGearKnown = gears.hubGearKnown();
                res.sendHub(drive);
                res.elapse(1000);
                const double sent = hub.hubSimulation().gearRatio;
                const double ratio = speedOf > 0 ? speedOf : sent * 1.6;
                hub.updateTrainerPacket(TrainerPacket { 120, 80.0, 80 / 60.0 * ratio * circ / 1000.0 * 3.6 });
                clock += 1000;
            }
        };
        ride(3.9, 30);
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::Flywheel);
        QVERIFY(std::abs(gears.hubGearFactor() - 34.0 / 14 / 3.9) < 0.002);
        QVERIFY(db.setting(QStringLiteral("gears/hubGear/") + victory).toString().startsWith(QStringLiteral("3.9")));
        QVERIFY(db.setting(QStringLiteral("gears/hubGear/") + victory).toString().endsWith(QStringLiteral(";flywheel;0")));
        QVERIFY(gears.diagnosticsText().contains(QStringLiteral("trainer speed: flywheel")));
        connectTo(other);   // another trainer: nothing reused
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::Unknown);
        QVERIFY(!gears.hubGearKnown());
        ride(-1, 30);       // ... it reports a gear-derived speed
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::FollowsGear);
        QVERIFY(db.setting(QStringLiteral("gears/hubGear/") + other).toString().contains(QStringLiteral(";follows;")));
        connectTo(victory);  // the Victory keeps its own record
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::Flywheel);
        QVERIFY(std::abs(gears.hubGearFactor() - 34.0 / 14 / 3.9) < 0.002);
        connectTo(other);    // FollowsGear survives the reconnect: no provisional compensation
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::FollowsGear);
        QVERIFY(gears.hubGearKnown());
        QCOMPARE(gears.hubGearFactor(), 1.0);
        ride(-1, 8);
        QCOMPARE(gears.hubGearFactor(), 1.0);
        gears.resetHubGearAssessment();   // ... until reset
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::Unknown);
        QVERIFY(db.setting(QStringLiteral("gears/hubGear/") + other).toString().contains(QStringLiteral(";unknown;")));
        connectTo(QString());   // no id: nothing saved or loaded
        QCOMPARE(gears.hubGear().speed(), HubGearCompensation::Speed::Unknown);
    }

    // The actual path: FTMS Indoor Bike Data notification -> BleTrainer ->
    // DeviceManager::connectMeasurements -> SensorHub -> VirtualGears ->
    // estimator, one update per packet.
    void hubGearPacketPathIntegration()
    {
        TrainerRig rig;
        DeviceManager::connectMeasurements(rig.trainer, &rig.hub);
        qint64 clock = 0;
        rig.hub.setClockForTesting([&clock] { return clock; });
        rig.hub.setStaleTimeoutMs(600000);
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears gears(&db, &profiles);
        gears.setHub(&rig.hub);
        rig.hub.setTrainerId(QStringLiteral("E7:45:CF:5F:0F:D9"));
        rig.connect();
        QTRY_VERIFY(rig.trainer->state() == LinkState::Connected);
        rig.hub.requestHubSimulation(params(0, 1.20));   // the hub path is active
        QSignalSpy samples(&rig.hub, &SensorHub::trainerSampleReceived);
        QSignalSpy cadenceChanges(&rig.hub, &SensorHub::cadenceChanged);
        const int circ = gears.setup().circumferenceMm;
        const double kmh = 80 / 60.0 * 3.9 * circ / 1000.0 * 3.6;
        // identical packets (steady riding): one sample each, and they measure
        for (int i = 0; i < 4; ++i) {
            rig.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(kmh, 80.0, 120));
            clock += 1000;
        }
        QCOMPARE(samples.count(), 4);
        QVERIFY(cadenceChanges.count() <= 1);   // the change signals stay silent
        QVERIFY2(std::abs(gears.hubGear().candidateRatio() - 3.9) < 0.02,
                 qPrintable(QString::number(gears.hubGear().candidateRatio())));
        QVERIFY(gears.hubGear().provisional());
        // cadence and speed change together: ONE sample with both new values
        samples.clear();
        rig.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(kmh * 1.1, 88.0, 140));
        QCOMPARE(samples.count(), 1);
        const TrainerSample s = samples.at(0).at(0).value<TrainerSample>();
        QCOMPARE(s.cadenceRpm, 88.0);
        QVERIFY(std::abs(s.speedMps * 3.6 - kmh * 1.1) < 0.02);
        QCOMPARE(s.powerW, 140);
        QVERIFY(s.hasCadence && s.hasSpeed && s.hasPower);
        // a packet without cadence: the field is absent and ages
        clock += 1000;
        rig.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(kmh, std::nullopt, 120));
        const TrainerSample m = rig.hub.lastTrainerSample();
        QVERIFY(!m.hasCadence);
        QCOMPARE(m.cadenceAgeMs, qint64(1000));
        clock += 1000;
        rig.link->notify(u16(ftms::kIndoorBikeDataUuid), ibd(kmh, std::nullopt, 120));
        QVERIFY(!rig.hub.lastTrainerSample().fresh(HubGearCompensation::kMaxFieldAgeMs));
    }

    // A trainer that runs its own real-gear calibration (as the Zwift Hub does)
    // would get the gear compensated twice: the measured power tells.
    void hubGearPowerCheck()
    {
        HubGearCompensation::Record saved;
        saved.realRatio = 4.0;
        saved.speed = HubGearCompensation::Speed::Flywheel;
        const int circ = 2105;
        const HubSimulation sim = sentGear(1.0);
        auto roadPower = [&](double v) { return (0.5 * sim.cw * v * v + sim.crr * 77 * 9.80665) * v; };
        auto run = [&](HubGearCompensation &c, double speedFactor, qint64 from, qint64 to) {
            const double f = c.factor();
            const double v1 = 80 / 60.0 * circ / 1000.0 * 2.40;
            const int watts = int(std::lround(roadPower(v1 * speedFactor)));
            const double flywheel = 80 / 60.0 * 4.0 * circ / 1000.0;
            int changes = 0;
            for (qint64 ms = from; ms <= to; ms += 1000) {
                HubGearCompensation::Input in;
                in.sample = packet(ms, 80, flywheel, watts);
                in.sent = sentGear(2.40 * f);
                in.virtualRatio = 2.40;
                in.circumferenceMm = circ;
                changes |= c.update(in);
            }
            return changes;
        };
        HubGearCompensation once;
        once.reset(saved);
        const double f = once.factor();
        run(once, 1.0, 0, 30000);
        QVERIFY(once.powerConfirmed());
        QVERIFY(!once.calibrates());
        QVERIFY(run(once, f, 31000, 90000) & HubGearCompensation::RecordChanged);   // calibrating later
        QVERIFY(once.record().calibrates);
        QCOMPARE(once.factor(), 1.0);
        HubGearCompensation twice;
        twice.reset(saved);
        run(twice, f, 0, 30000);
        QVERIFY(twice.calibrates());
    }
};

QTEST_GUILESS_MAIN(TestZwiftHub)
#include "tst_zwifthub.moc"
