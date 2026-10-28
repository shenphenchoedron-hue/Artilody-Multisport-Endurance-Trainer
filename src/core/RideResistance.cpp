#include "RideResistance.h"

#include "DiagLog.h"
#include "SensorHub.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace watt {

namespace {
std::atomic<bool> g_softStartEnabled { true };
} // namespace

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
    m_confirmations = 0;
    m_lastResultMs = 0;
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
    if (m_ratio > 0 && std::abs(mean - m_ratio) <= m_ratio * kChangeThreshold) {
        if (ms - m_lastResultMs >= kWindowMs) {
            ++m_confirmations;   // another full steady window agrees
            m_lastResultMs = ms;
        }
        return false;
    }
    m_ratio = std::round(mean * 1000.0) / 1000.0;
    m_confirmations = 0;
    m_lastResultMs = ms;
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
    const double vRef = referenceSpeed(d);
    const double vVirt = gearSpeed(d);
    if (!(vRef >= kMinGearSpeedMps) || !(vVirt > 0))
        return std::clamp(0.0, minGrade, maxGrade);   // coasting / standing: 0 % (soft start), not the road grade
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
    // Zwift hub protocol chosen by the rider and its handshake done: the
    // trainer does the physics (never ERG); otherwise the FTMS paths below.
    // While its handshake runs (Click connected, trainer with the Zwift
    // service) as well: nothing goes to the trainer until the hub is active
    // (BleTrainer keeps the request), no FTMS ERG / 0x11 meanwhile - Zwift
    // never mixes FTMS commands into the hub protocol.
    if (hub && (hub->trainerZwiftHubActive()
                || (hub->zwiftHubWanted() && hub->trainerZwiftHubState() == SensorHub::HubStarting)))
        return Path::GearHub;
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
    case Path::GearHub: return "gearZwiftHub";
    }
    return "erg";
}

// --- soft start --------------------------------------------------------------------------

void RideResistance::setSoftStartEnabled(bool on)
{
    g_softStartEnabled.store(on);
}

bool RideResistance::softStartEnabled()
{
    return g_softStartEnabled.load();
}

double RideResistance::softStartFactor() const
{
    if (!softStartEnabled())
        return 1.0;
    switch (m_soft) {
    case SoftStart::Waiting: return 0.0;
    case SoftStart::Ramping: return std::clamp(double(m_softRampMs) / kSoftStartRampMs, 0.0, 1.0);
    case SoftStart::Done: return 1.0;
    }
    return 1.0;
}

double RideResistance::softStartGrade(double target) const
{
    if (!std::isfinite(target))
        return 0.0;
    return std::round(target * softStartFactor() * 100.0) / 100.0 + 0.0;   // + 0.0: no "-0.00"
}

void RideResistance::armSoftStart()
{
    if (m_softUsed && m_soft != SoftStart::Waiting)
        DiagLog::write("RES", QStringLiteral("soft start re-armed: 0 % until the flywheel turns again"));
    m_soft = SoftStart::Waiting;
    m_softTurningMs = 0;
    m_softStoppedMs = 0;
    m_softRampMs = 0;
    m_softUsed = false;
}

void RideResistance::noteSoftTarget(double target)
{
    m_softTarget = target;
    if (!softStartEnabled())
        return;
    if (!m_softUsed && m_soft == SoftStart::Waiting)
        DiagLog::write("RES", QStringLiteral("soft start begin: sending 0 % instead of %1 % until the flywheel turns "
                                             "(cadence >= %2 rpm or speed >= %3 km/h for %4 s)")
                                  .arg(target, 0, 'f', 2)
                                  .arg(kSoftStartCadenceRpm, 0, 'f', 0)
                                  .arg(kSoftStartSpeedKmh, 0, 'f', 0)
                                  .arg(kSoftStartTurnMs / 1000));
    m_softUsed = true;
}

