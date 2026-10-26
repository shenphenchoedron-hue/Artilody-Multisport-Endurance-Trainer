#include "RideResistance.h"

#include "SensorHub.h"

#include <algorithm>
#include <cmath>

namespace watt {

bool RideResistance::gearSimulationPossible(const SensorHub *hub)
{
    return hub && !hub->trainerSimulationUnsupported() && hub->trainerReportsSpeed();
}

RideResistance::Path RideResistance::choose(bool erg, bool virtualGears, const SensorHub *hub)
{
    if (erg)
        return Path::Erg;
    if (!virtualGears)
        return Path::Simulation;
    return gearSimulationPossible(hub) ? Path::GearSimulation : Path::GearErg;
}

const char *RideResistance::pathName(Path p)
{
    switch (p) {
    case Path::Erg: return "erg";
    case Path::Simulation: return "simulation";
    case Path::GearSimulation: return "gearSimulation";
    case Path::GearErg: return "gearErg";
    }
    return "erg";
}

void RideResistance::reset()
{
    m_lastWatts = -1;
    m_lastGrade = std::numeric_limits<double>::quiet_NaN();
    m_sinceMs = 0;
}

void RideResistance::release()
{
    m_hub->requestFreeRide();
    m_serial = m_hub->commandSerial();
    reset();
}

bool RideResistance::lastCommandIsOurs() const
{
    return m_serial != 0 && m_hub->commandSerial() == m_serial;
}

bool RideResistance::sendErg(int watts, bool force)
{
    if (!force && !simulating() && m_lastWatts == watts)
        return false;
    m_hub->requestTargetPower(watts);
    m_serial = m_hub->commandSerial();
    m_lastWatts = watts;
    m_lastGrade = std::numeric_limits<double>::quiet_NaN();
    m_sinceMs = 0;
    return true;
}

bool RideResistance::sendGearWatts(int watts, bool force)
{
    if (simulating()) {
        force = true;   // coming from a simulation path
        m_lastGrade = std::numeric_limits<double>::quiet_NaN();
    }
    if (watts <= 0) {
        if (force || m_lastWatts != 0) {
            m_hub->requestFreeRide();
            m_serial = m_hub->commandSerial();
            m_lastWatts = 0;
            m_sinceMs = 0;
            return true;
        }
        return false;
    }
    const bool changed = m_lastWatts <= 0 || std::abs(watts - m_lastWatts) >= kMinWattsChange;
    const bool due = m_lastWatts <= 0 || m_sinceMs >= kGearIntervalMs;
    if (force || (changed && due)) {
        m_hub->requestTargetPower(watts);
        m_serial = m_hub->commandSerial();
        m_lastWatts = watts;
        m_sinceMs = 0;
        return true;
    }
    return false;
}

bool RideResistance::sendGrade(double grade, bool force)
{
    if (!std::isfinite(grade))
        grade = 0;
    grade = std::round(std::clamp(grade, kMinGrade, kMaxGrade) * 100.0) / 100.0;
    const bool first = !simulating() || m_lastWatts > 0;
    const bool changed = first || std::abs(grade - m_lastGrade) >= kMinGradeChange - 1e-9;
    const bool due = first || m_sinceMs >= kMinGradeIntervalMs;
    if (!(force || first || (changed && due) || m_sinceMs >= kGradeRefreshMs))
        return false;
    m_hub->requestSimulation(grade, 0.0, SensorHub::kDefaultCrr, SensorHub::kDefaultCw);
    m_serial = m_hub->commandSerial();
    const bool newValue = first || grade != m_lastGrade;
    m_lastGrade = grade;
    m_lastWatts = 0;
    m_sinceMs = 0;
    return newValue;
}

} // namespace watt
