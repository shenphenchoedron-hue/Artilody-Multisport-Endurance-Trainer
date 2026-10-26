#pragma once

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
//   GearSimulation - virtual gears on a simulating trainer that reports its
//                    speed ("path a"): the trainer gets a gear-adjusted grade
//                    (road::gearSimulationGrade), the bike stays in the
//                    reference gear.
//   GearErg        - virtual gears via ERG ("path b"): the trainer holds the
//                    road power for the virtual speed. Used when the trainer
//                    cannot simulate gradients or does not report speed.
// Grades go out at the first call, then when they moved by >= kMinGradeChange
// but at most every kMinGradeIntervalMs, refreshed every kGradeRefreshMs.
// Gear watts at most every kGearIntervalMs and only when they moved by
// >= kMinWattsChange. force sends right away (shift, mode or grade change).
class RideResistance
{
public:
    enum class Path { Erg, Simulation, GearSimulation, GearErg };

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
