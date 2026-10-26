#pragma once

#include "Gearing.h"
#include "Profile.h"
#include "Ride.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace watt {

class Database;
class ProfileManager;
class SensorHub;
class TrainingSession;

// Free ride without a workout. Exposed to QML as "FreeRide" (Artilody.Backend).
//
// Modes ("mode"):
//   "gears" - virtual shifting. The rider's bike gearing (GearSetup, per
//             profile) turns cadence into a virtual speed
//                 v = cadence / 60 · (chainring / cog) · wheel circumference
//             and the road model (Gearing.h, road::powerForSpeed) turns that
//             speed into the power the rider would need on a road with the
//             selected grade:
//                 P = (Crr·m·g·cosθ + m·g·sinθ + ½·ρ·CdA·v²) · v / η
//             P is sent to the trainer as ERG target (FTMS Set Target Power),
//             so this works with every trainer that supports ERG. Shifting
//             harder raises v at the same cadence and therefore P. The target
//             uses a smoothed cadence (kCadenceSmoothingSec), is resent at
//             most every kTargetIntervalMs (shifts and grade changes apply
//             immediately), clamped to kMinGearWatts..kMaxWatts, and released
//             (trainer free ride) while coasting (cadence < kCoastCadenceRpm).
//             While coasting the virtual speed decays with the road
//             resistance (road::coastDeceleration).
//   "erg"   - hold targetWatts (ERG) as long as you like.
//   "free"  - no resistance control at all (trainer's own free ride).
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
    Q_PROPERTY(bool ergEnabled READ ergEnabled WRITE setErgEnabled NOTIFY modeChanged)

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

    // targets
    Q_PROPERTY(int targetWatts READ targetWatts WRITE setTargetWatts NOTIFY targetChanged)       // ERG target
    Q_PROPERTY(int gearTargetWatts READ gearTargetWatts NOTIFY targetChanged)   // virtual gearing power (gears mode)
    Q_PROPERTY(int requestedWatts READ requestedWatts NOTIFY targetChanged)     // last value sent (0 = released)
    Q_PROPERTY(double gradePercent READ gradePercent WRITE setGrade NOTIFY gradeChanged)
    Q_PROPERTY(int minTargetWatts READ minTargetWatts CONSTANT)
    Q_PROPERTY(int maxTargetWatts READ maxTargetWatts CONSTANT)
    Q_PROPERTY(double minGrade READ minGrade CONSTANT)
    Q_PROPERTY(double maxGrade READ maxGrade CONSTANT)

    // gear
    Q_PROPERTY(QVariantMap gear READ gear NOTIFY gearChanged)
    Q_PROPERTY(QString gearLabel READ gearLabel NOTIFY gearChanged)
    Q_PROPERTY(int gearIndex READ gearIndex NOTIFY gearChanged)
    Q_PROPERTY(int gearCount READ gearCount NOTIFY gearChanged)
    Q_PROPERTY(double gearRatio READ gearRatio NOTIFY gearChanged)
    Q_PROPERTY(int frontIndex READ frontIndex NOTIFY gearChanged)
    Q_PROPERTY(int rearIndex READ rearIndex NOTIFY gearChanged)
    Q_PROPERTY(int frontTeeth READ frontTeeth NOTIFY gearChanged)
    Q_PROPERTY(int rearTeeth READ rearTeeth NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftUp READ canShiftUp NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftDown READ canShiftDown NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftFrontUp READ canShiftFrontUp NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftFrontDown READ canShiftFrontDown NOTIFY gearChanged)
    Q_PROPERTY(QVariantList gears READ gears NOTIFY gearSetupChanged)          // [{index, label, ratio, front, rear}]
    Q_PROPERTY(QVariantMap gearSetup READ gearSetup NOTIFY gearSetupChanged)
    Q_PROPERTY(bool sequential READ sequential WRITE setSequential NOTIFY gearSetupChanged)
    Q_PROPERTY(QVariantList chainringPresets READ chainringPresets CONSTANT)
    Q_PROPERTY(QVariantList cassettePresets READ cassettePresets CONSTANT)
    Q_PROPERTY(QVariantList wheelPresets READ wheelPresets CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    // charts: mean per seriesStep seconds (<= kMaxSeriesPoints values)
    Q_PROPERTY(QVariantList powerSeries READ powerSeries NOTIFY seriesChanged)
    Q_PROPERTY(QVariantList speedSeries READ speedSeries NOTIFY seriesChanged)        // km/h
    Q_PROPERTY(QVariantList heartRateSeries READ heartRateSeries NOTIFY seriesChanged)
    Q_PROPERTY(int seriesStep READ seriesStep NOTIFY seriesChanged)

    // devices
    Q_PROPERTY(bool trainerConnected READ trainerConnected NOTIFY devicesChanged)
    Q_PROPERTY(bool clickConnected READ clickConnected NOTIFY devicesChanged)
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY rideSaved)

