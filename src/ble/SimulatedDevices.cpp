#include "SimulatedDevices.h"

#include "DiagLog.h"
#include "FtmsProtocol.h"
#include "Gearing.h"   // road::kDrivetrainEfficiency (watt_core)
#include "Pm5Protocol.h"
#include "ZwiftHubLink.h"
#include "ZwiftHubProtocol.h"

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
double noise(double amplitude)
{
    return (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * amplitude;
}

// The virtual trainer in the diagnostic log: the FTMS command a real trainer
// would get, encoded and decoded like BleTrainer's "CP write".
void logVirtualCommand(const QByteArray &cmd, const char *answer = "Success (virtual)")
{
    if (DiagLog::instance().isActive())
        DiagLog::write("FTMS", QStringLiteral("CP write (virtual trainer) [%1] = %2 -> %3")
                                   .arg(DiagLog::hex(cmd), ftms::describeControlPoint(cmd), QString::fromLatin1(answer)));
}
} // namespace

// ---------------------------------------------------------------------------

// In-process "GATT" of the virtual trainer's Zwift hub service: answers like
// a Zwift Hub (unencrypted trainer variant), see ZwiftHubProtocol.h.
class SimZwiftGatt : public GattLink
{
public:
    SimZwiftGatt(SimulatedTrainer *trainer, bool answerHandshake)
        : GattLink(trainer)
        , m_trainer(trainer)
        , m_answer(answerHandshake)
    {
        m_stream.setInterval(1000);
        QObject::connect(&m_stream, &QTimer::timeout, this, [this] { sendRiding(); });
    }
    void sendRiding()
    {
        {
            zhub::RidingData d;
            d.power = int(std::lround(m_trainer->currentPower()));
            d.cadence = int(std::lround(m_trainer->cadenceRpm()));
            d.speedMps = m_trainer->wheelSpeedMps();
            d.field4 = d.power;   // as the Victory
            emit characteristicChanged(ZwiftHubLink::asyncUuid(), zhub::encodeRidingData(d));
            if (++m_ticks % 5 == 0)
                emit characteristicChanged(ZwiftHubLink::asyncUuid(), QByteArray::fromHex("1910"));   // idle / keep-alive
        }
    }
    void connectToDevice() override { m_state = LinkState::Connected; }
    void disconnectFromDevice() override
    {
        m_state = LinkState::Disconnected;
        m_stream.stop();
    }
    bool reconnectNow() override { return false; }
    void setAutoReconnect(bool) override {}
    LinkState state() const override { return m_state; }
    bool hasCharacteristic(const QBluetoothUuid &c) const override
    {
        return c == ZwiftHubLink::asyncUuid() || c == ZwiftHubLink::syncRxUuid() || c == ZwiftHubLink::syncTxUuid();
    }
    QList<QBluetoothUuid> discoveredServices() const override
    {
        return { QBluetoothUuid(quint16(0x1826)), QBluetoothUuid(quint16(zwift::kServiceUuid16)) };
    }
    bool otherServicesInspected() const override { return true; }
    bool enableNotifications(const QBluetoothUuid &c, bool) override { return hasCharacteristic(c); }
    bool read(const QBluetoothUuid &) override { return false; }
    bool write(const QBluetoothUuid &c, const QByteArray &data, bool) override
    {
        if (m_state != LinkState::Connected || c != ZwiftHubLink::syncRxUuid() || data.isEmpty())
            return false;
        if (zhub::isRideOn(data)) {
            if (m_answer)
                QTimer::singleShot(150, this, [this] { emit characteristicChanged(ZwiftHubLink::syncTxUuid(), QByteArray("RideOn")); });
        } else if (data == zhub::startStream()) {
            if (m_answer)
                QTimer::singleShot(300, this, [this] {
                    m_stream.start();
                    sendRiding();
                });
        } else if (quint8(data.at(0)) == zhub::OutCommand) {
            m_trainer->applyHubCommand(data.mid(1));
        } else if (quint8(data.at(0)) == zhub::OutRequest) {
            const auto id = zhub::decodeRequest(data.mid(1));
            if (id && *id == zhub::kSettingsDataId) {
                QTimer::singleShot(100, this, [this] { emit characteristicChanged(ZwiftHubLink::asyncUuid(), zhub::encodeSettingsEcho(m_settings)); });
            }
        }
        return true;
    }
    zhub::Settings m_settings;

private:
    SimulatedTrainer *m_trainer;
    bool m_answer;
    LinkState m_state = LinkState::Disconnected;
    QTimer m_stream;
    int m_ticks = 0;
};

