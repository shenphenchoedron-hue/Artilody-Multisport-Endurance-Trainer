#pragma once

#include "Gearing.h"
#include "SensorHub.h"

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <limits>
#include <optional>

namespace watt {

class SensorHub;

// The real gear on the trainer from the trainer's speed and cadence:
//     R_real = v / (cadence/60 · circumference)
// (direct-drive trainers report the speed of their flywheel as a wheel speed,
// so this is the chainring/cog ratio the bike sits in). Only steady pedaling
// counts: kMinCadenceRpm..kMaxCadenceRpm, at least kMinSpeedMps, and all
// ratios of the last kWindowMs within ±kMaxSpread of their mean. The result
// stays until a new steady window gives another one (clear() forgets it).
class GearRatioDetector
{
public:
    static constexpr double kMinCadenceRpm = 50;
    static constexpr double kMaxCadenceRpm = 130;
    static constexpr double kMinSpeedMps = 3.0;
    static constexpr int kWindowMs = 6000;
    static constexpr double kMaxSpread = 0.03;     // ±3 %
    static constexpr double kMinRatio = 0.6;
    static constexpr double kMaxRatio = 6.0;
    static constexpr double kChangeThreshold = 0.02;   // a new result differs by more

    // A result counts as stable once later steady windows (at most one per
    // kWindowMs) confirmed it kStableConfirmations times (~18 s of steady
    // pedaling): only then the app suggests it to the rider.
    static constexpr int kStableConfirmations = 2;

    // true when ratio() changed
    bool addSample(qint64 ms, double cadenceRpm, double speedMps, int circumferenceMm);
    bool valid() const { return m_ratio > 0; }
    double ratio() const { return m_ratio; }
    int confirmations() const { return m_confirmations; }
    bool stable() const { return valid() && m_confirmations >= kStableConfirmations; }
    // ratio of the last steady sample (diagnostics), 0 = none
    double lastSampleRatio() const { return m_window.isEmpty() ? 0.0 : m_window.constLast().ratio; }
    void clear();

private:
    struct Sample { qint64 ms; double ratio; };
    QVector<Sample> m_window;
    double m_ratio = 0;
    int m_confirmations = 0;
    qint64 m_lastResultMs = 0;
};

// Resistance commands of the riding sessions (Free Ride, Video Ride), rate
// limited, through SensorHub (-> device layer -> FTMS).
//
// Paths (choose()):
//   Erg            - fixed watts (FTMS Set Target Power), sent at once on change.
//   Simulation     - real gears: the trainer gets the road grade (FTMS 0x11).
//   GearSimulation - virtual gears on a simulating trainer ("path a"): the
//                    trainer gets a gear-adjusted grade (gearGrade()), the
//                    bike stays in the reference gear. Needs the cadence or
//                    the trainer's speed.
//   GearErg        - virtual gears via ERG ("path b"): the trainer holds the
//                    road power for the virtual speed (gearWatts()). Used
//                    when the trainer cannot simulate gradients (or reports
//                    neither cadence nor speed).
// Free Ride and Video Ride use exactly these functions for virtual gears
// (same physics, same rate limits) - only the road grade differs.
// Grades go out at the first call, then when they moved by >= kMinGradeChange
// but at most every kMinGradeIntervalMs, refreshed every kGradeRefreshMs.
// Gear watts at most every kGearIntervalMs and only when they moved by
// >= kMinWattsChange. force sends right away (shift, mode or grade change).
//
//   GearHybrid     - virtual gears, "Auto" shifting method (default) on a
//                    simulating trainer that reports cadence: path a for
//                    real loads, ERG at the gear's watts (floor
//                    kLightMinWatts) for light ones (sendHybrid()).
//
// Why the hybrid: in path a the flywheel turns at the speed of the REAL gear
// (v_ref = cadence × R_ref × circumference: 50×17 at 90 rpm = 33 km/h). A
// trainer simulating 0 % at 33 km/h brakes with rolling + air drag of that
// speed, ~185 W for a 85 kg rider - whatever the virtual gear. A light virtual
// gear (44×44 = 11 km/h, ~20 W) therefore needs a NEGATIVE grade (≈ −2.2 %),
// which many FTMS trainers clamp to 0 % or answer with a high minimum
// resistance: "200 W at the start of a descent in the lightest gear". ERG at
// the gear's watts asks the brake for (almost) nothing instead.
//
// Virtual shifting method (VirtualGears::shiftMethod, "Devices > Virtual gears"):
//   Gradient - DEFAULT: GearSimulation (simulation, the 1.3.1 behaviour),
//              GearErg only without gradient simulation
//   Auto     - optional / experimental, only when the rider picks it:
//              GearHybrid (trainer without cadence / gradient simulation:
//              GearSimulation / GearErg as before)
//   Erg      - optional / experimental: always GearErg
class RideResistance
{
public:
    // GearHub: virtual gears through the Zwift hub protocol (the trainer gets
    // incline + gear ratio + weights and does the physics itself); only while
    // SensorHub::trainerZwiftHubActive() - otherwise the FTMS paths.
    enum class Path { Erg, Simulation, GearSimulation, GearErg, GearHybrid, GearHub };
    enum class GearMethod { Auto, Gradient, Erg };
    static QString methodName(GearMethod m);                       // "auto" / "gradient" / "erg"
    static std::optional<GearMethod> parseMethod(const QString &text);

