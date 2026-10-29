#pragma once

#include "Gearing.h"
#include "Profile.h"
#include "Ride.h"
#include "RideResistance.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace watt {

class Database;
class ProfileManager;
class SensorHub;
class TrainingSession;
class VirtualGears;

// Free ride without a workout. Exposed to QML as "FreeRide" (Artilody.Backend).
//
// Modes ("mode", setting "freeRide/mode"):
//   "ride" - a simulated road with the grade setting (gradePercent), ridden
//            like outdoors:
//            * virtual gears off: the rider shifts the real bike, the trainer
//              simulates the grade (FTMS 0x11, RideResistance::Simulation);
//              speed from the measured power with inertia
//              (road::integrateSpeed).
//            * virtual gears on (VirtualGears::enabled, e.g. a Zwift Click):
//              the virtual gear sets the speed
//                  v = cadence / 60 · (chainring / cog) · wheel circumference
//              and the road power P = road::powerForSpeed(v, grade) is what the
//              rider pushes, through RideResistance::GearSimulation ("path a":
//              gear-adjusted grade RideResistance::gearGrade() for the
//              cadence, trainer simulates) or RideResistance::GearErg ("path
//              b": ERG target P = RideResistance::gearWatts(), every other
//              trainer) - the same functions as the Video Ride. With the page
//              open (before Start) the trainer already gets this resistance
//              (gearHolding()), so shifting changes the watts before Start. Coasting releases the trainer and the speed rolls out
//              (road::coastDeceleration); descents may roll faster than the
//              pedals turn.
//            * trainer without gradient simulation and virtual gears off:
//              virtual gears via ERG anyway (simulationFallback), otherwise a
//              climb could not get harder.
//   "erg"  - hold targetWatts (ERG) as long as you like; speed from power on
//            a flat road.
//   Stored "gears" / "free" (older versions) are migrated to "ride".
// States ("state"): "idle", "running", "paused", "finished". A ride only
// starts with start() (Start button / Space): pedaling never starts or
// resumes it.
//
// Only one session (cycling workout, rowing, free ride) runs at a time:
// setTrainingSession() installs a start guard on the workout session, further
// guards come from setStartBlocker() (main.cpp wires rowing).
// Rides are stored as sport "cycling", workout name "Free Ride", with the
// virtual speed per second and the virtual distance (FIT/CSV export).
class FreeRideSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)        // running or paused
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    // Genuinely in progress: riding, or paused with something recorded. Only
    // such a session blocks starting another one; one paused before anything
    // was recorded is given up instead (abandon()).
    Q_PROPERTY(bool inProgress READ inProgress NOTIFY stateChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool ergEnabled READ ergEnabled NOTIFY modeChanged)   // read-only: mode == "erg"
    // The trainer holds the ERG target outside a ride (ERG selected on the open
    // Free Ride page before Start; see holdWanted()).
    Q_PROPERTY(bool ergHolding READ ergHolding NOTIFY targetChanged)
    // The trainer has the virtual gear resistance outside a ride (Ride with
    // virtual gears on the open Free Ride page before Start): shifting is
    // felt - and the watts follow the gear - before Start.
    Q_PROPERTY(bool gearHolding READ gearHolding NOTIFY targetChanged)

    // live / session values
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY tick)
    Q_PROPERTY(double distanceMeters READ distanceMeters NOTIFY tick)
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY metricsChanged)
    Q_PROPERTY(double avgSpeedKmh READ avgSpeedKmh NOTIFY tick)
    Q_PROPERTY(int watts READ watts NOTIFY metricsChanged)
    Q_PROPERTY(int avgWatts READ avgWatts NOTIFY tick)
    Q_PROPERTY(int maxWatts READ maxWatts NOTIFY tick)
    Q_PROPERTY(double kilojoules READ kilojoules NOTIFY tick)
    Q_PROPERTY(int cadence READ cadence NOTIFY metricsChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY metricsChanged)
    Q_PROPERTY(bool coasting READ coasting NOTIFY metricsChanged)

    // resistance
    Q_PROPERTY(bool virtualGears READ virtualGears NOTIFY modeChanged)              // the virtual gear sets the resistance
    Q_PROPERTY(bool simulationFallback READ simulationFallback NOTIFY modeChanged)  // ride mode, trainer cannot simulate
    Q_PROPERTY(QString resistancePath READ resistancePath NOTIFY modeChanged)       // RideResistance::pathName()
    Q_PROPERTY(int targetWatts READ targetWatts WRITE setTargetWatts NOTIFY targetChanged)       // ERG target
    Q_PROPERTY(int gearTargetWatts READ gearTargetWatts NOTIFY targetChanged)   // road power of the virtual gear
    Q_PROPERTY(int requestedWatts READ requestedWatts NOTIFY targetChanged)     // last watts sent (0 = none)
    Q_PROPERTY(double simulationGradePercent READ simulationGradePercent NOTIFY targetChanged)   // last grade sent
    Q_PROPERTY(bool simulationActive READ simulationActive NOTIFY targetChanged)
    Q_PROPERTY(double gradePercent READ gradePercent WRITE setGrade NOTIFY gradeChanged)
    Q_PROPERTY(int minTargetWatts READ minTargetWatts CONSTANT)
    Q_PROPERTY(int maxTargetWatts READ maxTargetWatts CONSTANT)
    Q_PROPERTY(double minGrade READ minGrade CONSTANT)
    Q_PROPERTY(double maxGrade READ maxGrade CONSTANT)

    // charts: mean per seriesStep seconds (<= kMaxSeriesPoints values)
    Q_PROPERTY(QVariantList powerSeries READ powerSeries NOTIFY seriesChanged)
    Q_PROPERTY(QVariantList speedSeries READ speedSeries NOTIFY seriesChanged)        // km/h
    Q_PROPERTY(QVariantList heartRateSeries READ heartRateSeries NOTIFY seriesChanged)
    Q_PROPERTY(int seriesStep READ seriesStep NOTIFY seriesChanged)

    // devices
    Q_PROPERTY(bool trainerConnected READ trainerConnected NOTIFY devicesChanged)
    // -1 from start() / reset() / discard until the ride is saved
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY lastRideIdChanged)
    // Informational target of a Free Ride planned in the calendar (seconds,
    // 0 = none; set by Planner.startEntry(), cleared when the plan entry is
    // done or another entry is started). Nothing stops at this time.
    Q_PROPERTY(int plannedDurationSec READ plannedDurationSec WRITE setPlannedDurationSec NOTIFY plannedDurationChanged)

