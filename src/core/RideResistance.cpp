#include "RideResistance.h"

#include "SensorHub.h"

#include <algorithm>
#include <cmath>

namespace watt {

bool RideResistance::gearSimulationPossible(const SensorHub *hub)
{
    // the gear-adjusted grade needs the reference gear's speed: from the
    // cadence, else from the trainer's speed
    // (right after connecting neither is known yet: assume it may report, so
    // the path does not start in ERG and flip to the simulation a moment later)
    return hub && !hub->trainerSimulationUnsupported()
           && (hub->cadenceReported() || hub->trainerReportsSpeed() || hub->trainerFeedbackUnknown());
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
    // crank power -> the wheel power the trainer takes (power convention in
    // Gearing.h): in the reference gear this is exactly the road grade
    const double eff = d.road.efficiency > 0 ? d.road.efficiency : 1.0;
    return road::gradeForTrainerPower(gearPower(d, vVirt) * eff, vRef, trainerModel(d), minGrade, maxGrade);
}

road::TrainerModel RideResistance::trainerModel(const GearDrive &d)
{
    return road::trainerModelFor(d.road, d.trainerMassKg);
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
    m_gearEngaged = false;
    m_sinceToggleMs = kGearToggleMs;   // the first engagement goes out at once
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

bool RideResistance::updateGearEngaged(double cadenceRpm)
{
    const bool want = m_gearEngaged ? cadenceRpm >= kGearReleaseCadenceRpm : cadenceRpm >= kGearEngageCadenceRpm;
    if (want != m_gearEngaged && m_sinceToggleMs >= kGearToggleMs) {
        m_gearEngaged = want;
        m_sinceToggleMs = 0;
    }
    return m_gearEngaged;
}

bool RideResistance::sendGear(const GearDrive &d, bool force, double liveCadenceRpm)
{
    int watts = 0;
    if (updateGearEngaged(liveCadenceRpm >= 0 ? liveCadenceRpm : d.cadenceRpm)) {
        GearDrive engaged = d;
        // between release and engage cadence the gear keeps its (small) load
        engaged.cadenceRpm = std::max(d.cadenceRpm, double(kCoastCadenceRpm));
        watts = gearWatts(engaged);
    }
    return sendGearWatts(watts, force);
}

bool RideResistance::sendGrade(double grade, bool force, const road::TrainerModel &trainer)
{
    if (!std::isfinite(grade))
        grade = 0;
    grade = std::round(std::clamp(grade, kMinGrade, kMaxGrade) * 100.0) / 100.0;
    // new rider parameters (weight / CdA edited) go out like a new road
    const bool first = !simulating() || m_lastWatts > 0 || trainer.crr != m_lastCrr || trainer.cwKgPerM != m_lastCw;
    const bool changed = first || std::abs(grade - m_lastGrade) >= kMinGradeChange - 1e-9;
    const bool due = first || m_sinceMs >= kMinGradeIntervalMs;
    if (!(force || first || (changed && due) || m_sinceMs >= kGradeRefreshMs))
        return false;
    m_hub->requestSimulation(grade, 0.0, trainer.crr, trainer.cwKgPerM);
    m_serial = m_hub->commandSerial();
    m_lastCrr = trainer.crr;
    m_lastCw = trainer.cwKgPerM;
    const bool newValue = first || grade != m_lastGrade;
    m_lastGrade = grade;
    m_lastWatts = 0;
    m_sinceMs = 0;
    return newValue;
}

} // namespace watt