SimulatedTrainer::SimulatedTrainer(QObject *parent)
    : TrainerDevice(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SimulatedTrainer::step);
    m_simSupported = qEnvironmentVariableIntValue("WATT_SIM_TRAINER_NO_SIM") <= 0;
    // test aid: answer negative grades with "Invalid parameter" (rejection UI)
    m_rejectDescents = qEnvironmentVariableIntValue("WATT_SIM_TRAINER_REJECT_DESCENTS") > 0;
    const QString zwift = qEnvironmentVariable("WATT_SIM_TRAINER_ZWIFT");
    if (!zwift.isEmpty() && zwift != QLatin1String("0"))
        setZwiftHubCapability(true, zwift != QLatin1String("fail"));
}

// --- Zwift hub (virtual) -------------------------------------------------------------

void SimulatedTrainer::setZwiftHubCapability(bool on, bool answerHandshake)
{
    if (m_zhub) {
        delete m_zhub;
        delete m_zgatt;
        m_zhub = nullptr;
        m_zgatt = nullptr;
    }
    if (!on)
        return;
    m_zgatt = new SimZwiftGatt(this, answerHandshake);
    m_zhub = new ZwiftHubLink(m_zgatt, this);
    connect(m_zgatt, &GattLink::characteristicChanged, m_zhub, &ZwiftHubLink::handleNotification);
    connect(m_zhub, &ZwiftHubLink::stateChanged, this, [this] {
        if (!m_zhub->active())
            m_hubSim = false;   // (becomeActive() re-sends a request made before)
        emit zwiftHubChanged();
        emit controlStatusChanged();
    });
    connect(m_zhub, &ZwiftHubLink::failed, this, [this](const QString &reason) {
        emit logMessage(QStringLiteral("Zwift protocol failed (%1) – using FTMS gradient simulation").arg(reason), false);
    });
    if (state() == LinkState::Connected) {
        m_zgatt->connectToDevice();
        m_zhub->detect();
    }
}

int SimulatedTrainer::zwiftHubState() const
{
    return m_zhub && state() == LinkState::Connected ? m_zhub->hubState() : int(SensorHub::HubNone);
}

QString SimulatedTrainer::zwiftHubStatus() const
{
    return m_zhub ? m_zhub->failureReason() : QString();
}

void SimulatedTrainer::maybeStartHub()
{
    if (m_zhub && m_zhubEnabled && state() == LinkState::Connected && m_zhub->state() == ZwiftHubLink::State::Available) {
        emit logMessage(QStringLiteral("Zwift Click connected and %1 has the Zwift trainer service – starting the Zwift protocol (virtual)").arg(name()), true);
        m_zhub->start();
    }
}

void SimulatedTrainer::setZwiftHubEnabled(bool on)
{
    if (m_zhubEnabled == on)
        return;
    m_zhubEnabled = on;
    if (!m_zhub)
        return;
    DiagLog::write("ZHUB", QStringLiteral("virtual trainer: Zwift protocol %1").arg(on ? QStringLiteral("wanted (Zwift Click connected)")
                                                                                       : QStringLiteral("not wanted (no Zwift Click)")));
    if (on) {
        if (m_zhub->state() == ZwiftHubLink::State::Failed)
            m_zhub->stop();
        maybeStartHub();
    } else {
        m_zhub->stop();
        m_hubSim = false;
    }
}

void SimulatedTrainer::setHubSimulation(const HubSimulation &p)
{
    m_lastHubReq = p;
    if (m_zhub && m_zhub->active())
        m_zhub->update(p);
}