public:
    enum class Mode { Ride, Erg };
    enum class State { Idle, Running, Paused, Finished };

    static constexpr int kPedalPowerWatts = 30;     // below (and no cadence): not pedaling
    // Zwift protocol, coasting on the flat / uphill: the speed follows the
    // trainer's simulated speed (VirtualGears::hubTrainerSpeedMps) down,
    // braking at most this much harder than the road model (no jump)
    static constexpr double kHubCoastCatchUpMps2 = RideResistance::kHubCoastCatchUpMps2;
    // Zwift protocol: the flywheel stopped - the bike brakes to 0 at this rate
    // instead of jumping to 0 (RideResistance::hubSpeedStep)
    static constexpr double kHubStopDecelMps2 = RideResistance::kHubStopDecelMps2;
    static constexpr int kCoastCadenceRpm = RideResistance::kCoastCadenceRpm;   // below: not pedaling (coasting)
    static constexpr int kMinGearWatts = RideResistance::kMinGearWatts;         // lowest gears-mode target while pedaling
    static constexpr int kMinErgWatts = 25;
    static constexpr int kMaxWatts = 1500;
    static constexpr int kDefaultErgWatts = 150;    // without a profile FTP
    static constexpr double kDefaultErgFtpShare = 0.6;   // first ERG target: 60 % FTP
    static constexpr int kControllerErgStep = 10;   // Zwift Click +/- in ERG mode
    static constexpr int kTargetIntervalMs = RideResistance::kGearIntervalMs;
    static constexpr int kMinTargetChangeWatts = RideResistance::kMinWattsChange;
    static constexpr double kCadenceSmoothingSec = 1.5;
    static constexpr double kMinGrade = -15.0;
    static constexpr double kMaxGrade = 20.0;
    static constexpr int kMaxSeriesPoints = 720;

    FreeRideSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears,
                    QObject *parent = nullptr);
    ~FreeRideSession() override;

    // Mutual exclusion with the workout session (installs its start guard,
    // gives it up when it is paused without a recording).
    void setTrainingSession(TrainingSession *training);
    // Another session started / paused / ended (SessionGuard): the page's
    // resistance hold follows (holdBlocked()).
    void otherSessionChanged() { updateHold(); }
    // Further guards; while one returns true, start() is refused.
    void setStartBlocker(const QString &key, std::function<bool()> blocker);
    bool startBlocked() const;
    // Called by start() once the guards passed: gives up another session that
    // has nothing to lose (paused before anything was recorded). Empty
    // function removes it.
    void setAbandonCanceller(const QString &key, std::function<void()> cancel);

    // Tests
    void setManualClock(bool manual) { m_manualClock = manual; }
    void advanceClock(int ms);
    // Tests: a timer tick that measured elapsedMs of wall-clock time (long
    // gaps count as a pause, see SessionClock.h).
    void advanceWallClock(qint64 elapsedMs);
    bool timerActive() const { return m_timer.isActive(); }
    void setMinimumSaveSeconds(int s) { m_minSaveSec = s; }

    QString state() const;
    State stateValue() const { return m_state; }
    bool active() const { return m_state == State::Running || m_state == State::Paused; }
    bool running() const { return m_state == State::Running; }
    bool paused() const { return m_state == State::Paused; }
    bool hasRecording() const { return m_recordedSec > 0 || m_activeMs > 0; }
    bool inProgress() const { return running() || (paused() && hasRecording()); }
    bool abandonable() const { return paused() && !hasRecording(); }
    void abandon() { if (abandonable()) finish(false); }   // empty paused: given up
    QString mode() const;
    Mode modeValue() const { return m_mode; }
    void setMode(const QString &mode);
    static QString modeName(Mode m);
    // "ride" / "erg"; the old "gears" and "free" -> Ride. nullopt if unknown.
    static std::optional<Mode> parseMode(const QString &text);
    // ERG is on exactly in mode "erg" (the Ride | ERG selector is the only
    // switch); there is no separate setter, so mode and ERG cannot disagree.
    bool ergEnabled() const { return m_mode == Mode::Erg; }
    // ERG outside a ride: selecting ERG on the Free Ride page (VirtualGears::
    // activePage() == "freeride") puts the trainer in ERG at the target right
    // away before Start (trainer connected, no other session
    // busy); Ride / leaving the page releases it. Running: the ride's
    // resistance; paused: released, ERG comes with resume().
    // Likewise Ride with virtual gears on the open page: the trainer gets the
    // virtual gear resistance (same paths as the ride, following the live
    // cadence), so shifting changes the watts before Start too.
    bool ergHolding() const { return m_holding && m_mode == Mode::Erg; }
    bool gearHolding() const { return m_holding && m_mode == Mode::Ride; }
    bool holdWanted() const;
    int defaultErgTarget() const;   // 60 % FTP, rounded to 5 W

    int elapsedSeconds() const { return int(m_activeMs / 1000); }
    double distanceMeters() const { return m_distance; }
    double speedKmh() const;
    double speedMps() const;
    double avgSpeedKmh() const;
    int watts() const;
    int avgWatts() const;
    int maxWatts() const { return m_maxWatts; }
    double kilojoules() const { return m_energyJ / 1000.0; }
    int cadence() const;
    int heartRate() const;
    bool coasting() const;
    // Zwift protocol: pedaling = power >= kPedalPowerWatts OR cadence >=
    // kCoastCadenceRpm (the cadence lags at the start: 0 rpm at 180 W; light
    // pedaling reads 0 W on the Victory); coasting = not pedaling while the
    // flywheel turns. Light pedaling (cadence, power below kPedalPowerWatts):
    // the speed follows the trainer's flywheel for the gear.
    bool pedaling() const;

    bool virtualGears() const;
    bool simulationFallback() const;
    RideResistance::Path path() const;
    const RideResistance &resistance() const { return m_res; }   // diagnostics
    QString resistancePath() const { return QString::fromLatin1(RideResistance::pathName(path())); }
    double simulationGradePercent() const { return m_res.lastGrade(); }
    bool simulationActive() const { return running() && m_res.simulating(); }
    // Grade sent in the gear-simulation path for the current cadence/speed.
    double gearSimulationGrade() const;
    int targetWatts() const { return m_targetWatts; }
    Q_INVOKABLE void setTargetWatts(int watts);
    int gearTargetWatts() const;
    int requestedWatts() const { return m_res.lastWatts(); }
    double gradePercent() const { return m_grade; }
    Q_INVOKABLE void setGrade(double percent);
    int minTargetWatts() const { return kMinErgWatts; }
    int maxTargetWatts() const { return kMaxWatts; }
    double minGrade() const { return kMinGrade; }
    double maxGrade() const { return kMaxGrade; }

    VirtualGears *gears() const { return m_gears; }
    const Gearing &gearing() const;

    QVariantList powerSeries() const;
    QVariantList speedSeries() const;
    QVariantList heartRateSeries() const;
    int seriesStep() const;
    const QVector<Sample> &samples() const { return m_samples; }

    bool trainerConnected() const;
    qint64 lastRideId() const { return m_lastRideId; }
    int plannedDurationSec() const { return m_plannedDurationSec; }
    Q_INVOKABLE void setPlannedDurationSec(int seconds);

    // Road model for the current rider (profile weight + bike) and grade.
    road::Model roadModel(double gradePercent) const;

    Q_INVOKABLE bool start();               // the only way a ride starts
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE qint64 stop() { return finish(true); }
    Q_INVOKABLE qint64 finish(bool save = true);   // ride id or -1
    Q_INVOKABLE void discard() { finish(false); }
    Q_INVOKABLE void reset();               // finished -> idle (clears the last ride's values)

    Q_INVOKABLE void adjustTargetWatts(int delta);
    Q_INVOKABLE void adjustGrade(double delta);

