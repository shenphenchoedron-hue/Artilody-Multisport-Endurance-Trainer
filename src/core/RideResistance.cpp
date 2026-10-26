#include "RideResistance.h"

#include "SensorHub.h"

#include <algorithm>
#include <cmath>

namespace watt {

bool RideResistance::gearSimulationPossible(const SensorHub *hub)
{
    // the gear-adjusted grade needs the reference gear's speed: from the
    // cadence, else from the trainer's speed
    return hub && !hub->trainerSimulationUnsupported() && (hub->cadenceReported() || hub->trainerReportsSpeed());
}

double RideResistance::gearSpeed(const GearDrive &d)
{
    if (d.cadenceRpm >= kCoastCadenceRpm)
        return Gearing::speedFor(d.cadenceRpm, d.virtualRatio, d.circumferenceMm);
    if (d.trainerSpeedMps > 0 && d.referenceRatio > 0 && d.virtualRatio > 0)
        return d.trainerSpeedMps * d.virtualRatio / d.referenceRatio;
    return 0.0;
}

namespace {
double gearPower(const RideResistance::GearDrive &d, double v)
{
    if (d.road.gradePercent < 0 && d.bikeSpeedMps > v + 0.3)
        return RideResistance::kMinGearWatts;   // the road is faster than the pedals (freewheeling)
    return std::clamp(road::powerForSpeed(d.road, v), double(RideResistance::kMinGearWatts),
                      double(RideResistance::kMaxGearWatts));
}
} // namespace

int RideResistance::gearWatts(const GearDrive &d)
{
    if (d.cadenceRpm < kCoastCadenceRpm)
        return 0;
    const double v = gearSpeed(d);
    if (!(v > 0))
        return 0;
    return int(std::lround(gearPower(d, v)));
}

double RideResistance::gearGrade(const GearDrive &d, double minGrade, double maxGrade)
{
    const double plain = std::clamp(d.road.gradePercent, minGrade, maxGrade);
    double vRef = 0;
    if (d.cadenceRpm >= kCoastCadenceRpm)
        vRef = Gearing::speedFor(d.cadenceRpm, d.referenceRatio, d.circumferenceMm);
    else if (d.cadenceRpm <= 0)
        vRef = d.trainerSpeedMps;   // trainer without cadence
    const double vVirt = gearSpeed(d);
    if (!(vRef >= kMinGearSpeedMps) || !(vVirt > 0))
        return plain;   // coasting / standing: the road as it is
    road::TrainerModel trainer;
    trainer.massKg = d.road.massKg;
    trainer.crr = SensorHub::kDefaultCrr;
    trainer.cwKgPerM = SensorHub::kDefaultCw;
    return road::gradeForTrainerPower(gearPower(d, vVirt), vRef, trainer, minGrade, maxGrade);
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