void SimulatedTrainer::applyHubCommand(const QByteArray &payload)
{
    const auto cmd = zhub::decodeCommand(payload);
    if (!cmd)
        return;
    if (cmd->simulation) {
        const zhub::Simulation &sim = *cmd->simulation;
        if (sim.inclinePercent)
            m_grade = *sim.inclinePercent;
        if (sim.crr)
            m_crr = *sim.crr;
        if (sim.cw)
            m_cw = *sim.cw;
        // echo like the Jetblack Victory: incline in whole percent (f6), CWa (f9)
        m_zgatt->m_settings.inclineWholePercent = zhub::echoedIncline(m_grade);
        m_zgatt->m_settings.crr = m_crr;
        m_zgatt->m_settings.cwa = m_cw;
        if (sim.windMps)
            m_zgatt->m_settings.windMps = *sim.windMps;
    }
    if (cmd->physical) {
        const zhub::Physical &phys = *cmd->physical;
        if (phys.gearRatio && *phys.gearRatio > 0)
            m_hubRatio = *phys.gearRatio;
        if (phys.riderKg)
            m_hubRiderKg = *phys.riderKg;
        if (phys.bikeKg)
            m_hubBikeKg = *phys.bikeKg;
        if (m_hubRiderKg > 0)
            m_massKg = m_hubRiderKg + m_hubBikeKg;
        m_zgatt->m_settings.gearRatio = m_hubRatio;
        m_zgatt->m_settings.weight11Kg = m_hubRiderKg;   // the Victory echoes the rider in f11
    }
    if (cmd->powerTarget) {
        m_erg = true;
        m_target = *cmd->powerTarget;
        m_hubSim = false;
        return;
    }
    // the trainer does the physics: simulation at the hub's gear
    m_hubSim = true;
    m_sim = true;
    m_erg = false;
    m_problem.clear();
    emit controlStatusChanged();
}

void SimulatedTrainer::setSimulationSupported(bool on)
{
    if (m_simSupported == on)
        return;
    m_simSupported = on;
    if (!on)
        m_sim = false;
    emit capabilitiesChanged();
    emit simulationSupportChanged();
}

void SimulatedTrainer::connectDevice()
{
    if (state() != LinkState::Disconnected)
        return;
    setState(LinkState::Connecting);
    QTimer::singleShot(600, this, [this] {
        if (state() != LinkState::Connecting)
            return;
        setRssi(-52);
        setState(LinkState::Connected);
        emit logMessage(QStringLiteral("Trainer connected (%1)").arg(name()), true);
        emit logMessage(QStringLiteral("FTMS capabilities detected"), true);
        emit capabilitiesChanged();
        m_timer.start();
        if (m_zgatt) {
            // like BleLink: the other services are inspected a little later
            m_zgatt->connectToDevice();
            QTimer::singleShot(800, this, [this] {
                if (!m_zhub || state() != LinkState::Connected)
                    return;
                m_zhub->detect();
                emit zwiftHubChanged();
                maybeStartHub();
            });
        }
    });
}

void SimulatedTrainer::disconnectDevice()
{
    m_timer.stop();
    if (m_zhub) {
        m_zgatt->disconnectFromDevice();
        m_zhub->reset();
        m_hubSim = false;
    }
    m_power = m_cadence = 0;
    setState(LinkState::Disconnected);
    emit logMessage(QStringLiteral("Trainer disconnected"), false);
}

void SimulatedTrainer::setTargetPower(int watts)
{
    logVirtualCommand(ftms::setTargetPower(watts));
    m_erg = true;
    m_sim = false;
    m_hubSim = false;
    m_target = watts;
    m_problem.clear();
    emit controlStatusChanged();
}

void SimulatedTrainer::setFreeRide()
{
    if (DiagLog::instance().isActive())
        DiagLog::write("FTMS", m_simSupported ? QStringLiteral("free ride (virtual trainer): 0x11 flat road")
                                              : QStringLiteral("free ride (virtual trainer): released"));
    m_erg = false;
    m_sim = false;
    m_hubSim = false;
    m_problem.clear();
    emit controlStatusChanged();
}

