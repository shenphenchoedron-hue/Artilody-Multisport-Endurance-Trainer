#include "SimulatedDevices.h"

#include "Pm5Protocol.h"

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
double noise(double amplitude)
{
    return (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * amplitude;
}
} // namespace

// ---------------------------------------------------------------------------

SimulatedTrainer::SimulatedTrainer(QObject *parent)
    : TrainerDevice(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SimulatedTrainer::step);
    m_simSupported = qEnvironmentVariableIntValue("WATT_SIM_TRAINER_NO_SIM") <= 0;
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
    });
}

void SimulatedTrainer::disconnectDevice()
{
    m_timer.stop();
    m_power = m_cadence = 0;
    setState(LinkState::Disconnected);
    emit logMessage(QStringLiteral("Trainer disconnected"), false);
}

void SimulatedTrainer::setTargetPower(int watts)
{
    m_erg = true;
    m_sim = false;
    m_target = watts;
}

void SimulatedTrainer::setFreeRide()
{
    m_erg = false;
    m_sim = false;
}

void SimulatedTrainer::setSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM)
{
    if (!m_simSupported)
        return;   // like a trainer answering "Op Code not supported"
    if (!m_sim) {
        // start in a middle gear; the rider shifts from there
        m_wheelSpeed = 0;
        m_gear = 10;
    }
    m_erg = false;
    m_sim = true;
    m_grade = gradePercent;
    m_wind = windSpeedMps;
    m_crr = crr;
    m_cw = cwKgPerM;
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
    const double force = m_crr * kSimMassKg * 9.80665 * std::cos(theta) + kSimMassKg * 9.80665 * std::sin(theta)
                         + 0.5 * m_cw * air * std::abs(air);
    return std::max(0.0, force * v);
}

double SimulatedTrainer::riderGoal() const
{
    if (m_fixedRatio > 0)
        return 0.0;   // virtual gears: no effort target, the brake decides
    const double base = m_freeRidePower + 15.0 * std::sin(m_t / 40.0);
    return std::clamp(base + kSimWattsPerPercent * m_grade, 0.5 * base, 1.8 * base);
}

void SimulatedTrainer::step()
{
    ++m_t;
    double goal;
    if (m_fixedRatio > 0) {
        // Virtual gears: the bike stays in the reference gear and the rider
        // keeps his cadence (~88 rpm, a few rpm less under a heavy load). No
        // shifting, no effort target - the power is whatever the brake asks
        // for at that speed, so the app's resistance (and with it the
        // virtual gear) decides the watts.
        const double load = std::clamp((m_power - kSimCadenceLoadWatts) / 50.0, 0.0, kSimMaxCadenceDrop);
        m_cadence += ((kSimCadenceRpm + 2.0 * std::sin(m_t / 23.0) - load) - m_cadence) * 0.5;
        m_wheelSpeed = m_cadence / 60.0 * m_fixedRatio * kWheelCircumferenceM;
        if (m_erg && m_target > 0)
            goal = m_target;                               // ERG holds the target at any speed
        else if (m_sim)
            goal = brakePower(m_wheelSpeed);               // the (gear-adjusted) grade
        else
            goal = brakePowerAt(m_wheelSpeed, 0.0);        // free ride: the flywheel's basic resistance
    } else {
        m_cadence += ((85.0 + 4.0 * std::sin(m_t / 23.0)) - m_cadence) * 0.5;
        if (m_erg && m_target > 0) {
            goal = m_target;
        } else if (m_sim) {
            // the brake decides the power; the rider shifts one gear per second
            // (up to three when far off) towards the effort he wants
            const double want = riderGoal();
            auto powerIn = [this](int g) { return brakePower(m_cadence / 60.0 * gearRatio(g) * kWheelCircumferenceM); };
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
            goal = brakePower(m_wheelSpeed);
        } else {
            goal = m_freeRidePower + 25.0 * std::sin(m_t / 40.0) + 10.0 * std::sin(m_t / 7.0);
        }
    }
    const bool brake = m_fixedRatio > 0 || m_sim;
    m_power += (goal - m_power) * (brake ? 0.7 : 0.45);
    const double shown = std::max(0.0, m_power + (m_noise ? noise(4.0) : 0.0));
    emit powerMeasured(int(std::lround(shown)));
    emit cadenceMeasured(std::max(0.0, m_cadence + (m_noise ? noise(1.0) : 0.0)));
    emit speedMeasured(brake ? m_wheelSpeed * 3.6 : std::cbrt(std::max(0.0, shown)) * 5.2);
}

// ---------------------------------------------------------------------------

SimulatedController::SimulatedController(QObject *parent)
    : ControllerDevice(parent)
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

} // namespace watt
