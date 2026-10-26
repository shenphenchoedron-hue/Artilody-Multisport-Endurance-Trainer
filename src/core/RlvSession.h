#pragma once

#include "Gearing.h"
#include "Profile.h"
#include "Ride.h"
#include "RideResistance.h"
#include "RlvRoute.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>

namespace watt {

class Database;
class FreeRideSession;
class ProfileManager;
class RlvLibrary;
class SensorHub;
class TrainingSession;
class VirtualGears;

// Real Life Video ride. Exposed to QML as "Rlv" (Artilody.Backend).
//
// The rider moves along the GPS track of the selected route; the video
// follows the rider's position.
//
// Resistance ("mode", setting "rlv/mode"):
//   "sim" (default) - the route is a simulated road; climbs get harder like
//       outdoors (classic RLV gradient simulation, like Tacx, Rouvy, Kinomap):
//       * virtual gears off (VirtualGears::enabled): the rider shifts the
//         real bike and the trainer gets the effective grade with FTMS Set
//         Indoor Bike Simulation Parameters (op 0x11, SensorHub::
//         requestSimulation(): wind 0, grade in 0.01 % clamped kMinSimGrade..
//         kMaxSimGrade, Crr 0.0040, Cw 0.51 kg/m - the values Zwift sends).
//         Rate limits: RideResistance (first value at once, then on a change
//         >= kSimMinGradeChange at most every kSimMinIntervalMs, refreshed
//         every kSimRefreshMs). Speed: from the measured power on the
//         effective grade with inertia (road::integrateSpeed:
//         m·dv/dt = P·η/v − F(v)), settling at road::speedForPower().
//       * virtual gears on (Zwift Click / Devices page): shifting works like
//         a mechanical groupset - the virtual gear (shared VirtualGears, set
//         up on the Devices page) and the effective grade decide the
//         resistance: v = cadence/60 · ratio · wheel circumference and the
//         road power P = road::powerForSpeed(v, grade). Delivered through
//         RideResistance::GearSimulation ("path a": the trainer simulates and
//         reports speed; it gets the gear-adjusted grade
//         road::gearSimulationGrade() while the bike stays in the reference
//         gear) or RideResistance::GearErg ("path b": ERG target P, resent at
//         most every kTargetIntervalMs, clamped kMinGearWatts..kMaxWatts,
//         released while coasting). Speed/distance/video follow the virtual
//         speed; while coasting, or when the road is faster than the pedals
//         (descents), the road physics (road::coastDeceleration). A trainer
//         without cadence: speed from power (with inertia).
//       * a trainer that cannot simulate (SensorHub::trainerSimulationSupport()
//         == 0) with virtual gears off: virtual gears via ERG anyway
//         (simulationFallback) - otherwise climbs could not get harder.
//   "erg" - fixed watts: the trainer holds targetWatts (FTMS Set Target
//       Power, kMinErgWatts..kMaxWatts, setting "rlv/ergWatts"); speed and
//       video come from the power on the effective grade (with inertia), so
//       climbs make the video slower, not the pedals heavier. Default when
//       switching to ERG: the rider's current 3 s power while pedaling, else
//       the last target, else 60 % FTP (rounded to 5 W).
//   Stored "power" and "gears" (older versions) are migrated to "sim".
// effective grade = route grade at the rider's distance × difficultyPercent/100
// (descents are scaled too) in "sim"; the real route grade in
// "erg" (the difficulty is a resistance setting). difficultyPercent 0..100, default 100, setting
// "rlv/difficulty".
//
// Video sync contract (QML: MediaPlayer + VideoOutput):
//   * source: videoUrl.
//   * videoPosition (ms) is the target position for the rider's distance,
//     updated every tick. Do NOT bind player.position to it. Seek only when
//     seekRequested(ms) fires (route selected, seekToDistance(), start,
//     or a drift > kSeekThresholdSec).
//   * playbackRate: bind player.playbackRate to it (riderSpeed / filmed
//     speed at that point, clamped kMinRate..kMaxRate, quantised to 0.05,
//     with up to ±kMaxRateCorrection drift correction).
//   * videoShouldPlay: play() when true, pause() when false (not running,
//     slower than kMinPlaySpeedKmh, or video ahead while the rider is slower
//     than kMinRate allows).
//   * call reportVideoPosition(player.position) regularly (e.g. on
//     positionChanged or every 500 ms) while the player plays; without
//     reports there is no drift correction.
//
// States ("state"): "idle", "ready" (armed, starts on the first pedal
// stroke), "running", "paused", "finished" (end of the route reached or
// stopped). The route end finishes and saves the ride automatically.
//
// Only one session (workout, rowing, free ride, RLV) runs at a time:
// setTrainingSession() installs a start guard on the workout session,
// setStartBlocker() adds further guards (main.cpp wires free ride/rowing).
// Rides are stored as sport "cycling", name "RLV: <route name>", with the
// virtual speed per second and the ridden distance.
class RlvSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(bool waiting READ waiting NOTIFY stateChanged)
    Q_PROPERTY(bool finishedRide READ finishedRide NOTIFY stateChanged)
    Q_PROPERTY(bool completed READ completed NOTIFY stateChanged)     // route end reached
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)

    // route
    Q_PROPERTY(bool hasRoute READ hasRoute NOTIFY routeChanged)
    Q_PROPERTY(int routeIndex READ routeIndex NOTIFY routeChanged)          // row in Videos, -1
    Q_PROPERTY(QString routePath READ routePath NOTIFY routeChanged)
    Q_PROPERTY(QString routeName READ routeName NOTIFY routeChanged)
    Q_PROPERTY(QString routeTitle READ routeTitle NOTIFY routeChanged)
    Q_PROPERTY(QString routeCountry READ routeCountry NOTIFY routeChanged)
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY routeChanged)
    Q_PROPERTY(bool videoAvailable READ videoAvailable NOTIFY routeChanged)
    Q_PROPERTY(double totalDistanceM READ totalDistanceM NOTIFY routeChanged)
    Q_PROPERTY(double totalAscentM READ totalAscentM NOTIFY routeChanged)
    Q_PROPERTY(int routeDurationSec READ routeDurationSec NOTIFY routeChanged)
    Q_PROPERTY(double minElevationM READ minElevationM NOTIFY routeChanged)
    Q_PROPERTY(double maxElevationM READ maxElevationM NOTIFY routeChanged)
    Q_PROPERTY(QVariantList profile READ profile NOTIFY routeChanged)       // [{d, ele}] kProfileSamples
    Q_PROPERTY(QVariantList informations READ informations NOTIFY routeChanged)   // [{distance, text}]
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    // position
    Q_PROPERTY(double startDistanceM READ startDistanceM NOTIFY positionChanged)
    Q_PROPERTY(double distanceMeters READ distanceMeters NOTIFY positionChanged)  // position on the route
    Q_PROPERTY(double riddenMeters READ riddenMeters NOTIFY positionChanged)      // distance - start
    Q_PROPERTY(double remainingMeters READ remainingMeters NOTIFY positionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY positionChanged)              // 0..1 of the route
    Q_PROPERTY(double gradePercent READ gradePercent NOTIFY positionChanged)
    Q_PROPERTY(double effectiveGradePercent READ effectiveGradePercent NOTIFY positionChanged)
    Q_PROPERTY(double elevationM READ elevationM NOTIFY positionChanged)
    Q_PROPERTY(double ascentDoneM READ ascentDoneM NOTIFY positionChanged)
    Q_PROPERTY(QString currentInformation READ currentInformation NOTIFY informationChanged)
    Q_PROPERTY(int currentInformationIndex READ currentInformationIndex NOTIFY informationChanged)

    // video
    Q_PROPERTY(qint64 videoPosition READ videoPosition NOTIFY positionChanged)    // ms, target
    Q_PROPERTY(double playbackRate READ playbackRate NOTIFY videoChanged)
    Q_PROPERTY(bool videoShouldPlay READ videoShouldPlay NOTIFY videoChanged)
    Q_PROPERTY(double videoDriftSec READ videoDriftSec NOTIFY videoChanged)       // reported - target

    // live / session values
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY tick)
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY metricsChanged)
    Q_PROPERTY(double avgSpeedKmh READ avgSpeedKmh NOTIFY tick)
    Q_PROPERTY(int watts READ watts NOTIFY metricsChanged)                       // raw
    Q_PROPERTY(int displayWatts READ displayWatts NOTIFY metricsChanged)         // Sensors.displayPower
    Q_PROPERTY(int avgWatts READ avgWatts NOTIFY tick)
    Q_PROPERTY(int maxWatts READ maxWatts NOTIFY tick)
    Q_PROPERTY(double kilojoules READ kilojoules NOTIFY tick)
    Q_PROPERTY(int cadence READ cadence NOTIFY metricsChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY metricsChanged)
    Q_PROPERTY(bool coasting READ coasting NOTIFY metricsChanged)

    // resistance
    Q_PROPERTY(int difficultyPercent READ difficultyPercent WRITE setDifficulty NOTIFY difficultyChanged)
    Q_PROPERTY(int targetWatts READ targetWatts WRITE setTargetWatts NOTIFY targetChanged)   // ERG mode target
    Q_PROPERTY(int minTargetWatts READ minTargetWatts CONSTANT)
    Q_PROPERTY(int maxTargetWatts READ maxTargetWatts CONSTANT)
    Q_PROPERTY(int sentTargetWatts READ sentTargetWatts NOTIFY targetChanged)    // last ERG target sent (0 = none)
    Q_PROPERTY(int gearTargetWatts READ gearTargetWatts NOTIFY targetChanged)    // road power of the virtual gear
    // grade last sent to the trainer (FTMS 0x11), and whether it is being sent
    Q_PROPERTY(double simulationGradePercent READ simulationGradePercent NOTIFY targetChanged)
    Q_PROPERTY(bool simulationActive READ simulationActive NOTIFY targetChanged)
    // sim mode on a trainer without gradient simulation: virtual gears instead
    Q_PROPERTY(bool simulationFallback READ simulationFallback NOTIFY modeChanged)
    // the virtual gear sets the resistance ("sim" with virtual gears on, or the fallback)
    Q_PROPERTY(bool virtualGears READ virtualGears NOTIFY modeChanged)
    Q_PROPERTY(QString resistancePath READ resistancePath NOTIFY modeChanged)   // RideResistance::pathName()

    Q_PROPERTY(bool trainerConnected READ trainerConnected NOTIFY devicesChanged)
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY rideSaved)