    // What the virtual gear resistance depends on (both pages).
    struct GearDrive
    {
        road::Model road;            // rider + bike on the (effective) road grade
        double cadenceRpm = 0;       // < kCoastCadenceRpm: not pedaling / no cadence
        double virtualRatio = 0;     // current virtual gear (chainring / cog)
        double referenceRatio = 0;   // the real gear the bike sits in on the trainer
        int circumferenceMm = GearSetup::kDefaultCircumferenceMm;
        double trainerSpeedMps = 0;  // the trainer's reported speed (fallback without cadence)
        double bikeSpeedMps = -1;    // the session's road speed: faster than the pedals
                                     // downhill = freewheeling (< 0: unknown)
        double trainerMassKg = 0;    // the trainer's own rider weight setting, 0 = unknown
                                     // (assumed to be road.massKg)
        double riderKg = 0;          // rider alone (road.massKg - riderKg = bike), 0 = unknown
    };
    static constexpr int kCoastCadenceRpm = 15;
    static constexpr int kMinGearWatts = 20;     // lowest gear power while pedaling
    static constexpr int kMaxGearWatts = 1500;
    static constexpr double kMinGearSpeedMps = 1.0;

    // Virtual speed v_v = cadence/60 · R_virtual · circumference (no cadence:
    // trainer speed · R_virtual / R_reference). 0 when unknown.
    static double gearSpeed(const GearDrive &d);
    // Road power at the virtual speed, kMinGearWatts..kMaxGearWatts; the
    // minimum when the bike rolls faster than the pedals turn it downhill or
    // the road power would be lower (descents). 0 = not pedaling (release).
    // Monotonic: a harder gear at the same cadence never gives less.
    static int gearWatts(const GearDrive &d);
    // Path a: the grade that makes the trainer, turning at the speed of the
    // reference gear v_ref, brake with gearWatts():
    //     v_ref = cadence/60 · R_reference · circumference
    // (taken from the cadence, not the trainer's speed: a heavier brake slows
    // the flywheel, and a speed-based grade would then drop with it - the
    // gear must decide, not the feedback; trainer speed only without cadence).
    // Standing / coasting (v_ref < kMinGearSpeedMps): 0 % (the soft start
    // takes over, see kSoftStartCadenceRpm). Clamped to minGrade..maxGrade.
    // The trainer is modelled with exactly the parameters sendGrade() sends
    // (trainerModel()): Crr and Cw = ρ·CdA of the rider, mass = rider + bike.
    static double gearGrade(const GearDrive &d, double minGrade = kMinGrade, double maxGrade = kMaxGrade);
    // FTMS 0x11 parameters for this rider (road::trainerModelFor()).
    static road::TrainerModel trainerModel(const GearDrive &d);

    // GearErg engages / releases with hysteresis around kCoastCadenceRpm
    // (release below kGearReleaseCadenceRpm, engage from
    // kGearEngageCadenceRpm) and at most once per kGearToggleMs, so a cadence
    // hovering near 15 rpm does not flap between ERG and free ride.
    static constexpr int kGearReleaseCadenceRpm = 12;
    static constexpr int kGearEngageCadenceRpm = 18;
    static constexpr int kGearToggleMs = 1000;
    static constexpr int kGearIntervalMs = 1000;
    static constexpr int kMinWattsChange = 2;
    static constexpr double kMinGradeChange = 0.1;   // %
    static constexpr int kMinGradeIntervalMs = 1000;
    static constexpr int kGradeRefreshMs = 3000;
    static constexpr double kMinGrade = -10.0;       // what is sent with 0x11
    static constexpr double kMaxGrade = 20.0;