void SimulatedTrainer::setSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM)
{
    const bool reject = m_simSupported && m_rejectDescents && gradePercent < 0;
    logVirtualCommand(ftms::setIndoorBikeSimulation(windSpeedMps, gradePercent, crr, cwKgPerM),
                      !m_simSupported ? "Not supported (virtual, WATT_SIM_TRAINER_NO_SIM)"
                      : reject        ? "Invalid parameter (virtual, WATT_SIM_TRAINER_REJECT_DESCENTS)"
                                      : "Success (virtual)");
    if (!m_simSupported)
        return;   // like a trainer answering "Op Code not supported"
    m_hubSim = false;
    if (reject) {
        // the trainer keeps its previous state; the problem is shown
        m_problem = ftms::rejectionText(quint8(ftms::OpCode::SetIndoorBikeSimulation), 0x03);
        emit controlStatusChanged();
        return;
    }
    m_problem.clear();
    if (!m_sim) {
        // start in a middle gear; the rider shifts from there
        m_wheelSpeed = 0;
        m_gear = 10;
    }
    m_erg = false;
    m_sim = true;
    // NaN / infinity would poison the brake model (and the reported power)
    // for the rest of the ride: keep the last valid Crr / Cw, no wind / flat.
    m_grade = std::isfinite(gradePercent) ? gradePercent : 0.0;
    m_wind = std::isfinite(windSpeedMps) ? windSpeedMps : 0.0;
    if (std::isfinite(crr) && crr >= 0)
        m_crr = crr;
    if (std::isfinite(cwKgPerM) && cwKgPerM >= 0)
        m_cw = cwKgPerM;
    emit controlStatusChanged();
}

QString SimulatedTrainer::trainerMode() const
{
    if (state() != LinkState::Connected)
        return QString();
    if (m_erg)
        return QStringLiteral("erg");
    if (m_hubSim)
        return QStringLiteral("zwift");
    if (m_sim)
        return QStringLiteral("simulation");
    return m_simSupported ? QStringLiteral("simulation") : QStringLiteral("resistance");
}

QString SimulatedTrainer::controlStatus() const
{
    if (state() != LinkState::Connected)
        return QString();
    if (!m_problem.isEmpty())
        return m_problem;
    if (m_erg)
        return QStringLiteral("ERG – trainer accepted %1 W").arg(m_target);
    if (m_hubSim)
        return QStringLiteral("Zwift protocol – trainer simulates %1 %, gear ratio %2").arg(m_grade, 0, 'f', 1).arg(m_hubRatio, 0, 'f', 2);
    if (m_sim)
        return QStringLiteral("Simulation – trainer accepted %1%2 %")
            .arg(m_grade < 0 ? QStringLiteral("−") : QString())
            .arg(std::abs(m_grade), 0, 'f', 2);
    return m_simSupported ? QStringLiteral("Simulation – flat road") : QStringLiteral("Free ride (no gradient simulation)");
}

QStringList SimulatedTrainer::capabilities() const
{
    QStringList caps { QStringLiteral("Power"), QStringLiteral("Cadence"), QStringLiteral("ERG Control") };
    if (m_simSupported)
        caps << QStringLiteral("Gradient Simulation");
    caps << QStringLiteral("Free Ride");
    return caps;
}

int SimulatedTrainer::gearCount() { return 22; }

double SimulatedTrainer::gearRatio(int index) const
{
    // 34×28 (1.21) .. 50×11 (4.55), evenly spaced on a log scale
    const double lo = 34.0 / 28.0, hi = 50.0 / 11.0;
    const double t = std::clamp(index, 0, gearCount() - 1) / double(gearCount() - 1);
    return lo * std::pow(hi / lo, t);
}

double SimulatedTrainer::brakePower(double v) const
{
    return brakePowerAt(v, m_grade);
}

double SimulatedTrainer::brakePowerAt(double v, double gradePercent) const
{
    const double theta = std::atan(gradePercent / 100.0);
    const double air = v + m_wind;
    const double weight = m_massKg * 9.80665;
    const double force = m_crr * weight * std::cos(theta) + weight * std::sin(theta) + 0.5 * m_cw * air * std::abs(air);
    return std::max(0.0, force * v);
}

