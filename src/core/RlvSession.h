#pragma once

#include "Gearing.h"
#include "Profile.h"
#include "Ride.h"
#include "RideResistance.h"
#include "RlvRoute.h"
#include "RouteProgress.h"
#include "RouteSegments.h"
#include <algorithm>
#include "Sprint.h"
#include "WorkoutEngine.h"

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
//         kMaxSimGrade, Crr and Cw = ρ·CdA of the rider from
//         road::trainerModelFor() - the app's road model, see Gearing.h).
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
//         RideResistance::GearSimulation ("path a": the trainer simulates; it
//         gets the gear-adjusted grade RideResistance::gearGrade() for the
//         cadence while the bike stays in the reference gear) or
//         RideResistance::GearErg ("path b": ERG target P =
//         RideResistance::gearWatts(), resent at most every
//         kTargetIntervalMs, released while coasting) - the same functions
//         as the Free Ride. With the page open (before Start) the trainer
//         already gets this resistance (gearHolding()). Speed/distance/video follow the virtual
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
// States ("state"): "idle", "running", "paused", "finished" (end of the
// route reached or stopped). A ride only starts with start() (Start button /
// Space): pedaling never starts or resumes it. The route end finishes and
// saves the ride automatically.
//
// Only one session (workout, rowing, free ride, RLV) runs at a time:
// setTrainingSession() installs a start guard on the workout session,
// setStartBlocker() adds further guards (main.cpp wires free ride/rowing).
// Rides are stored as sport "cycling", name "RLV: <route name>", with the
// virtual speed per second and the ridden distance.
//
// Workout during the video ride (attachWorkout()): a cycling workout of the
// library runs on the ride's clock (WorkoutEngine, advanced only while the
// ride is running - paused with it). Targets are % of the rider's cycling FTP
// × (100 + intensityPercent) / 100, like Training's intensity offset.
//   * workoutErg on (default when attaching): the trainer holds the
//     workout's target in ERG (path Erg, target workoutTargetWatts); speed,
//     distance and video come from the power on the real route grade (the
//     same physics as the "erg" mode) - climbs slow the video, they do not
//     change the watts. "mode" (sim/erg) is kept for after the workout.
//   * workoutErg off: the selected mode decides the resistance as without a
//     workout (Simulation / virtual gears / RLV ERG); the workout targets are
//     guidance only (shown, recorded, compliance computed).
//   * The workout ends before the route: workoutFinished, workoutCompleted();
//     the workout ERG is released and the ride continues in the selected mode.
//   * The route ends first: the ride finishes and is saved as usual, the
//     workout stays unfinished (workoutFinished false).
//   * Zwift Click / PgUp / PgDn while the workout ERG holds: intensity
//     ±kControllerIntensityStep % (the Watt Training page's +/- step);
//     otherwise as without a workout (gears in sim, ±10 W in RLV ERG).
// With a workout the ride is stored as "RLV: <route> + <workout>" with
// workout_id, blocks = intervals and the workout target per second.
//
// Kind (constructor): Kind::Video is the Video Ride above ("Rlv", page
// "video", settings "rlv/…", rides "RLV: …"). Kind::Gpx is the GPX Ride
// ("Gpx", page "gpx", settings "gpx/…", rides "GPX: …"): the same session,
// physics and trainer control on a plain .gpx track without a video (the
// page shows the route in 3D instead, Route3DItem). Only the names, the
// settings keys and the route files accepted differ - never the physics.
//
// Sprints and KOMs (GPX Ride only): the segments placed in the GPX
// Workshop and saved in the GPX file (RlvRoute::segments(); a route without
// them has none). Each is timed on the ride's clock from its start line to
// its finish line, the crossings interpolated within a tick (paused time
// never counts; a pause during it voids it; a discarded ride's times are
// deleted). Every finished one is saved (Database::saveSegmentRecord: kind
// "sprint" / "kom", profile, the route's fingerprint sprint::routeKey() and
// where it starts, linked to the ride when it is saved) and compared with
// the profile's best on it. The sprint* / kom* properties describe the one
// of each kind in focus: the one being ridden, else the one just finished
// (for kResultShowMs, unless the next is close), else the next ahead.
class RlvSession : public QObject, public RouteProgressSource
{
    Q_OBJECT
    Q_PROPERTY(QString kind READ kindName CONSTANT)            // "video" | "gpx"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    // Genuinely in progress: riding, or paused with something recorded. Only
    // such a session blocks starting another one; one paused before anything
    // was recorded is given up instead (abandon()).
    Q_PROPERTY(bool inProgress READ inProgress NOTIFY stateChanged)
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
    // the route's sprints and KOMs for the profile: [{kind "sprint"/"kom", start, finish, name}]
    Q_PROPERTY(QVariantList segmentMarkers READ segmentMarkers NOTIFY routeChanged)
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
    // GPX Ride loop: ride the route lap after lap until Finish (a round trip
    // round and round, any other route there and back); every lap ends on a
    // finish line. Set before the start (kept in the settings).
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY loopChanged)
    Q_PROPERTY(bool canLoop READ canLoop NOTIFY routeChanged)
    Q_PROPERTY(bool roundTrip READ roundTrip NOTIFY routeChanged)
    Q_PROPERTY(bool looping READ looping NOTIFY stateChanged)            // this ride loops
    Q_PROPERTY(int lap READ lap NOTIFY lapChanged)                       // 1, 2, ... (the one being ridden)
    Q_PROPERTY(bool returning READ returning NOTIFY lapChanged)          // there and back: on the way back
    Q_PROPERTY(double lastLapSec READ lastLapSec NOTIFY lapChanged)      // the last finished lap, 0 = none
    Q_PROPERTY(double lapElapsedSec READ lapElapsedSec NOTIFY tick)
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
    Q_PROPERTY(bool ergHolding READ ergHolding NOTIFY targetChanged)   // ERG held outside a ride
    // virtual gear resistance held outside a ride (Simulation with virtual
    // gears on the open Video Ride page before Start)
    Q_PROPERTY(bool gearHolding READ gearHolding NOTIFY targetChanged)
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
    // -1 from start() / reset() / discard until the ride is saved
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY lastRideIdChanged)

    // workout during the ride (see the class comment)
    Q_PROPERTY(bool hasWorkout READ hasWorkout NOTIFY workoutChanged)
    Q_PROPERTY(qint64 workoutId READ workoutId NOTIFY workoutChanged)           // -1 = none
    Q_PROPERTY(QString workoutName READ workoutName NOTIFY workoutChanged)
    // [{startSec, durationSec, watts, endWatts, percent, ramp, name}] at the rider's cycling
    // FTP, without the intensity offset (the format of Rowing.intervals / WorkoutChart)
    Q_PROPERTY(QVariantList workoutIntervals READ workoutIntervals NOTIFY workoutChanged)
    Q_PROPERTY(int workoutTotalSec READ workoutTotalSec NOTIFY workoutChanged)
    Q_PROPERTY(int workoutElapsedSec READ workoutElapsedSec NOTIFY workoutTick)
    Q_PROPERTY(int workoutRemainingSec READ workoutRemainingSec NOTIFY workoutTick)
    Q_PROPERTY(double workoutProgress READ workoutProgress NOTIFY workoutTick)   // 0..1
    Q_PROPERTY(int currentIntervalIndex READ currentIntervalIndex NOTIFY workoutIntervalChanged)
    Q_PROPERTY(QString currentIntervalName READ currentIntervalName NOTIFY workoutIntervalChanged)
    Q_PROPERTY(int intervalRemainingSec READ intervalRemainingSec NOTIFY workoutTick)
    Q_PROPERTY(int workoutTargetWatts READ workoutTargetWatts NOTIFY workoutTargetChanged)   // incl. intensity, 0 = none
    Q_PROPERTY(int nextTargetWatts READ nextTargetWatts NOTIFY workoutTargetChanged)
    Q_PROPERTY(int workoutCompliance READ workoutCompliance NOTIFY workoutTick)   // % of seconds within ±10 %
    Q_PROPERTY(bool workoutErg READ workoutErg WRITE setWorkoutErg NOTIFY workoutErgChanged)
    // the trainer follows the workout right now (hasWorkout && workoutErg && !workoutFinished)
    Q_PROPERTY(bool workoutErgActive READ workoutErgActive NOTIFY workoutErgChanged)
    Q_PROPERTY(bool workoutFinished READ workoutFinished NOTIFY workoutErgChanged)
    Q_PROPERTY(int intensityPercent READ intensityPercent WRITE setIntensityPercent NOTIFY workoutTargetChanged)

    // sprint in focus (see the class comment); metres along the route
    Q_PROPERTY(bool sprintAvailable READ sprintAvailable NOTIFY routeChanged)
    Q_PROPERTY(int sprintCount READ sprintCount NOTIFY routeChanged)
    Q_PROPERTY(QString sprintName READ sprintName NOTIFY sprintChanged)
    Q_PROPERTY(double sprintAnnounceM READ sprintAnnounceM NOTIFY sprintChanged)
    Q_PROPERTY(double sprintStartM READ sprintStartM NOTIFY sprintChanged)
    Q_PROPERTY(double sprintFinishM READ sprintFinishM NOTIFY sprintChanged)
    Q_PROPERTY(double sprintLengthM READ sprintLengthM NOTIFY sprintChanged)
    // "none" (no sprint on this route), "ahead" (still to come), "running",
    // "done" (timed this ride), "void" (paused / stopped during it),
    // "passed" (the ride started beyond its start line)
    Q_PROPERTY(QString sprintState READ sprintState NOTIFY sprintChanged)
    Q_PROPERTY(double sprintElapsedSec READ sprintElapsedSec NOTIFY sprintChanged)   // while running
    Q_PROPERTY(double sprintTimeSec READ sprintTimeSec NOTIFY sprintChanged)         // this ride's time, 0 = none
    Q_PROPERTY(double sprintBestSec READ sprintBestSec NOTIFY sprintChanged)         // best on the route, 0 = none
    // the best before this ride's sprint (0 = it was the first)
    Q_PROPERTY(double sprintPreviousBestSec READ sprintPreviousBestSec NOTIFY sprintChanged)
    Q_PROPERTY(bool sprintNewBest READ sprintNewBest NOTIFY sprintChanged)
    Q_PROPERTY(int sprintAttempts READ sprintAttempts NOTIFY sprintChanged)          // saved, incl. this ride

    // KOM in focus: the same (komState as sprintState)
    Q_PROPERTY(bool komAvailable READ komAvailable NOTIFY routeChanged)
    Q_PROPERTY(int komCount READ komCount NOTIFY routeChanged)
    Q_PROPERTY(QString komName READ komName NOTIFY komChanged)
    Q_PROPERTY(double komStartM READ komStartM NOTIFY komChanged)
    Q_PROPERTY(double komFinishM READ komFinishM NOTIFY komChanged)
    Q_PROPERTY(double komLengthM READ komLengthM NOTIFY komChanged)
    Q_PROPERTY(double komGainM READ komGainM NOTIFY komChanged)
    Q_PROPERTY(double komGradePercent READ komGradePercent NOTIFY komChanged)
    Q_PROPERTY(QString komState READ komState NOTIFY komChanged)
    Q_PROPERTY(double komElapsedSec READ komElapsedSec NOTIFY komChanged)
    Q_PROPERTY(double komTimeSec READ komTimeSec NOTIFY komChanged)
    Q_PROPERTY(double komBestSec READ komBestSec NOTIFY komChanged)
    Q_PROPERTY(double komPreviousBestSec READ komPreviousBestSec NOTIFY komChanged)
    Q_PROPERTY(bool komNewBest READ komNewBest NOTIFY komChanged)
    Q_PROPERTY(int komAttempts READ komAttempts NOTIFY komChanged)

