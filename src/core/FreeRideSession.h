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
//              gear-adjusted grade, trainer simulates and reports speed) or
//              RideResistance::GearErg ("path b": ERG target P, every other
//              trainer). Coasting releases the trainer and the speed rolls out
//              (road::coastDeceleration); descents may roll faster than the
//              pedals turn.
//            * trainer without gradient simulation and virtual gears off:
//              virtual gears via ERG anyway (simulationFallback), otherwise a
//              climb could not get harder.
//   "erg"  - hold targetWatts (ERG) as long as you like; speed from power on
//            a flat road.
//   Stored "gears" / "free" (older versions) are migrated to "ride".
// States ("state"): "idle", "ready" (armed by startWhenPedaling(), starts on
// the first pedal stroke), "running", "paused", "finished".
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
    Q_PROPERTY(bool waiting READ waiting NOTIFY stateChanged)      // armed, waits for pedaling
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool ergEnabled READ ergEnabled NOTIFY modeChanged)   // read-only: mode == "erg"
    // The trainer holds the ERG target outside a ride (ERG selected on the open
    // Free Ride page, idle or armed; see holdWanted()).
    Q_PROPERTY(bool ergHolding READ ergHolding NOTIFY targetChanged)

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
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY rideSaved)

public:
    enum class Mode { Ride, Erg };
    enum class State { Idle, Ready, Running, Paused, Finished };

    static constexpr int kStartCadenceRpm = 20;     // same thresholds as TrainingSession
    static constexpr int kStartPowerWatts = 30;
    static constexpr int kCoastCadenceRpm = 15;     // below: not pedaling (coasting)
    static constexpr int kMinGearWatts = 20;        // lowest gears-mode target while pedaling
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
    // cancels it when it is only armed).
    void setTrainingSession(TrainingSession *training);
    // Further guards; while one returns true, start() is refused.
    void setStartBlocker(const QString &key, std::function<bool()> blocker);
    bool startBlocked() const;

    // Tests
    void setManualClock(bool manual) { m_manualClock = manual; }
    void advanceClock(int ms);
    void setMinimumSaveSeconds(int s) { m_minSaveSec = s; }

    QString state() const;
    State stateValue() const { return m_state; }
    bool active() const { return m_state == State::Running || m_state == State::Paused; }
    bool running() const { return m_state == State::Running; }
    bool paused() const { return m_state == State::Paused; }
    bool waiting() const { return m_state == State::Ready; }
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
    // away while the ride is idle or armed (trainer connected, no other session
    // busy); Ride / leaving the page releases it. Running: the ride's
    // resistance; paused: released, ERG comes with resume().
    bool ergHolding() const { return m_holding; }
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

    bool virtualGears() const;
    bool simulationFallback() const;
    RideResistance::Path path() const;
    QString resistancePath() const { return QString::fromLatin1(RideResistance::pathName(path())); }
    double simulationGradePercent() const { return m_res.lastGrade(); }
    bool simulationActive() const { return running() && m_res.simulating(); }
    // Grade sent in the gear-simulation path for the current cadence/speed.
    double gearSimulationGrade() const;
    int targetWatts() const { return m_targetWatts; }
    void setTargetWatts(int watts);
    int gearTargetWatts() const;
    int requestedWatts() const { return m_res.lastWatts(); }
    double gradePercent() const { return m_grade; }
    void setGrade(double percent);
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

    // Road model for the current rider (profile weight + bike) and grade.
    road::Model roadModel(double gradePercent) const;

    Q_INVOKABLE bool start();               // start now
    Q_INVOKABLE void startWhenPedaling();   // arm: starts on the first pedal stroke
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE qint64 stop() { return finish(true); }
    Q_INVOKABLE qint64 finish(bool save = true);   // ride id or -1
    Q_INVOKABLE void discard() { finish(false); }
    Q_INVOKABLE void reset();               // finished/ready -> idle (clears the last ride's values)

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
    void startRefused(const QString &reason);
    void shifted(int direction);    // an ERG step from a controller (gear shifts: VirtualGears::shifted)

private:
    void setState(State s);
    void onTimer();
    void process(int ms);
    void updateModel(double dt);
    void recordUpTo(int second);
    void checkPedaling();
    void applyTarget(bool force = false);
    int desiredGearWatts() const;
    int computeGearTarget(double cadenceRpm) const;
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

    Mode m_mode = Mode::Ride;
    State m_state = State::Idle;
    int m_targetWatts = kDefaultErgWatts;
    bool m_holding = false;       // ERG target held outside a ride (updateHold())
    double m_grade = 0.0;

    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_manualClock = false;
    int m_minSaveSec = 30;
    qint64 m_activeMs = 0;
    double m_cadSmooth = 0;
    double m_speed = 0;           // m/s (model, while ready/running/paused)
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
};

} // namespace watt