double SimulatedTrainer::crankPower(double v) const
{
    return crankPowerAt(v, m_grade);
}

double SimulatedTrainer::crankPowerAt(double v, double gradePercent) const
{
    return brakePowerAt(v, gradePercent) / road::kDrivetrainEfficiency;
}

void SimulatedTrainer::setMassKg(double kg)
{
    if (std::isfinite(kg) && kg >= 20 && kg <= 400)
        m_massKg = kg;
}

double SimulatedTrainer::riderCapacity() const
{
    if (m_ftp <= 0)
        return 1e9;
    return m_ftp * (1.0 + (kBurstFactor - 1.0) * std::clamp(m_reserve, 0.0, 1.0));
}

double SimulatedTrainer::fixedGearDemand(double cadence) const
{
    const double v = cadence / 60.0 * effectiveRatio() * kWheelCircumferenceM;
    if (m_erg && m_target > 0)
        return m_target;   // ERG holds the target at any speed
    return m_sim ? crankPower(v) : crankPowerAt(v, 0.0);
}

double SimulatedTrainer::riderCadence() const
{
    const double preferred = kSimCadenceRpm + 2.0 * std::sin(m_t / 23.0);
    if (m_ftp <= 0 || effectiveRatio() <= 0)
        return preferred;
    const double capacity = riderCapacity();
    const double demand = fixedGearDemand(preferred);
    double cadence = preferred;
    if (demand > capacity) {
        if (m_erg && m_target > 0) {
            // ERG: the brake holds the watts whatever the cadence - the legs fade
            cadence = preferred * capacity / demand;
        } else {
            // too hard: the cadence drops until the gear asks for what he has
            double lo = kSimMinCadenceRpm, hi = preferred;
            if (fixedGearDemand(lo) >= capacity)
                return kSimMinCadenceRpm;
            for (int i = 0; i < 40; ++i) {
                const double mid = (lo + hi) / 2;
                (fixedGearDemand(mid) > capacity ? hi : lo) = mid;
            }
            cadence = lo;
        }
    } else {
        // easy gear: spin up
        const double easy = std::clamp(1.0 - demand / (kSimEasyShare * m_ftp), 0.0, 1.0);
        cadence = preferred + (kSimMaxCadenceRpm - preferred) * easy;
    }
    return std::clamp(cadence, kSimMinCadenceRpm, 110.0);
}

double SimulatedTrainer::riderGoal() const
{
    if (effectiveRatio() > 0)
        return 0.0;   // virtual gears: no effort target, the brake decides
    // >= 1 W: std::clamp needs lo <= hi (a free ride power near 0 made base
    // negative, i.e. 0.5·base > 1.8·base - undefined behaviour)
    const double base = std::max(1.0, m_freeRidePower + 15.0 * std::sin(m_t / 40.0));
    return std::clamp(base + kSimWattsPerPercent * m_grade, 0.5 * base, 1.8 * base);
}

