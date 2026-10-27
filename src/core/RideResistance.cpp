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
double gearPower(const RideResistance::GearDrive &d, double v, double floor = RideResistance::kMinGearWatts)
{
    if (d.road.gradePercent < 0 && d.bikeSpeedMps > v + 0.3)
        return floor;   // the road is faster than the pedals (freewheeling)
    return std::clamp(road::powerForSpeed(d.road, v), floor, double(RideResistance::kMaxGearWatts));
}
} // namespace

// --- gear ratio detection ------------------------------------------------------------

void GearRatioDetector::clear()
{
    m_window.clear();
    m_ratio = 0;
}

bool GearRatioDetector::addSample(qint64 ms, double cadenceRpm, double speedMps, int circumferenceMm)
{
    const bool steady = cadenceRpm >= kMinCadenceRpm && cadenceRpm <= kMaxCadenceRpm && speedMps >= kMinSpeedMps
                        && circumferenceMm > 0;
    const double r = steady ? speedMps / (cadenceRpm / 60.0 * circumferenceMm / 1000.0) : 0.0;
    if (!steady || r < kMinRatio || r > kMaxRatio || (!m_window.isEmpty() && ms < m_window.constLast().ms)) {
        m_window.clear();   // not pedaling steadily: start over (the last result stays)
        return false;
    }
    m_window.append({ ms, r });
    while (m_window.size() > 2 && ms - m_window.at(1).ms >= kWindowMs)
        m_window.removeFirst();
    if (m_window.size() < 5 || ms - m_window.constFirst().ms < kWindowMs)
        return false;
    double sum = 0, lo = m_window.constFirst().ratio, hi = lo;
    for (const Sample &s : std::as_const(m_window)) {
        sum += s.ratio;
        lo = std::min(lo, s.ratio);
        hi = std::max(hi, s.ratio);
    }
    const double mean = sum / m_window.size();
    if (hi > mean * (1 + kMaxSpread) || lo < mean * (1 - kMaxSpread))
        return false;   // speed and cadence do not agree yet (accelerating flywheel)
    if (m_ratio > 0 && std::abs(mean - m_ratio) <= m_ratio * kChangeThreshold)
        return false;
    m_ratio = std::round(mean * 1000.0) / 1000.0;
    return true;
}

int RideResistance::gearWatts(const GearDrive &d)
{
    if (d.cadenceRpm < kCoastCadenceRpm)
        return 0;
    const double v = gearSpeed(d);
    if (!(v > 0))
        return 0;
    return int(std::lround(gearPower(d, v)));
}

int RideResistance::lightWatts(const GearDrive &d)
{
    if (d.cadenceRpm < kCoastCadenceRpm)
        return 0;
    const double v = gearSpeed(d);
    if (!(v > 0))
        return 0;
    return int(std::lround(gearPower(d, v, kLightMinWatts)));
}

double RideResistance::referenceSpeed(const GearDrive &d)
{
    if (d.cadenceRpm >= kCoastCadenceRpm)
        return Gearing::speedFor(d.cadenceRpm, d.referenceRatio, d.circumferenceMm);
    if (d.cadenceRpm <= 0)
        return d.trainerSpeedMps;   // trainer without cadence
    return 0.0;
}

double RideResistance::gearGrade(const GearDrive &d, double minGrade, double maxGrade)
{
    const double plain = std::clamp(d.road.gradePercent, minGrade, maxGrade);
    const double vRef = referenceSpeed(d);
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

QString RideResistance::methodName(GearMethod m)
{
    switch (m) {
    case GearMethod::Auto: return QStringLiteral("auto");
    case GearMethod::Gradient: return QStringLiteral("gradient");
    case GearMethod::Erg: return QStringLiteral("erg");
    }
    return QStringLiteral("auto");
}

std::optional<RideResistance::GearMethod> RideResistance::parseMethod(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("auto"))
        return GearMethod::Auto;
    if (t == QLatin1String("gradient") || t == QLatin1String("simulation"))
        return GearMethod::Gradient;
    if (t == QLatin1String("erg"))
        return GearMethod::Erg;
    return std::nullopt;
}

RideResistance::Path RideResistance::choose(bool erg, bool virtualGears, const SensorHub *hub, GearMethod method)
{
    if (erg)
        return Path::Erg;
    if (!virtualGears)
        return Path::Simulation;
    if (method == GearMethod::Erg || !gearSimulationPossible(hub))
        return Path::GearErg;
    return method == GearMethod::Auto ? Path::GearHybrid : Path::GearSimulation;
}

const char *RideResistance::pathName(Path p)
{
    switch (p) {
    case Path::Erg: return "erg";
    case Path::Simulation: return "simulation";
    case Path::GearSimulation: return "gearSimulation";
    case Path::GearErg: return "gearErg";
    case Path::GearHybrid: return "gearHybrid";
    }
    return "erg";
}

void RideResistance::reset()
{
    m_light = -1;
    m_sinceHybridMs = kHybridSwitchMs;
    m_debug = GearDebug();
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

bool RideResistance::sendHybrid(const GearDrive &d, bool force, double liveCadenceRpm, bool cadenceReported,
                                double minGrade, double maxGrade)
{
    m_debug.referenceRatio = d.referenceRatio;
    if (!cadenceReported) {
        // no cadence: the trainer's speed is v_ref (path a), nothing to
        // engage on - the gear grade as with the Gradient method
        m_light = -1;
        m_debug.demandedWatts = gearWatts(d);
        m_debug.wantedGrade = gearGrade(d, minGrade, maxGrade);
        m_debug.referenceSpeedMps = referenceSpeed(d);
        m_debug.decision = QStringLiteral("grade");
        return sendGrade(m_debug.wantedGrade, force, trainerModel(d));
    }
    if (!updateGearEngaged(liveCadenceRpm >= 0 ? liveCadenceRpm : d.cadenceRpm)) {
        m_light = -1;   // decided afresh when pedaling again
        m_debug.demandedWatts = 0;
        m_debug.wantedGrade = std::clamp(d.road.gradePercent, minGrade, maxGrade);
        m_debug.referenceSpeedMps = 0;
        m_debug.decision = QStringLiteral("free");
        return sendGearWatts(0, force);   // coasting: the trainer's minimum
    }
    GearDrive engaged = d;
    engaged.cadenceRpm = std::max(d.cadenceRpm, double(kCoastCadenceRpm));
    const int watts = lightWatts(engaged);
    const double grade = gearGrade(engaged, minGrade, maxGrade);
    const bool wantLight = lightWanted(m_light == 1, grade, watts);
    if (m_light < 0) {
        m_light = wantLight ? 1 : 0;
        m_sinceHybridMs = 0;
    } else if (wantLight != (m_light == 1) && m_sinceHybridMs >= kHybridSwitchMs) {
        m_light = wantLight ? 1 : 0;
        m_sinceHybridMs = 0;
        force = true;
    }
    m_debug.demandedWatts = watts;
    m_debug.wantedGrade = grade;
    m_debug.referenceSpeedMps = referenceSpeed(engaged);
    m_debug.decision = m_light == 1 ? QStringLiteral("erg-light") : QStringLiteral("grade");
    if (m_light == 1)
        return sendGearWatts(watts, force);
    return sendGrade(grade, force, trainerModel(engaged));
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