public:
    enum class Mode { Sim, Erg };
    enum class State { Idle, Ready, Running, Paused, Finished };

    static constexpr int kStartCadenceRpm = 20;
    static constexpr int kStartPowerWatts = 30;
    static constexpr int kCoastCadenceRpm = 15;
    static constexpr int kMinGearWatts = 20;
    static constexpr int kMaxWatts = 1500;
    static constexpr int kTargetIntervalMs = RideResistance::kGearIntervalMs;
    static constexpr int kMinTargetChangeWatts = RideResistance::kMinWattsChange;
    static constexpr double kCadenceSmoothingSec = 1.5;
    static constexpr int kDefaultDifficulty = 100;
    static constexpr int kDifficultyStep = 10;
    // ERG
    static constexpr int kMinErgWatts = 25;
    static constexpr int kControllerErgStep = 10;   // Zwift Click / PgUp / PgDn
    static constexpr double kDefaultErgFtpShare = 0.6;
    // gradient simulation
    static constexpr double kMinSimGrade = RideResistance::kMinGrade;
    static constexpr double kMaxSimGrade = RideResistance::kMaxGrade;
    static constexpr double kSimMinGradeChange = RideResistance::kMinGradeChange;   // %
    static constexpr int kSimMinIntervalMs = RideResistance::kMinGradeIntervalMs;
    static constexpr int kSimRefreshMs = RideResistance::kGradeRefreshMs;
    // speed from power with inertia
    static constexpr double kRotatingMassKg = 1.5;      // wheels, equivalent mass
    static constexpr double kMinDriveSpeedMps = 1.0;    // P/v drive force below this speed
    static constexpr double kIntegrationStepSec = 0.05;
    static constexpr int kProfileSamples = 500;
    // video sync
    static constexpr double kMinRate = 0.25;
    static constexpr double kMaxRate = 2.5;
    static constexpr double kRateQuantum = 0.05;
    static constexpr double kMinPlaySpeedKmh = 2.0;
    static constexpr double kSeekThresholdSec = 2.0;
    static constexpr double kRateCorrectionPerSec = 0.1;   // rate × (1 - drift·this)
    static constexpr double kMaxRateCorrection = 0.1;      // ±10 %
    static constexpr double kHoldAheadSec = 0.5;           // video ahead at min rate: hold
    static constexpr int kSeekCooldownMs = 1500;
    static constexpr int kReportMaxAgeMs = 3000;

    RlvSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears,
               RlvLibrary *library = nullptr, QObject *parent = nullptr);
    ~RlvSession() override;

    // An armed free ride is cancelled when the video ride starts.
    void setFreeRideSession(FreeRideSession *freeRide) { m_freeRide = freeRide; }
    void setTrainingSession(TrainingSession *training);
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
    bool finishedRide() const { return m_state == State::Finished; }
    bool completed() const { return m_completed; }
    QString mode() const { return modeName(m_mode); }
    Mode modeValue() const { return m_mode; }
    void setMode(const QString &mode);
    static QString modeName(Mode m);
    // "sim" / "erg"; the old "power" and "gears" -> Sim. nullopt if unknown.
    static std::optional<Mode> parseMode(const QString &text);

    const RlvRoute &route() const { return m_route; }
    bool hasRoute() const { return m_route.isValid(); }
    int routeIndex() const;
    QString routePath() const { return m_route.sourcePath(); }
    QString routeName() const { return m_route.name(); }
    QString routeTitle() const { return m_route.title(); }
    QString routeCountry() const { return m_route.country(); }
    QUrl videoUrl() const;
    bool videoAvailable() const { return m_route.hasVideo(); }
    double totalDistanceM() const { return m_route.totalDistance(); }
    double totalAscentM() const { return m_route.ascent(); }
    int routeDurationSec() const;
    double minElevationM() const { return m_route.minElevation(); }
    double maxElevationM() const { return m_route.maxElevation(); }
    QVariantList profile() const { return m_profile; }
    QVariantList informations() const;
    QString lastError() const { return m_lastError; }

    double startDistanceM() const { return m_startDistance; }
    double distanceMeters() const { return m_distance; }
    double riddenMeters() const { return std::max(0.0, m_distance - m_startDistance); }
    double remainingMeters() const { return std::max(0.0, totalDistanceM() - m_distance); }
    double progress() const;
    double gradePercent() const;
    double effectiveGradePercent() const;
    double elevationM() const;
    double ascentDoneM() const;
    QString currentInformation() const;
    int currentInformationIndex() const { return m_infoIndex; }

    qint64 videoPosition() const;
    double playbackRate() const { return m_rate; }
    bool videoShouldPlay() const { return m_shouldPlay; }
    double videoDriftSec() const { return m_drift; }

    int elapsedSeconds() const { return int(m_activeMs / 1000); }
    double speedMps() const;
    double speedKmh() const { return speedMps() * 3.6; }
    double avgSpeedKmh() const;
    int watts() const;
    int displayWatts() const;
    int avgWatts() const;
    int maxWatts() const { return m_maxWatts; }
    double kilojoules() const { return m_energyJ / 1000.0; }
    int cadence() const;
    int heartRate() const;
    bool coasting() const;

    int difficultyPercent() const { return m_difficulty; }
    void setDifficulty(int percent);
    int targetWatts() const { return m_ergTarget; }
    void setTargetWatts(int watts);
    int minTargetWatts() const { return kMinErgWatts; }
    int maxTargetWatts() const { return kMaxWatts; }
    int sentTargetWatts() const { return m_res.lastWatts(); }
    int gearTargetWatts() const;
    double simulationGradePercent() const { return m_res.lastGrade(); }
    bool simulationActive() const { return running() && m_res.simulating(); }
    bool simulationFallback() const;
    bool virtualGears() const;
    RideResistance::Path path() const;
    QString resistancePath() const { return QString::fromLatin1(RideResistance::pathName(path())); }
    // Grade the sim mode sends with real gears (rounded to 0.01 %).
    double simulationGradeToSend() const;
    // Grade sent with virtual gears on a simulating trainer (path a).
    double gearSimulationGrade() const;

    VirtualGears *gears() const { return m_gears; }
    const Gearing &gearing() const;

    bool trainerConnected() const;
    qint64 lastRideId() const { return m_lastRideId; }
    const QVector<Sample> &samples() const { return m_samples; }

    road::Model roadModel(double gradePercent) const;
    // Speed (m/s) at which the rider's power holds on the given grade
    // (road::speedForPower for the current rider and bike).
    Q_INVOKABLE double solveSpeedForPower(double watts, double gradePercent) const;

    Q_INVOKABLE bool select(int row);                 // row in Videos
    Q_INVOKABLE bool selectPath(const QString &path); // folder, .xml or .gpx (file:// URLs ok)
    Q_INVOKABLE void clearRoute();
    Q_INVOKABLE bool start();
    Q_INVOKABLE void startWhenPedaling();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE qint64 stop() { return finish(true); }
    Q_INVOKABLE qint64 finish(bool save = true);
    Q_INVOKABLE void discard() { finish(false); }
    Q_INVOKABLE void reset();                          // finished -> idle at the start distance
    Q_INVOKABLE bool seekToDistance(double meters);    // start point; not while active
    Q_INVOKABLE void reportVideoPosition(qint64 ms);
    Q_INVOKABLE void adjustDifficulty(int delta) { setDifficulty(m_difficulty + delta); }
    Q_INVOKABLE void adjustTargetWatts(int delta) { setTargetWatts(m_ergTarget + delta); }
    Q_INVOKABLE QString formatDistance(double meters) const;   // "1.25 km" / "850 m"