public:
    enum class Mode { Gears, Erg, Free };
    enum class State { Idle, Ready, Running, Paused, Finished };

    static constexpr int kStartCadenceRpm = 20;     // same thresholds as TrainingSession
    static constexpr int kStartPowerWatts = 30;
    static constexpr int kCoastCadenceRpm = 15;     // below: not pedaling (coasting)
    static constexpr int kMinGearWatts = 20;        // lowest gears-mode target while pedaling
    static constexpr int kMinErgWatts = 25;
    static constexpr int kMaxWatts = 1500;
    static constexpr int kDefaultErgWatts = 150;
    static constexpr int kControllerErgStep = 10;   // Zwift Click +/- in ERG mode
    static constexpr int kTargetIntervalMs = 1000;  // gears mode resend interval
    static constexpr int kMinTargetChangeWatts = 2;
    static constexpr double kCadenceSmoothingSec = 1.5;
    static constexpr double kMinGrade = -15.0;
    static constexpr double kMaxGrade = 20.0;
    static constexpr int kMaxSeriesPoints = 720;

    FreeRideSession(Database *db, ProfileManager *profiles, SensorHub *hub, QObject *parent = nullptr);
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
    void flushSettings();   // writes a pending gear setup change now

    QString state() const;
    State stateValue() const { return m_state; }
    bool active() const { return m_state == State::Running || m_state == State::Paused; }
    bool running() const { return m_state == State::Running; }
    bool paused() const { return m_state == State::Paused; }
    bool waiting() const { return m_state == State::Ready; }
    QString mode() const;
    Mode modeValue() const { return m_mode; }
    void setMode(const QString &mode);
    bool ergEnabled() const { return m_mode == Mode::Erg; }
    void setErgEnabled(bool on);

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

    int targetWatts() const { return m_targetWatts; }
    void setTargetWatts(int watts);
    int gearTargetWatts() const;
    int requestedWatts() const { return m_lastSent > 0 ? m_lastSent : 0; }
    double gradePercent() const { return m_grade; }
    void setGrade(double percent);
    int minTargetWatts() const { return kMinErgWatts; }
    int maxTargetWatts() const { return kMaxWatts; }
    double minGrade() const { return kMinGrade; }
    double maxGrade() const { return kMaxGrade; }

    QVariantMap gear() const;
    QString gearLabel() const { return m_gearing.label(); }
    int gearIndex() const { return m_gearing.gearIndex(); }
    int gearCount() const { return m_gearing.gearCount(); }
    double gearRatio() const { return m_gearing.ratio(); }
    int frontIndex() const { return m_gearing.frontIndex(); }
    int rearIndex() const { return m_gearing.rearIndex(); }
    int frontTeeth() const { return m_gearing.frontTeeth(); }
    int rearTeeth() const { return m_gearing.rearTeeth(); }
    bool canShiftUp() const { return m_gearing.canShiftUp(); }
    bool canShiftDown() const { return m_gearing.canShiftDown(); }
    bool canShiftFrontUp() const { return m_gearing.canShiftFrontUp(); }
    bool canShiftFrontDown() const { return m_gearing.canShiftFrontDown(); }
    QVariantList gears() const;
    QVariantMap gearSetup() const;
    bool sequential() const { return m_gearing.setup().sequential; }
    void setSequential(bool on);
    const Gearing &gearing() const { return m_gearing; }
    static QVariantList chainringPresets() { return GearSetup::chainringPresets(); }
    static QVariantList cassettePresets() { return GearSetup::cassettePresets(); }
    static QVariantList wheelPresets() { return GearSetup::wheelPresets(); }
    QString lastError() const { return m_lastError; }

    QVariantList powerSeries() const;
    QVariantList speedSeries() const;
    QVariantList heartRateSeries() const;
    int seriesStep() const;
    const QVector<Sample> &samples() const { return m_samples; }

    bool trainerConnected() const;
    bool clickConnected() const { return m_clickConnected; }
    void setClickConnected(bool on);
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

    Q_INVOKABLE bool shiftUp();
    Q_INVOKABLE bool shiftDown();
    Q_INVOKABLE bool shiftFrontUp();
    Q_INVOKABLE bool shiftFrontDown();
    Q_INVOKABLE bool setGear(int front, int rear);
    Q_INVOKABLE bool setGearIndex(int index);
    Q_INVOKABLE void adjustTargetWatts(int delta);
    Q_INVOKABLE void adjustGrade(double delta);
    // Validates and stores the gear setup of the current profile.
    // chainrings/cogs: lists of numbers (any order). false + lastError on error.
    Q_INVOKABLE bool setGearSetup(const QVariantList &chainrings, const QVariantList &cogs, int circumferenceMm,
                                  bool sequential);
    Q_INVOKABLE bool setBikePhysics(double bikeWeightKg, double cda, double crr);
    Q_INVOKABLE void resetGearSetup();
    // "" when valid, else the error text (no change).
    Q_INVOKABLE QString validateGearSetup(const QVariantList &chainrings, const QVariantList &cogs,
                                          int circumferenceMm) const;
    // "11,12,13-15" style text -> [11,12,13,15]; [] when invalid
    Q_INVOKABLE QVariantList parseTeeth(const QString &text) const;
    // Gears of a setup that is not stored yet (gear setup dialog preview), in
    // the order the given shifting mode uses (easiest first):
    // [{index, label, ratio, front, rear, frontTeeth, rearTeeth, speedKmh, watts}]
    // speedKmh at cadenceRpm, watts = power for that speed on a flat road for
    // the current rider and bike. [] when the setup is invalid.
    Q_INVOKABLE QVariantList previewGears(const QVariantList &chainrings, const QVariantList &cogs,
                                          int circumferenceMm, bool sequential, int cadenceRpm = 90) const;
    // GearSetup::defaults() in the gearSetup map format (setup dialog "Reset").
    Q_INVOKABLE QVariantMap defaultGearSetup() const;