void SimulatedTrainer::step()
{
    ++m_t;
    double goal;
    if (effectiveRatio() > 0) {
        // Virtual gears: the bike stays in the reference gear and the rider
        // pedals at the cadence his legs allow in it (riderCadence()). No
        // shifting, no effort target - the power is whatever the brake asks
        // for at that cadence, so the app's resistance (and with it the
        // virtual gear) decides the watts: the (gear-adjusted) grade, the
        // ERG target, or the flywheel's basic resistance (free ride).
        m_cadence += (riderCadence() - m_cadence) * 0.5;
        m_wheelSpeed = m_cadence / 60.0 * effectiveRatio() * kWheelCircumferenceM;
        goal = fixedGearDemand(m_cadence);
        if (m_ftp > 0)
            goal = std::min(goal, riderCapacity());   // never more than his legs give
    } else {
        m_cadence += ((85.0 + 4.0 * std::sin(m_t / 23.0)) - m_cadence) * 0.5;
        if (m_erg && m_target > 0) {
            goal = m_target;
        } else if (m_sim) {
            // the brake decides the power; the rider shifts one gear per second
            // (up to three when far off) towards the effort he wants
            const double want = riderGoal();
            auto powerIn = [this](int g) { return crankPower(m_cadence / 60.0 * gearRatio(g) * kWheelCircumferenceM); };
            for (int i = 0; i < 3; ++i) {
                const double p = powerIn(m_gear);
                const bool farOff = p > want * 1.3 || p < want * 0.7;
                if (p > want * 1.08 && m_gear > 0 && std::abs(powerIn(m_gear - 1) - want) < std::abs(p - want))
                    --m_gear;
                else if (p < want * 0.92 && m_gear < gearCount() - 1
                         && (powerIn(m_gear + 1) <= want || std::abs(powerIn(m_gear + 1) - want) < std::abs(p - want)))
                    ++m_gear;
                else
                    break;
                if (!farOff)
                    break;
            }
            m_wheelSpeed = m_cadence / 60.0 * gearRatio(m_gear) * kWheelCircumferenceM;
            goal = crankPower(m_wheelSpeed);   // reported: crank power
        } else {
            goal = m_freeRidePower + 25.0 * std::sin(m_t / 40.0) + 10.0 * std::sin(m_t / 7.0);
        }
    }
    const bool brake = effectiveRatio() > 0 || m_sim;
    m_power += (goal - m_power) * (brake ? 0.7 : 0.45);
    if (m_ftp > 0) {
        // burst reserve: drains above FTP (kBurstSec at the full burst),
        // refills below it (kRecoverSec)
        if (m_power > m_ftp)
            m_reserve -= (m_power - m_ftp) / ((kBurstFactor - 1.0) * m_ftp) / kBurstSec;
        else
            m_reserve += (1.0 - m_power / m_ftp) / kRecoverSec * 2.0;
        m_reserve = std::clamp(m_reserve, 0.0, 1.0);
    }
    const double shown = std::max(0.0, m_power + (m_noise ? noise(4.0) : 0.0));
    const double cadence = std::max(0.0, m_cadence + (m_noise ? noise(1.0) : 0.0));
    const double speed = brake ? m_wheelSpeed * 3.6 : std::cbrt(std::max(0.0, shown)) * 5.2;
    emit powerMeasured(int(std::lround(shown)));
    emit cadenceMeasured(cadence);
    emit speedMeasured(speed);
    emit packetMeasured(TrainerPacket { int(std::lround(shown)), cadence, speed });
    if (DiagLog::instance().isActive()) {   // 1 Hz (the step), like a trainer's Indoor Bike Data
        ftms::IndoorBikeData d;
        d.speedKmh = speed;
        d.cadenceRpm = cadence;
        d.powerW = int(std::lround(shown));
        DiagLog::write("IBD", QStringLiteral("(virtual trainer) %1; brake %2 grade %3 % gear %4")
                                  .arg(ftms::describeIndoorBikeData(d),
                                       m_erg ? QStringLiteral("ERG %1 W").arg(m_target)
                                             : m_hubSim ? QStringLiteral("ZWIFT-HUB")
                                             : m_sim ? QStringLiteral("SIM") : QStringLiteral("FREE"))
                                  .arg(m_grade, 0, 'f', 2)
                                  .arg(m_hubSim && m_hubRatio > 0 ? QStringLiteral("hub %1").arg(m_hubRatio, 0, 'f', 3)
                                       : m_fixedRatio > 0 ? QStringLiteral("fixed %1").arg(m_fixedRatio, 0, 'f', 3)
                                                        : QString::number(m_gear)));
    }
}

// ---------------------------------------------------------------------------

SimulatedController::SimulatedController(QObject *parent)
    : ControllerDevice(parent)
{
}

SimulatedController::SimulatedController(const QString &id, QObject *parent)
    : ControllerDevice(parent)
    , m_id(id.isEmpty() ? QStringLiteral("sim:click") : id)
{
}

