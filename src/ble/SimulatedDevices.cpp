#include "SimulatedDevices.h"

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
    m_target = watts;
}

void SimulatedTrainer::setFreeRide()
{
    m_erg = false;
}

QStringList SimulatedTrainer::capabilities() const
{
    return { QStringLiteral("Power"), QStringLiteral("Cadence"), QStringLiteral("ERG Control"),
             QStringLiteral("Free Ride") };
}

void SimulatedTrainer::step()
{
    ++m_t;
    const double goal = m_erg && m_target > 0
                            ? m_target
                            : m_freeRidePower + 25.0 * std::sin(m_t / 40.0) + 10.0 * std::sin(m_t / 7.0);
    m_power += (goal - m_power) * 0.45;
    const double shown = std::max(0.0, m_power + noise(4.0));
    m_cadence += ((85.0 + 4.0 * std::sin(m_t / 23.0)) - m_cadence) * 0.5;
    emit powerMeasured(int(std::lround(shown)));
    emit cadenceMeasured(std::max(0.0, m_cadence + noise(1.0)));
    emit speedMeasured(std::cbrt(std::max(0.0, shown)) * 5.2);
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

} // namespace watt