public slots:
    // Controller buttons (Zwift Click / keyboard): +1 = plus (harder), -1 = minus.
    // gears mode: shift up/down; erg mode: target ±kControllerErgStep W.
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
    void gearChanged();
    void gearSetupChanged();
    void seriesChanged();
    void devicesChanged();
    void lastErrorChanged();
    void started();
    void finished();
    void rideSaved(qint64 id);
    void startRefused(const QString &reason);
    void shifted(int direction);    // a shift (or ERG step) from shiftUp/Down or a controller

private:
    void setState(State s);
    void onTimer();
    void process(int ms);
    void updateModel(double dt);
    void recordUpTo(int second);
    void checkPedaling();
    void applyTarget(bool force = false);
    int desiredTarget() const;
    int computeGearTarget(double cadenceRpm) const;
    void afterShift(bool shifted, int direction);
    void clearRecording();
    qint64 saveRide();
    void loadSetup();
    void scheduleSave();
    void saveSettingsNow();
    void setError(const QString &e);
    void releaseTrainer();
    static QVariantMap setupToMap(const GearSetup &s);
    QVariantList downsample(const QVector<double> &data, bool ignoreZero, int decimals) const;

    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    QPointer<TrainingSession> m_training;
    QHash<QString, std::function<bool()>> m_startBlockers;

    Gearing m_gearing;
    qint64 m_setupProfileId = -1;
    bool m_reloadPending = false;
    QTimer m_saveTimer;
    bool m_savePending = false;

    Mode m_mode = Mode::Gears;
    State m_state = State::Idle;
    int m_targetWatts = kDefaultErgWatts;
    double m_grade = 0.0;
    bool m_clickConnected = false;
    QString m_lastError;

    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_manualClock = false;
    int m_minSaveSec = 30;
    qint64 m_activeMs = 0;
    int m_sinceSendMs = 0;
    int m_lastSent = -1;          // -1 nothing sent yet, 0 released, > 0 ERG target
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