void SimulatedController::connectDevice()
{
    if (state() != LinkState::Disconnected)
        return;
    setRssi(-58);
    setState(LinkState::Connected);
    setBatteryLevel(85);
    emit logMessage(QStringLiteral("Controller connected (%1)").arg(name()), true);
}

void SimulatedController::disconnectDevice()
{
    setState(LinkState::Disconnected);
}

void SimulatedController::setButtons(bool plus, bool minus)
{
    if (state() == LinkState::Connected)
        emit buttonsChanged(plus, minus);
}

void SimulatedController::pressButton(int direction)
{
    if (state() == LinkState::Connected && direction != 0)
        emit shiftRequested(direction > 0 ? 1 : -1);
}

// ---------------------------------------------------------------------------

SimulatedHeartRate::SimulatedHeartRate(QObject *parent)
    : HeartRateDevice(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SimulatedHeartRate::step);
}

void SimulatedHeartRate::setPhysiology(int restHr, int maxHr, int ftp)
{
    m_rest = restHr;
    m_max = maxHr;
    m_ftp = std::max(1, ftp);
}

void SimulatedHeartRate::connectDevice()
{
    if (state() != LinkState::Disconnected)
        return;
    setState(LinkState::Connecting);
    QTimer::singleShot(800, this, [this] {
        if (state() != LinkState::Connecting)
            return;
        m_hr = m_rest + 10;
        setRssi(-60);
        setState(LinkState::Connected);
        emit logMessage(QStringLiteral("Heart rate sensor connected (%1)").arg(name()), true);
        m_timer.start();
    });
}

void SimulatedHeartRate::disconnectDevice()
{
    m_timer.stop();
    setState(LinkState::Disconnected);
    emit logMessage(QStringLiteral("Heart rate sensor disconnected"), false);
}

void SimulatedHeartRate::step()
{
    const double intensity = std::clamp(m_load / double(m_ftp), 0.0, 1.4);
    const double goal = m_rest + (m_max - m_rest) * std::clamp(0.25 + 0.62 * intensity, 0.2, 1.0);
    // HR responds slowly (time constant ~25 s)
    m_hr += (goal - m_hr) / 25.0;
    emit heartRateMeasured(int(std::lround(m_hr + noise(0.8))));
}

// ---------------------------------------------------------------------------

SimulatedRower::SimulatedRower(QObject *parent)
    : RowerDevice(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, [this] { step(0.25); });
}

void SimulatedRower::connectDevice()
{
    if (state() != LinkState::Disconnected)
        return;
    setState(LinkState::Connecting);
    QTimer::singleShot(700, this, [this] {
        if (state() != LinkState::Connecting)
            return;
        if (m_hr <= 0)
            m_hr = 72;
        setRssi(-55);
        setState(LinkState::Connected);
        emit logMessage(QStringLiteral("Rower connected (%1)").arg(name()), true);
        m_timer.start();
    });
}

void SimulatedRower::disconnectDevice()
{
    m_timer.stop();
    setState(LinkState::Disconnected);
    emit logMessage(QStringLiteral("Rower disconnected"), false);
}