public slots:
    // Controller buttons (Zwift Click via VirtualGears::controllerShift(), keys):
    // +1 = plus (harder), -1 = minus. Ride: shift the virtual gear (never the
    // difficulty); ERG: target ±kControllerErgStep W.
    void controllerShift(int direction);
    void controllerShiftUp() { controllerShift(1); }
    void controllerShiftDown() { controllerShift(-1); }

signals:
    void stateChanged();
    void modeChanged();
    void metricsChanged();
    void tick();
    void targetChanged();
    void gradeChanged();
    void seriesChanged();
    void devicesChanged();
    void started();
    void finished();
    void rideSaved(qint64 id);
    void lastRideIdChanged();
    void startRefused(const QString &reason);
    void shifted(int direction);    // an ERG step from a controller (gear shifts: VirtualGears::shifted)
    void plannedDurationChanged();

private:
    void setState(State s);
    void onTimer();
    void process(int ms);
    void updateModel(double dt);
    void recordUpTo(int second);
    void applyTarget(bool force = false);
    int desiredGearWatts() const;
    RideResistance::GearDrive gearDrive(double cadenceRpm) const;
    double controlCadence() const;   // cadence the resistance follows (0..: < coast = coasting)
    bool sendResistance(bool force);  // the current path's command (ride and page hold)
    void updateTimer();
    void abandonOthers();
    void syncRideProfile();
    void resetLastRideId();
    void resistanceSetupChanged();
    void clearRecording();
    qint64 saveRide();
    void releaseTrainer();
    void updateHold(bool force = false);
    bool holdBlocked() const;
    QVariantList downsample(const QVector<double> &data, bool ignoreZero, int decimals) const;

    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    VirtualGears *m_gears;
    RideResistance m_res;
    QPointer<TrainingSession> m_training;
    QHash<QString, std::function<bool()>> m_startBlockers;
    QHash<QString, std::function<void()>> m_abandonCancellers;

    Mode m_mode = Mode::Ride;
    State m_state = State::Idle;
    int m_targetWatts = kDefaultErgWatts;
    bool m_holding = false;       // ERG target / virtual gear held outside a ride (updateHold())
    double m_grade = 0.0;

    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_manualClock = false;
    int m_minSaveSec = 30;
    qint64 m_activeMs = 0;
    double m_cadSmooth = 0;
    double m_speed = 0;           // m/s (model, while running/paused)
    double m_distance = 0;

    QDateTime m_startedAt;
    Profile m_rideProfile;
    QVector<Sample> m_samples;
    QVector<double> m_powerData, m_speedData, m_hrData;
    int m_recordedSec = 0;
    double m_energyJ = 0;
    double m_powerSum = 0;
    int m_maxWatts = 0;
    int m_ergSeconds = 0;
    qint64 m_lastRideId = -1;
    int m_plannedDurationSec = 0;
};

} // namespace watt