void RideResistance::updateSoftStart(int ms)
{
    if (!m_hub || ms <= 0 || !softStartEnabled())
        return;
    const int cad = m_hub->cadence();
    const double speed = m_hub->speedKmh();
    const int power = m_hub->power();
    const bool cadRep = m_hub->cadenceReported();
    const bool spdRep = m_hub->trainerReportsSpeed();
    const bool turning = cad >= kSoftStartCadenceRpm || speed >= kSoftStartSpeedKmh
                         || (!cadRep && !spdRep && power >= kSoftStartPowerWatts);
    // stopped: no cadence; without a cadence the flywheel (nearly) still;
    // a trainer reporting neither: no power either
    const bool stopped = cad <= 0 && (cadRep || speed < kSoftStartStopSpeedKmh) && (cadRep || spdRep || power <= 0);
    m_softTurningMs = turning ? m_softTurningMs + ms : 0;
    m_softStoppedMs = stopped ? m_softStoppedMs + ms : 0;
    switch (m_soft) {
    case SoftStart::Waiting:
        if (m_softTurningMs >= kSoftStartTurnMs) {
            m_soft = SoftStart::Ramping;
            m_softRampMs = 0;
            if (m_softUsed)
                DiagLog::write("RES", QStringLiteral("soft start: flywheel turning (cadence %1 rpm, speed %2 km/h) - ramping to %3 % over %4 s")
                                          .arg(cad)
                                          .arg(speed, 0, 'f', 1)
                                          .arg(m_softTarget, 0, 'f', 2)
                                          .arg(kSoftStartRampMs / 1000));
        }
        break;
    case SoftStart::Ramping:
    case SoftStart::Done:
        if (m_softStoppedMs > kSoftStartRearmMs) {
            armSoftStart();
            break;
        }
        if (m_soft == SoftStart::Ramping) {
            m_softRampMs += ms;
            if (m_softRampMs >= kSoftStartRampMs) {
                m_soft = SoftStart::Done;
                if (m_softUsed)
                    DiagLog::write("RES", QStringLiteral("soft start end: full grade %1 %").arg(m_softTarget, 0, 'f', 2));
            }
        }
        break;
    }
}

void RideResistance::reset()
{
    armSoftStart();
    m_lastWasHub = false;
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

HubSimulation RideResistance::hubSimulationFor(const GearDrive &d)
{
    HubSimulation p;
    p.inclinePercent = std::isfinite(d.road.gradePercent)
                           ? std::round(std::clamp(d.road.gradePercent, kMinGrade, kMaxGrade) * 100.0) / 100.0
                           : 0.0;
    // Wind, CWa and Crr exactly as Zwift sends them to these trainers (wind 0,
    // CWa 5100, Crr 400; Incyclist uses the same defaults): the trainer's
    // physics are tuned to them (Makinolo: other values make it "behave in very
    // strange ways"). v1.3.3 sent the rider's own CdA-based value (e.g. 0.37,
    // later 0.3899), re-sent on every change.
    p.windMps = 0;
    p.cw = kHubCwa;
    p.crr = kHubCrr;
    p.gearRatio = d.virtualRatio;
    const double total = d.road.massKg;
    p.riderKg = d.riderKg > 0 && d.riderKg < total ? d.riderKg : std::max(20.0, total - road::kDefaultBikeKg);
    p.bikeKg = std::max(0.0, total - p.riderKg);
    return p;
}

bool RideResistance::sendHub(const GearDrive &d)
{
    HubSimulation p = hubSimulationFor(d);
    noteSoftTarget(p.inclinePercent);
    p.inclinePercent = softStartGrade(p.inclinePercent);   // soft start: 0 % / ramp
    m_hub->requestHubSimulation(p);
    m_serial = m_hub->commandSerial();
    const bool newValue = !m_lastWasHub || p.inclinePercent != m_lastGrade;
    m_lastWasHub = true;
    m_lastGrade = p.inclinePercent;
    m_lastWatts = 0;
    m_sinceMs = 0;
    m_debug.wantedGrade = p.inclinePercent;
    m_debug.referenceRatio = d.referenceRatio;
    m_debug.demandedWatts = 0;
    m_debug.referenceSpeedMps = 0;
    m_debug.decision = QStringLiteral("zwift-hub");
    return newValue;
}

bool RideResistance::sendErg(int watts, bool force)
{
    if (m_lastWasHub) {
        force = true;
        m_lastWasHub = false;
    }
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
    m_lastWasHub = false;
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
    noteSoftTarget(grade);
    grade = softStartGrade(grade);   // soft start: 0 % until the flywheel turns, then the ramp
    // new rider parameters (weight / CdA edited) go out like a new road
    // leaving the Zwift hub path (handshake failed, option off): FTMS again at once
    const bool first = !simulating() || m_lastWasHub || m_lastWatts > 0 || trainer.crr != m_lastCrr
                       || trainer.cwKgPerM != m_lastCw;
    m_lastWasHub = false;
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