public:
    enum class Mode { Sim, Erg };
    enum class State { Idle, Running, Paused, Finished };

    static constexpr int kPedalPowerWatts = 30;   // below (and no cadence): not pedaling
    // Zwift protocol, coasting on the flat / uphill: the speed follows the
    // trainer's simulated speed (VirtualGears::hubTrainerSpeedMps) down,
    // braking at most this much harder than the road model (no jump)
    static constexpr double kHubCoastCatchUpMps2 = RideResistance::kHubCoastCatchUpMps2;
    // Zwift protocol: the flywheel stopped - the bike brakes to 0 at this rate
    // instead of jumping to 0 (RideResistance::hubSpeedStep)
    static constexpr double kHubStopDecelMps2 = RideResistance::kHubStopDecelMps2;
    static constexpr int kCoastCadenceRpm = RideResistance::kCoastCadenceRpm;
    static constexpr int kMinGearWatts = RideResistance::kMinGearWatts;
    static constexpr int kMaxWatts = 1500;
    static constexpr int kTargetIntervalMs = RideResistance::kGearIntervalMs;
    static constexpr int kMinTargetChangeWatts = RideResistance::kMinWattsChange;
    static constexpr double kCadenceSmoothingSec = 1.5;
    static constexpr int kDefaultDifficulty = 100;
    static constexpr int kDifficultyStep = 10;
    // ERG
    static constexpr int kMinErgWatts = 25;
    static constexpr int kControllerErgStep = 10;   // Zwift Click / PgUp / PgDn
    static constexpr int kControllerIntensityStep = 5;   // % (workout ERG; Training's +/-)
    static constexpr int kMinIntensity = -50;            // WorkoutEngine::setIntensityOffset range
    static constexpr int kMaxIntensity = 50;
    static constexpr double kComplianceTolerance = 0.10; // ±10 % of the target
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
    // Every rate change makes the player (GStreamer) flush: the rate follows
    // the rider only when it moved by >= kRateMinChange and kRateMinIntervalMs
    // passed since the last change; a jump of >= kRateBigChange (start,
    // sprint, stop) goes out at once.
    static constexpr double kRateMinChange = 0.05;
    static constexpr double kRateBigChange = 0.25;
    static constexpr int kRateMinIntervalMs = 2000;
    static constexpr double kMinPlaySpeedKmh = 2.0;
    static constexpr double kSeekThresholdSec = 2.0;
    static constexpr double kRateCorrectionPerSec = 0.1;   // rate × (1 - drift·this)
    static constexpr double kMaxRateCorrection = 0.1;      // ±10 %
    static constexpr double kHoldAheadSec = 0.5;           // video ahead at min rate: hold
    static constexpr int kSeekCooldownMs = 1500;
    static constexpr int kReportMaxAgeMs = 3000;

    enum class Kind { Video, Gpx };

    RlvSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears,
               RlvLibrary *library = nullptr, QObject *parent = nullptr);
    RlvSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears, RlvLibrary *library,
               Kind kind, QObject *parent = nullptr);
    ~RlvSession() override;

    Kind kindValue() const { return m_kind; }
    bool isGpx() const { return m_kind == Kind::Gpx; }
    QString kindName() const { return isGpx() ? QStringLiteral("gpx") : QStringLiteral("video"); }
    // Main.qml page of this session ("video" / "gpx", VirtualGears::activePage())
    QString pageName() const { return kindName(); }
    // Key of this session in the other sessions' start blockers ("rlv" / "gpx")
    QString blockerKey() const { return isGpx() ? QStringLiteral("gpx") : QStringLiteral("rlv"); }

    // An empty paused free ride is given up when the video ride starts.
    void setFreeRideSession(FreeRideSession *freeRide) { m_freeRide = freeRide; }
    // Another session started / paused / ended (SessionGuard): the page's
    // resistance hold follows (holdBlocked()).
    void otherSessionChanged() { updateHold(); }
    void setTrainingSession(TrainingSession *training);
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
    bool running() const override { return m_state == State::Running; }
    bool paused() const { return m_state == State::Paused; }
    bool hasRecording() const { return m_recordedSec > 0 || m_activeMs > 0; }
    bool inProgress() const { return running() || (paused() && hasRecording()); }
    bool abandonable() const { return paused() && !hasRecording(); }
    void abandon() { if (abandonable()) finish(false); }   // empty paused: given up
    bool finishedRide() const { return m_state == State::Finished; }
    bool completed() const { return m_completed; }
    QString mode() const { return modeName(m_mode); }
    Mode modeValue() const { return m_mode; }
    void setMode(const QString &mode);
    static QString modeName(Mode m);
    // "sim" / "erg"; the old "power" and "gears" -> Sim. nullopt if unknown.
    static std::optional<Mode> parseMode(const QString &text);

    const RlvRoute &route() const override { return m_route; }
    bool hasRoute() const override { return m_route.isValid(); }
    int routeIndex() const;
    QString routePath() const override { return m_route.sourcePath(); }
    QString routeName() const { return m_route.name(); }
    QString routeTitle() const { return m_route.title(); }
    QString routeCountry() const { return m_route.country(); }
    QUrl videoUrl() const;
    bool videoAvailable() const { return !isGpx() && m_route.hasVideo(); }   // never for a GPX ride
    double totalDistanceM() const { return m_route.totalDistance(); }
    double totalAscentM() const { return m_route.ascent(); }
    int routeDurationSec() const;
    double minElevationM() const { return m_route.minElevation(); }
    double maxElevationM() const { return m_route.maxElevation(); }
    QVariantList profile() const { return m_profile; }
    QVariantList segmentMarkers() const;
    QVariantList informations() const;
    QString lastError() const { return m_lastError; }

    double startDistanceM() const override { return m_startDistance; }
    double distanceMeters() const override { return m_distance; }
    double riddenMeters() const { return m_ridden; }
    // to the route's end (looping: to this lap's finish line)
    double remainingMeters() const { return std::max(0.0, m_returning ? m_distance : totalDistanceM() - m_distance); }

    bool loop() const { return m_loop; }
    void setLoop(bool on);
    bool canLoop() const { return isGpx() && hasRoute(); }
    bool roundTrip() const { return m_route.isRoundTrip(); }
    bool looping() const { return m_loopRide; }
    bool loopRide() const override { return isGpx() && (active() || m_state == State::Finished ? m_loopRide : m_loop); }
    bool returning() const override { return m_returning; }
    int lap() const { return m_lapsDone + 1; }
    int lapsDone() const override { return m_lapsDone; }
    double lastLapSec() const { return m_lastLapSec; }
    double lapElapsedSec() const { return std::max(0.0, (double(m_activeMs) - m_lapStartMs) / 1000.0); }
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
    double speedKmh() const override { return speedMps() * 3.6; }
    double avgSpeedKmh() const;
    int watts() const;
    int displayWatts() const;
    int avgWatts() const;
    int maxWatts() const { return m_maxWatts; }
    double kilojoules() const { return m_energyJ / 1000.0; }
    int cadence() const override;
    int heartRate() const;
    bool coasting() const;
    // Zwift protocol: pedaling = power >= kPedalPowerWatts OR cadence >=
    // kCoastCadenceRpm (the cadence lags at the start: 0 rpm at 180 W; light
    // pedaling reads 0 W on the Victory); coasting = not pedaling while the
    // flywheel turns. Light pedaling (cadence, power below kPedalPowerWatts):
    // the speed follows the trainer's flywheel for the gear.
    bool pedaling() const;

    int difficultyPercent() const { return m_difficulty; }
    void setDifficulty(int percent);
    int targetWatts() const { return m_ergTarget; }
    void setTargetWatts(int watts);
    int minTargetWatts() const { return kMinErgWatts; }
    int maxTargetWatts() const { return kMaxWatts; }
    int sentTargetWatts() const { return m_res.lastWatts(); }
    // ERG outside a ride: ERG selected on the open Video Ride page
    // (VirtualGears::activePage() == "video") puts the trainer in ERG at the
    // target right away before Start (trainer connected, no other
    // session busy); Simulation / leaving the page releases it. Paused:
    // released, ERG comes with resume().
    // Simulation with virtual gears on the open page holds the virtual gear
    // resistance the same way (shifting changes the watts before Start).
    bool ergHolding() const { return m_ergHold && ergNow(); }
    bool gearHolding() const { return m_ergHold && !ergNow(); }
    bool holdWanted() const;
    int gearTargetWatts() const;
    double simulationGradePercent() const { return m_res.lastGrade(); }
    bool simulationActive() const { return running() && m_res.simulating(); }
    bool simulationFallback() const;
    bool virtualGears() const;
    RideResistance::Path path() const;
    const RideResistance &resistance() const { return m_res; }   // diagnostics
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
    Q_INVOKABLE bool start();   // the only way a ride starts
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

    // Workout during the ride. attachWorkout(): cycling workouts with
    // intervals only, not while a ride is running/paused (false + lastError);
    // turns workoutErg on and resets the intensity. detachWorkout() also
    // during a ride (the selected mode takes over at once).
    Q_INVOKABLE bool attachWorkout(qint64 workoutId);
    bool attachWorkout(const Workout &workout);
    Q_INVOKABLE void detachWorkout();
    Q_INVOKABLE void skipInterval();                    // while the ride is active
    Q_INVOKABLE void adjustIntensity(int deltaPercent) { setIntensityPercent(intensityPercent() + deltaPercent); }

    bool hasWorkout() const { return m_hasWorkout; }
    qint64 workoutId() const { return m_hasWorkout ? m_workout.workout().id : -1; }
    QString workoutName() const { return m_hasWorkout ? m_workout.workout().name : QString(); }
    QVariantList workoutIntervals() const { return m_workoutIntervals; }
    int workoutTotalSec() const { return m_hasWorkout ? m_workout.totalSec() : 0; }
    int workoutElapsedSec() const { return m_hasWorkout ? m_workout.elapsedSec() : 0; }
    int workoutRemainingSec() const { return m_hasWorkout ? m_workout.remainingSec() : 0; }
    double workoutProgress() const { return m_hasWorkout ? m_workout.progress() : 0.0; }
    int currentIntervalIndex() const { return m_hasWorkout ? m_workout.currentIndex() : -1; }
    QString currentIntervalName() const;
    int intervalRemainingSec() const { return m_hasWorkout ? m_workout.intervalRemainingSec() : 0; }
    int workoutTargetWatts() const;
    int nextTargetWatts() const;
    int workoutCompliance() const;
    bool workoutErg() const { return m_workoutErg; }
    void setWorkoutErg(bool on);
    bool workoutErgActive() const { return m_hasWorkout && m_workoutErg && !m_workoutFinished; }
    bool workoutFinished() const { return m_workoutFinished; }
    int intensityPercent() const { return m_workout.intensityOffset(); }
    void setIntensityPercent(int percent);
    const WorkoutEngine &workoutEngine() const { return m_workout; }

    // timed segments: the sprints and KOMs of the route
    enum class SegmentPhase { Idle, Running, Done, Void };
    QVector<RouteSegment> routeSegments() const override { return m_route.segments(); }
    QString segmentKey() const { return m_segmentKey; }   // sprint::routeKey() of the route ("" = no segments)
    QString sprintKey() const { return sprintCount() ? m_segmentKey : QString(); }
    QString komKey() const { return komCount() ? m_segmentKey : QString(); }
    int sprintCount() const { return countOf(RouteSegment::Sprint); }
    int komCount() const { return countOf(RouteSegment::Kom); }
    SegmentPhase sprintPhase() const { return focusSeg(RouteSegment::Sprint).phase; }
    bool sprintAvailable() const { return sprintCount() > 0; }
    QString sprintName() const { return focusSeg(RouteSegment::Sprint).name; }
    double sprintAnnounceM() const { return std::max(0.0, sprintStartM() - sprint::kAnnounceM); }
    double sprintStartM() const { return focusSeg(RouteSegment::Sprint).start; }
    double sprintFinishM() const { return focusSeg(RouteSegment::Sprint).finish; }
    double sprintLengthM() const { return focusSeg(RouteSegment::Sprint).length(); }
    QString sprintState() const { return timedState(focusSeg(RouteSegment::Sprint)); }
    double sprintElapsedSec() const { return timedElapsed(focusSeg(RouteSegment::Sprint)); }
    double sprintTimeSec() const { return focusSeg(RouteSegment::Sprint).time; }
    double sprintBestSec() const { return focusSeg(RouteSegment::Sprint).best; }
    double sprintPreviousBestSec() const { return focusSeg(RouteSegment::Sprint).previousBest; }
    bool sprintNewBest() const { return focusSeg(RouteSegment::Sprint).newBest; }
    int sprintAttempts() const { return focusSeg(RouteSegment::Sprint).attempts; }

    bool komAvailable() const { return komCount() > 0; }
    QString komName() const { return focusSeg(RouteSegment::Kom).name; }
    double komStartM() const { return focusSeg(RouteSegment::Kom).start; }
    double komFinishM() const { return focusSeg(RouteSegment::Kom).finish; }
    double komLengthM() const { return focusSeg(RouteSegment::Kom).length(); }
    double komGainM() const;
    double komGradePercent() const { return komLengthM() > 0 ? komGainM() / komLengthM() * 100.0 : 0.0; }
    QString komState() const { return timedState(focusSeg(RouteSegment::Kom)); }
    double komElapsedSec() const { return timedElapsed(focusSeg(RouteSegment::Kom)); }
    double komTimeSec() const { return focusSeg(RouteSegment::Kom).time; }
    double komBestSec() const { return focusSeg(RouteSegment::Kom).best; }
    double komPreviousBestSec() const { return focusSeg(RouteSegment::Kom).previousBest; }
    bool komNewBest() const { return focusSeg(RouteSegment::Kom).newBest; }
    int komAttempts() const { return focusSeg(RouteSegment::Kom).attempts; }
    // ERG now: mode "erg" or the workout ERG.
    bool ergNow() const { return m_mode == Mode::Erg || workoutErgActive(); }
    // Watts the Erg path holds: the workout target (workout ERG) or targetWatts.
    int ergTargetNow() const;

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
    void lastRideIdChanged();
    void startRefused(const QString &reason);
    void shifted(int direction);   // ERG step from a controller (gear shifts: VirtualGears::shifted)
    void workoutChanged();
    void workoutTick();
    void workoutIntervalChanged();
    void workoutTargetChanged();
    void workoutErgChanged();
    void workoutCompleted();       // the attached workout ran to its end during the ride
    void sprintChanged();
    // a sprint was timed (and saved); previousBestSec 0 = the first on this route
    void sprintFinished(double timeSec, double previousBestSec, bool newBest);
    void komChanged();
    void komFinished(double timeSec, double previousBestSec, bool newBest);
    void loopChanged();
    void lapChanged();
    void lapCompleted(int lap, double timeSec);   // looping: lap (1, 2, ...) just finished

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
    void applyTarget(bool force = false);
    int desiredGearWatts() const;
    RideResistance::GearDrive gearDrive(double cadenceRpm) const;
    double controlCadence() const;   // cadence the resistance follows (< coast = coasting)
    bool sendResistance(bool force);  // the current path's command (ride and page hold)
    void updateTimer();
    void abandonOthers();
    void syncRideProfile();
    void resetLastRideId();
    void clearRecording();
    qint64 saveRide();
    void releaseTrainer();
    void updateHold(bool force = false);
    bool holdBlocked() const;
    void requestSeek();
    bool canSelect() const;
    void rebuildWorkoutIntervals();
    void advanceWorkout(int ms);
    void workoutResistanceChanged();   // workout ERG on/off: path, grade, target
    void emitWorkoutAll();
    QString settingKey(const char *name) const;   // "rlv/<name>" / "gpx/<name>"
    QString rideWhat() const;                     // "video ride" / "GPX ride"
    QString routeWhat() const;                    // "video" / "route"
    // One timed stretch of the route (a sprint, a KOM): where it lies, this
    // ride's timing and the profile's saved times on it.
    struct TimedSegment
    {
        RouteSegment::Kind kindId = RouteSegment::Sprint;
        QString kind;               // Database segment kind: "sprint" / "kom"
        QString name;               // display name ("Sprint 2" when unnamed)
        double start = 0, finish = 0;
        QString key;                // sprint::routeKey() ("" = none: the empty focus)
        SegmentPhase phase = SegmentPhase::Idle;
        double startMs = 0;         // m_activeMs at the start line (interpolated)
        double time = 0;            // this ride's time (s), 0 = none
        double best = 0, previousBest = 0;
        bool newBest = false;
        int attempts = 0;
        qint64 recordId = -1;       // this ride's saved time (linked to the ride on save)
        bool rearm = false;         // looping: a new lap - timed again from its start line
        bool valid() const { return !key.isEmpty(); }
        double length() const { return finish - start; }
    };
    void segmentsRouteChanged();                       // new route: where, key, best
    void loadBest(TimedSegment &seg, qint64 profileId);
    void loadBests(qint64 profileId);
    int countOf(RouteSegment::Kind kind) const;
    int focusIndex(RouteSegment::Kind kind) const;     // -1: none of this kind
    const TimedSegment &focusSeg(RouteSegment::Kind kind) const;
    void emitSegments(bool sprints, bool koms);
    void resetTimed(TimedSegment &seg);                // a new ride: nothing timed yet
    void resetSegments();
    void voidSegments();                               // paused / stopped: a running one is void
    void discardSegments();                            // the ride is discarded: its saved times too
    // one riding step from (d0, t0): true when seg changed; finished = timed now
    bool stepTimed(TimedSegment &seg, double d0, qint64 t0, bool &finished);
    void updateSegments(double d0, qint64 t0);
    double lapAscent() const;                          // climbed on this lap so far
    void clearLaps();
    void advance(double step, double d0, qint64 t0);   // move along the route (laps)
    void lapDone(double atMs);
    QString timedState(const TimedSegment &seg) const;
    double timedElapsed(const TimedSegment &seg) const;

    const Kind m_kind = Kind::Video;
    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    VirtualGears *m_gears;
    RideResistance m_res;
    QPointer<RlvLibrary> m_library;
    QPointer<FreeRideSession> m_freeRide;
    QPointer<TrainingSession> m_training;
    QHash<QString, std::function<bool()>> m_startBlockers;
    QHash<QString, std::function<void()>> m_abandonCancellers;

    RlvRoute m_route;
    QVariantList m_profile;
    Mode m_mode = Mode::Sim;
    int m_ergTarget = 150;
    bool m_ergHold = false;       // ERG target / virtual gear held outside a ride (updateHold())
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
    double m_ridden = 0;            // metres ridden this ride (all laps)

    // loop (GPX Ride)
    bool m_loop = false;            // the setting
    bool m_loopRide = false;        // this ride loops (m_loop at the start)
    bool m_returning = false;       // there and back: on the way back (m_distance falls)
    int m_lapsDone = 0;
    double m_lapStartMs = 0;        // m_activeMs at this lap's start
    double m_lastLapSec = 0;
    double m_ascentBase = 0;        // metres climbed on the laps before this one
    double m_lapFrom = 0;           // where this lap began on the route
    QVector<qint64> m_lapRecordIds; // sprint / KOM times of earlier laps (linked / discarded with the ride)
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
    qint64 m_rateAtMs = -100000;  // m_nowMs of the last playback rate change

    QDateTime m_startedAt;
    Profile m_rideProfile;
    QVector<Sample> m_samples;
    int m_recordedSec = 0;
    double m_energyJ = 0;
    double m_powerSum = 0;
    int m_maxWatts = 0;
    qint64 m_lastRideId = -1;

    // workout during the ride
    WorkoutEngine m_workout;
    bool m_hasWorkout = false;
    bool m_workoutErg = true;
    bool m_workoutFinished = false;
    QVariantList m_workoutIntervals;
    int m_workoutLastIndex = -2;
    int m_workoutLastTarget = -1;
    int m_complianceSec = 0;       // recorded workout seconds with a target
    int m_compliantSec = 0;        // ... within ±kComplianceTolerance

    // timed segments
    static constexpr qint64 kResultShowMs = 20000;   // a result keeps the focus (riding time)
    QVector<TimedSegment> m_segments;
    QString m_segmentKey;
    TimedSegment m_noSegment;                        // the focus when there is none
    int m_lastFinished[2] = { -1, -1 };              // per kind: the latest timed this ride
    qint64 m_lastFinishedMs[2] = { 0, 0 };
    int m_focusSeen[2] = { -1, -1 };                 // focus last announced (sprintChanged / komChanged)
};

} // namespace watt