    // GearHybrid ("Auto"): light = ERG at lightWatts(), else the gear grade.
    // Light when the grade path a would send is <= kLightEnterGrade or the
    // gear asks for less than kLightEnterWatts; back to the grade only when
    // it is > kLightExitGrade AND >= kLightExitWatts; at most one switch per
    // kHybridSwitchMs. Coasting (cadence below kGearReleaseCadenceRpm):
    // released (free ride) like GearErg.
    static constexpr double kLightEnterGrade = 0.0;   // %
    static constexpr double kLightExitGrade = 0.5;    // %
    static constexpr int kLightEnterWatts = 60;
    static constexpr int kLightExitWatts = 75;
    static constexpr int kLightMinWatts = 10;         // ERG floor of the light gears
    static constexpr int kHybridSwitchMs = 1000;

    // The gear's demanded crank power for the light ERG: like gearWatts() but
    // floored at kLightMinWatts. 0 = not pedaling.
    static int lightWatts(const GearDrive &d);
    // The hybrid decision with its hysteresis (light: currently ERG-light).
    static bool lightWanted(bool light, double gradeToSend, int demandedWatts)
    {
        return light ? !(gradeToSend > kLightExitGrade && demandedWatts >= kLightExitWatts)
                     : (gradeToSend <= kLightEnterGrade || demandedWatts < kLightEnterWatts);
    }
    // The trainer speed path a assumes (reference gear at the cadence, else
    // the trainer's speed); 0 = unknown.
    static double referenceSpeed(const GearDrive &d);

    // erg: ERG mode; virtualGears: the rider shifts virtually.
    static Path choose(bool erg, bool virtualGears, const SensorHub *hub, GearMethod method = GearMethod::Gradient);
    // Virtual gears can use the trainer's gradient simulation (path a).
    static bool gearSimulationPossible(const SensorHub *hub);
    static const char *pathName(Path p);

    // --- soft start (gradient simulation only, never ERG) ---------------------
    // At the start of a ride (reset()) and after the rider stopped, the
    // trainer does not get the route's grade (or the gear-adjusted grade) at
    // once: 0 % until the flywheel turns, then a linear ramp to the real grade.
    //   turning = cadence >= kSoftStartCadenceRpm OR trainer speed >=
    //             kSoftStartSpeedKmh (a trainer reporting neither: power >=
    //             kSoftStartPowerWatts), for kSoftStartTurnMs without a break
    //   ramp    = sent grade = target × t / kSoftStartRampMs
    //   re-arm  = stopped for more than kSoftStartRearmMs: cadence 0 (a trainer
    //             without cadence: speed < kSoftStartStopSpeedKmh; neither:
    //             power 0)
    // Applies to sendGrade() (Simulation, GearSimulation, the grade side of
    // GearHybrid) and sendHub()'s incline (Zwift hub path). The rate limits of
    // sendGrade() stay (a ramp step at most every kMinGradeIntervalMs);
    // latest-wins is unchanged: the target is taken afresh on every call.
    static constexpr double kSoftStartCadenceRpm = 40;
    static constexpr double kSoftStartSpeedKmh = 10;
    static constexpr int kSoftStartPowerWatts = 50;
    static constexpr int kSoftStartTurnMs = 2000;
    static constexpr int kSoftStartRampMs = 4000;
    static constexpr int kSoftStartRearmMs = 5000;
    static constexpr double kSoftStartStopSpeedKmh = 3;
    enum class SoftStart { Waiting, Ramping, Done };
    SoftStart softStart() const { return m_soft; }
    // 0 while waiting, 0..1 during the ramp, 1 when done (or disabled)
    double softStartFactor() const;
    // the grade that goes out for target (target × softStartFactor())
    double softStartGrade(double target) const;
    // Tests of unrelated behaviour may switch it off (process-wide, default on).
    static void setSoftStartEnabled(bool on);
    static bool softStartEnabled();