public slots:
    // Zwift Click (via VirtualGears::controllerShift()) / keyboard: "sim"
    // shifts the virtual gear (never the difficulty); ERG ±kControllerErgStep W.
    void controllerShift(int direction);

signals:
    void stateChanged();
    void modeChanged();
    void routeChanged();
    void positionChanged();
    void informationChanged();
    void videoChanged();
    void seekRequested(qint64 positionMs);
    void metricsChanged();
    void tick();
    void targetChanged();
    void difficultyChanged();
    void devicesChanged();
    void lastErrorChanged();
    void started();
    void finished();
    void rideSaved(qint64 id);
    void startRefused(const QString &reason);
    void shifted(int direction);   // ERG step from a controller (gear shifts: VirtualGears::shifted)

private:
    void setState(State s);
    void setError(const QString &e);
    void onTimer();
    void process(int ms);
    void updateModel(double dt);
    void integratePower(double watts, double dt);
    void resistanceSetupChanged();
    int defaultErgTarget() const;
    void updateVideo(int ms);
    void updateInformation();
    void recordUpTo(int second);
    void checkPedaling();
    void applyTarget(bool force = false);
    int desiredGearWatts() const;
    int computeGearTarget(double cadenceRpm) const;
    void clearRecording();
    qint64 saveRide();
    void releaseTrainer();
    void requestSeek();
    bool canSelect() const;

    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    VirtualGears *m_gears;
    RideResistance m_res;
    QPointer<RlvLibrary> m_library;
    QPointer<FreeRideSession> m_freeRide;
    QPointer<TrainingSession> m_training;
    QHash<QString, std::function<bool()>> m_startBlockers;

    RlvRoute m_route;
    QVariantList m_profile;
    Mode m_mode = Mode::Sim;
    int m_ergTarget = 150;
    State m_state = State::Idle;
    int m_difficulty = kDefaultDifficulty;
    bool m_completed = false;
    QString m_lastError;

    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_manualClock = false;
    int m_minSaveSec = 30;
    qint64 m_activeMs = 0;
    double m_cadSmooth = 0;
    double m_speed = 0;
    double m_startDistance = 0;
    double m_distance = 0;
    int m_infoIndex = -1;

    // video sync
    double m_rate = 1.0;
    bool m_shouldPlay = false;
    bool m_holding = false;
    double m_drift = 0;
    qint64 m_reportedMs = -1;
    qint64 m_nowMs = 0;          // session clock (advanced by process(), also with the manual clock)
    qint64 m_reportAtMs = 0;
    qint64 m_seekAtMs = -100000;

    QDateTime m_startedAt;
    Profile m_rideProfile;
    QVector<Sample> m_samples;
    int m_recordedSec = 0;
    double m_energyJ = 0;
    double m_powerSum = 0;
    int m_maxWatts = 0;
    qint64 m_lastRideId = -1;
};

} // namespace watt
