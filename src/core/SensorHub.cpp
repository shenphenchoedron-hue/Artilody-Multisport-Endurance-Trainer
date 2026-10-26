#include "SensorHub.h"

#include <algorithm>
#include <cmath>

namespace watt {

SensorHub::SensorHub(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_smoother.setHoldMs(m_staleMs);
    // while smoothing, the average keeps moving between samples (and decays
    // when the trainer stops sending)
    m_displayTimer.setInterval(250);
    connect(&m_displayTimer, &QTimer::timeout, this, &SensorHub::refreshDisplayPower);
}

qint64 SensorHub::now() const
{
    return m_clockFn ? m_clockFn() : m_clock.elapsed();
}

void SensorHub::setStaleTimeoutMs(int ms)
{
    m_staleMs = ms;
    m_smoother.setHoldMs(ms);
}

void SensorHub::setPowerSmoothing(bool on)
{
    if (m_smoothing == on)
        return;
    m_smoothing = on;
    emit powerSmoothingChanged();
    refreshDisplayPower();
}

void SensorHub::setPowerSmoothingSeconds(int seconds)
{
    seconds = std::clamp(seconds, 1, 30);
    if (seconds * 1000 == m_smoother.windowMs())
        return;
    m_smoother.setWindowMs(seconds * 1000);
    emit powerSmoothingChanged();
    refreshDisplayPower();
}

void SensorHub::refreshDisplayPower()
{
    const qint64 t = now();
    const int v = m_smoothing ? m_smoother.averageRounded(t) : m_power;
    if (v != m_displayPower) {
        m_displayPower = v;
        emit displayPowerChanged();
    }
    // keep ticking while the window still holds samples that can change the
    // average; stop once it has settled
    const bool moving = m_smoothing && m_lastSampleMs >= 0
                        && t - m_lastSampleMs < std::max(m_smoother.windowMs(), m_smoother.holdMs()) + 250;
    if (moving && !m_displayTimer.isActive())
        m_displayTimer.start();
    else if (!moving && m_displayTimer.isActive())
        m_displayTimer.stop();
}

void SensorHub::updatePower(int watts)
{
    m_powerAge.start();
    watts = std::max(0, watts);
    m_lastSampleMs = now();
    m_smoother.addSample(m_lastSampleMs, watts);
    if (watts != m_power) {
        m_power = watts;
        emit powerChanged();
    }
    refreshDisplayPower();
}

void SensorHub::updateCadence(double rpm)
{
    m_cadenceAge.start();
    const int v = std::max(0, int(std::lround(rpm)));
    if (v != m_cadence) {
        m_cadence = v;
        emit cadenceChanged();
    }
}

void SensorHub::updateHeartRate(int bpm)
{
    m_hrAge.start();
    bpm = std::max(0, bpm);
    if (bpm != m_heartRate) {
        m_heartRate = bpm;
        emit heartRateChanged();
    }
}

void SensorHub::updateSpeed(double kmh)
{
    if (!std::isfinite(kmh) || kmh < 0)
        kmh = 0;
    m_speedAge.start();
    if (kmh > 0.5 && m_trainerConnected && !m_speedReported) {
        m_speedReported = true;
        emit trainerReportsSpeedChanged();
    }
    if (!qFuzzyCompare(1.0 + kmh, 1.0 + m_speed)) {
        m_speed = kmh;
        emit speedChanged();
    }
}

void SensorHub::setTrainerConnected(bool connected)
{
    if (m_trainerConnected == connected)
        return;
    m_trainerConnected = connected;
    emit trainerConnectedChanged();
    if (!connected) {
        updatePower(0);
        // no fading average of a trainer that is gone
        m_smoother.clear();
        refreshDisplayPower();
        updateCadence(0);
        updateSpeed(0);
        if (m_speedReported) {
            m_speedReported = false;
            emit trainerReportsSpeedChanged();
        }
    } else if (m_requestedTarget > 0) {
        // re-apply ERG target after (re)connect
        emit targetPowerRequested(m_requestedTarget);
    } else if (m_simActive) {
        emit simulationRequested(m_simGrade, m_simWind, m_simCrr, m_simCw);
    }
}

void SensorHub::setHeartRateConnected(bool connected)
{
    if (m_hrConnected == connected)
        return;
    m_hrConnected = connected;
    emit heartRateConnectedChanged();
}

void SensorHub::expireStale()
{
    if (m_power && (!m_powerAge.isValid() || m_powerAge.elapsed() > m_staleMs)) {
        m_power = 0;
        emit powerChanged();
    }
    refreshDisplayPower();
    if (m_cadence && (!m_cadenceAge.isValid() || m_cadenceAge.elapsed() > m_staleMs)) {
        m_cadence = 0;
        emit cadenceChanged();
    }
    if (m_speed > 0 && (!m_speedAge.isValid() || m_speedAge.elapsed() > m_staleMs)) {
        m_speed = 0;
        emit speedChanged();
    }
    if (m_heartRate && (!m_hrAge.isValid() || m_hrAge.elapsed() > m_staleMs * 2)) {
        m_heartRate = 0;
        emit heartRateChanged();
    }
}

void SensorHub::setTrainerSimulationSupport(int support)
{
    support = std::clamp(support, -1, 1);
    if (support == m_simSupport)
        return;
    m_simSupport = support;
    emit trainerSimulationSupportChanged();
}

void SensorHub::requestTargetPower(int watts)
{
    watts = std::max(0, watts);
    if (watts != m_requestedTarget || m_simActive) {
        m_requestedTarget = watts;
        m_simActive = false;
        emit requestedTargetChanged();
    }
    emit targetPowerRequested(watts);
}

void SensorHub::requestFreeRide()
{
    if (m_requestedTarget != 0 || m_simActive) {
        m_requestedTarget = 0;
        m_simActive = false;
        emit requestedTargetChanged();
    }
    emit freeRideRequested();
}

void SensorHub::requestSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM)
{
    if (!std::isfinite(gradePercent))
        gradePercent = 0;
    const bool changed = m_requestedTarget != 0 || !m_simActive || gradePercent != m_simGrade;
    m_requestedTarget = 0;
    m_simActive = true;
    m_simGrade = gradePercent;
    m_simWind = windSpeedMps;
    m_simCrr = crr;
    m_simCw = cwKgPerM;
    if (changed)
        emit requestedTargetChanged();
    emit simulationRequested(gradePercent, windSpeedMps, crr, cwKgPerM);
}

} // namespace watt