    explicit RideResistance(SensorHub *hub) : m_hub(hub) {}

    void reset();          // forget what was sent: the next call sends; arms the soft start
    void release();        // trainer free ride + reset()
    void elapse(int ms)
    {
        m_sinceMs += ms;
        m_sinceToggleMs += ms;
        m_sinceHybridMs += ms;
        updateSoftStart(ms);
    }

    // Each returns true when a new value went out (callers emit their
    // targetChanged()).
    bool sendErg(int watts, bool force);
    bool sendGearWatts(int watts, bool force);   // <= 0: release (coasting)
    // GearErg for a drive: gearWatts() with the engage/release hysteresis
    // (see kGearReleaseCadenceRpm) on the live cadence (< 0: d.cadenceRpm),
    // released (free ride) while disengaged.
    bool sendGear(const GearDrive &d, bool force, double liveCadenceRpm = -1);
    // Hysteresis state for the cadence (updates it, rate limited).
    bool updateGearEngaged(double cadenceRpm);
    bool gearEngaged() const { return m_gearEngaged; }
    // FTMS 0x11 with the trainer model's Crr and Cw (its mass is not sent:
    // FTMS has no weight field).
    bool sendGrade(double gradePercent, bool force, const road::TrainerModel &trainer);
    // GearHybrid (see kLightEnterGrade). cadenceReported false: the gear grade
    // (path a, the trainer's speed stands in for the cadence).
    // Path GearHub: the road (incline = d.road.gradePercent, CdA, Crr) and
    // the virtual gear ratio + weights to the trainer's own physics (Zwift hub
    // protocol). Rate limits are the driver's (ZwiftHubLink): every call is
    // forwarded, a gear change goes out at once.
    bool sendHub(const GearDrive &d);
    static HubSimulation hubSimulationFor(const GearDrive &d);
    bool sendHybrid(const GearDrive &d, bool force, double liveCadenceRpm, bool cadenceReported,
                    double minGrade = kMinGrade, double maxGrade = kMaxGrade);
    bool hybridLight() const { return m_light == 1; }

    // What the last gear command was based on (diagnostics / WATT_DEBUG_GEARS).
    struct GearDebug
    {
        int demandedWatts = 0;          // the gear's road power (crank)
        double wantedGrade = 0;         // the grade path a would send
        double referenceSpeedMps = 0;   // v_ref
        double referenceRatio = 0;
        QString decision;               // "grade" / "erg-light" / "free" / ""
    };
    const GearDebug &gearDebug() const { return m_debug; }

    int lastWatts() const { return m_lastWatts > 0 ? m_lastWatts : 0; }   // last ERG/gear watts sent
    bool sentAnything() const { return m_lastWatts >= 0 || simulating(); }
    bool simulating() const { return m_lastGrade == m_lastGrade; }      // not NaN
    bool lastWasHub() const { return m_lastWasHub; }
    double lastGrade() const { return simulating() ? m_lastGrade : 0.0; }
    // The trainer's last command came from here (nobody sent one since).
    bool lastCommandIsOurs() const;

private:
    void updateSoftStart(int ms);
    void armSoftStart();
    void noteSoftTarget(double target);

    SensorHub *m_hub;
    SoftStart m_soft = SoftStart::Waiting;
    int m_softTurningMs = 0;      // flywheel turning without a break
    int m_softStoppedMs = 0;      // stopped without a break
    int m_softRampMs = 0;         // into the ramp
    bool m_softUsed = false;      // a grade / incline went out since arming (logging)
    double m_softTarget = 0;      // last target grade (logging)
    int m_lastWatts = -1;    // -1 nothing, 0 released, > 0 watts
    bool m_lastWasHub = false;   // the last command went through the Zwift hub protocol
    double m_lastGrade = std::numeric_limits<double>::quiet_NaN();
    double m_lastCrr = 0, m_lastCw = 0;
    int m_sinceMs = 0;
    bool m_gearEngaged = false;
    int m_sinceToggleMs = kGearToggleMs;
    int m_light = -1;                     // hybrid: -1 undecided, 0 grade, 1 light ERG
    int m_sinceHybridMs = kHybridSwitchMs;
    GearDebug m_debug;
    quint64 m_serial = 0;    // SensorHub::commandSerial() after our last command
};

} // namespace watt