void SimulatedRower::step(double dt)
{
    const double effort = m_target > 0 ? m_target
                                       : m_freePower + 20.0 * std::sin(m_t / 45.0) + 8.0 * std::sin(m_t / 9.0);
    // Rate rises gently with effort: 150 W -> 24 spm, 200 W -> 26 spm.
    const double spm = std::clamp(18.0 + effort / 25.0 + 0.8 * std::sin(m_t / 30.0), 16.0, 36.0);

    m_t += dt;
    if (m_rowing) {
        m_idle = 0;
        m_power += (effort - m_power) * std::min(1.0, dt * 1.5);
        m_phase += dt * spm / 60.0;
        if (m_phase >= 1.0) {
            m_phase -= 1.0;
            ++m_strokes;
            const int strokePower = int(std::lround(std::max(0.0, effort + noise(10.0))));
            m_powerSum += strokePower;
            applyStroke(m_strokes, strokePower);
            applyStrokeRate(int(std::lround(spm + noise(0.6))));
        }
    } else {
        m_idle += dt;
        m_power *= std::max(0.0, 1.0 - dt * 0.6);   // flywheel spins down
        if (m_power < 5)
            m_power = 0;
        if (m_idle > 3.0)
            applyStrokeRate(0);
    }

    const double pace = pm5::pace500FromWatts(m_power);          // s / 500 m
    const double speed = pace > 0 ? 500.0 / pace : 0.0;          // m/s
    m_dist += speed * dt;
    m_kcal += (4.0 * m_power + 300.0) / 3600.0 * dt;             // Concept2 cal/h

    const double hrGoal = std::clamp(72.0 + 0.48 * (m_rowing ? effort : 0.0), 60.0, 190.0);
    m_hr += (hrGoal - m_hr) * std::min(1.0, dt / 25.0);

    const bool active = m_rowing || m_idle <= 3.0;
    applyWorkoutState(int(pm5::WorkoutState::WorkoutRow), active);
    applyElapsed(m_t);
    applyDistance(m_dist);
    applySpeed(speed);
    applyPace(pace);
    applyAveragePace(m_dist > 1 ? m_t / m_dist * 500.0 : 0.0);
    applyAverageWatts(m_strokes > 0 ? int(std::lround(m_powerSum / m_strokes)) : 0);
    applyCalories(int(m_kcal));
    applyDragFactor(120);
    if (m_reportHr)
        applyHeartRate(int(std::lround(m_hr + noise(0.8))));
    commitMeasurement();
}

// ---------------------------------------------------------------------------

SimulatedTreadmill::SimulatedTreadmill(QObject *parent)
    : TreadmillDevice(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, [this] { step(0.25); });
}

void SimulatedTreadmill::connectDevice()
{
    if (state() != LinkState::Disconnected)
        return;
    setState(LinkState::Connecting);
    QTimer::singleShot(600, this, [this] {
        if (state() != LinkState::Connecting)
            return;
        setRssi(-50);
        setState(LinkState::Connected);
        emit logMessage(QStringLiteral("Treadmill connected (%1) – reading speed and incline only").arg(name()), true);
        m_timer.start();
    });
}

void SimulatedTreadmill::disconnectDevice()
{
    m_timer.stop();
    setState(LinkState::Disconnected);
}

void SimulatedTreadmill::suggest(double speedKmh, double inclinePercent)
{
    // <= 0: no workout target any more - the runner goes back to his own pace
    m_suggested = speedKmh > 0;
    if (m_suggested) {
        m_wantSpeed = std::clamp(speedKmh, 1.0, 25.0);
        m_wantIncline = std::clamp(inclinePercent, 0.0, 15.0);
    }
}

void SimulatedTreadmill::setFreeSpeed(double kmh, double inclinePercent)
{
    m_freeSpeed = std::clamp(kmh, 0.0, 25.0);
    m_freeIncline = std::clamp(inclinePercent, 0.0, 15.0);
}

void SimulatedTreadmill::step(double dt)
{
    m_t += dt;
    const double wantSpeed = m_suggested ? m_wantSpeed : m_freeSpeed;
    const double wantIncline = m_suggested ? m_wantIncline : m_freeIncline;
    // the runner presses +/- about three times a second: 0.1 km/h, 0.5 % steps
    const double maxSpeedStep = 1.2 * dt;
    m_belt += std::clamp(wantSpeed - m_belt, -maxSpeedStep, maxSpeedStep);
    if (std::abs(wantSpeed - m_belt) < 0.05)
        m_belt = wantSpeed;
    const double maxInclineStep = 2.0 * dt;
    m_incline += std::clamp(wantIncline - m_incline, -maxInclineStep, maxInclineStep);
    m_incline = std::round(m_incline * 2.0) / 2.0;
    m_dist += m_belt / 3.6 * dt;
    // a belt shows its set speed with tiny load-dependent wobble
    const double shown = m_belt > 0.2 ? std::round((m_belt + noise(0.03)) * 100.0) / 100.0 : 0.0;
    applyMeasurement(std::max(0.0, shown), m_incline, true, m_dist, 0);
}

} // namespace watt
