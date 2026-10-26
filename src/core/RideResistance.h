#pragma once

#include "Gearing.h"

#include <QtGlobal>
#include <limits>

namespace watt {

class SensorHub;

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
class RideResistance
{
public:
    enum class Path { Erg, Simulation, GearSimulation, GearErg };

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
    // Not pedaling: the plain road grade. Clamped to minGrade..maxGrade.
    static double gearGrade(const GearDrive &d, double minGrade = kMinGrade, double maxGrade = kMaxGrade);

    static constexpr int kGearIntervalMs = 1000;
    static constexpr int kMinWattsChange = 2;
    static constexpr double kMinGradeChange = 0.1;   // %
    static constexpr int kMinGradeIntervalMs = 1000;
    static constexpr int kGradeRefreshMs = 3000;
    static constexpr double kMinGrade = -10.0;       // what is sent with 0x11
    static constexpr double kMaxGrade = 20.0;

    // erg: ERG mode; virtualGears: the rider shifts virtually.
    static Path choose(bool erg, bool virtualGears, const SensorHub *hub);
    // Virtual gears can use the trainer's gradient simulation (path a).
    static bool gearSimulationPossible(const SensorHub *hub);
    static const char *pathName(Path p);

    explicit RideResistance(SensorHub *hub) : m_hub(hub) {}

    void reset();          // forget what was sent: the next call sends
    void release();        // trainer free ride + reset()
    void elapse(int ms) { m_sinceMs += ms; }

    // Each returns true when a new value went out (callers emit their
    // targetChanged()).
    bool sendErg(int watts, bool force);
    bool sendGearWatts(int watts, bool force);   // <= 0: release (coasting)
    bool sendGrade(double gradePercent, bool force);

    int lastWatts() const { return m_lastWatts > 0 ? m_lastWatts : 0; }   // last ERG/gear watts sent
    bool sentAnything() const { return m_lastWatts >= 0 || simulating(); }
    bool simulating() const { return m_lastGrade == m_lastGrade; }      // not NaN
    double lastGrade() const { return simulating() ? m_lastGrade : 0.0; }
    // The trainer's last command came from here (nobody sent one since).
    bool lastCommandIsOurs() const;

private:
    SensorHub *m_hub;
    int m_lastWatts = -1;    // -1 nothing, 0 released, > 0 watts
    double m_lastGrade = std::numeric_limits<double>::quiet_NaN();
    int m_sinceMs = 0;
    quint64 m_serial = 0;    // SensorHub::commandSerial() after our last command
};

} // namespace watt
