#include "SensorHub.h"

#include <cmath>

namespace watt {

SensorHub::SensorHub(QObject *parent)
    : QObject(parent)
{
}

void SensorHub::updatePower(int watts)
{
    m_powerAge.start();
    watts = std::max(0, watts);
    if (watts != m_power) {
        m_power = watts;
        emit powerChanged();
    }
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
        updateCadence(0);
        updateSpeed(0);
    } else if (m_requestedTarget > 0) {
        // re-apply ERG target after (re)connect
        emit targetPowerRequested(m_requestedTarget);
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
    if (m_cadence && (!m_cadenceAge.isValid() || m_cadenceAge.elapsed() > m_staleMs)) {
        m_cadence = 0;
        emit cadenceChanged();
    }
    if (m_heartRate && (!m_hrAge.isValid() || m_hrAge.elapsed() > m_staleMs * 2)) {
        m_heartRate = 0;
        emit heartRateChanged();
    }
}

void SensorHub::requestTargetPower(int watts)
{
    watts = std::max(0, watts);
    if (watts != m_requestedTarget) {
        m_requestedTarget = watts;
        emit requestedTargetChanged();
    }
    emit targetPowerRequested(watts);
}

void SensorHub::requestFreeRide()
{
    if (m_requestedTarget != 0) {
        m_requestedTarget = 0;
        emit requestedTargetChanged();
    }
    emit freeRideRequested();
}

} // namespace watt
