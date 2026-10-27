#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>

namespace watt {

enum class LinkState { Disconnected = 0, Connecting = 1, Connected = 2 };

// Common interface for every connectable sensor (BLE or simulated).
class SensorDevice : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString protocol() const = 0;   // e.g. "Bluetooth FTMS"
    virtual bool isSimulated() const { return false; }
    virtual void connectDevice() = 0;
    virtual void disconnectDevice() = 0;

    LinkState state() const { return m_state; }
    int rssi() const { return m_rssi; }
    virtual void setAutoReconnect(bool on) { m_autoReconnect = on; }
    // A scan saw this (dropped) device advertising again: reconnect at once
    // instead of waiting for the reconnect backoff. Returns true when a
    // connection attempt was started (see BleLink::reconnectNow()).
    virtual bool reconnectNow() { return false; }
    bool autoReconnect() const { return m_autoReconnect; }

signals:
    void stateChanged(watt::LinkState state);
    void rssiChanged(int rssi);
    void logMessage(const QString &text, bool ok);
    // GATT services/characteristics found after connecting (diagnostics).
    void gattReport(const QString &summary);

protected:
    void setState(LinkState s);
    void setRssi(int rssi);

    bool m_autoReconnect = true;

private:
    LinkState m_state = LinkState::Disconnected;
    int m_rssi = 0;
};

// Smart trainer: measures power/cadence and can be controlled (ERG).
class TrainerDevice : public SensorDevice
{
    Q_OBJECT
public:
    using SensorDevice::SensorDevice;

    virtual void setTargetPower(int watts) = 0;
    virtual void setFreeRide() = 0;          // release ERG, flat road simulation
    // Gradient simulation (FTMS Set Indoor Bike Simulation Parameters, 0x11):
    // the trainer computes its resistance from grade, wind, Crr and Cw (kg/m)
    // and the wheel speed. Default: not supported (ignored).
    virtual void setSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM)
    {
        Q_UNUSED(gradePercent) Q_UNUSED(windSpeedMps) Q_UNUSED(crr) Q_UNUSED(cwKgPerM)
    }
    // 1 = supported, 0 = not supported (feature bit missing or the command
    // was rejected), -1 = not known yet.
    virtual int simulationSupport() const { return 0; }
    virtual QStringList capabilities() const = 0;
    virtual bool supportsControl() const = 0;

    // --- what the trainer actually does (from its answers, not our requests) ---
    // "simulation", "erg", "resistance", "stopped", "no control", "unknown"
    // ("" = not connected). "simulation" only after the trainer accepted a
    // Set Indoor Bike Simulation command (or reported the change itself).
    virtual QString trainerMode() const { return QString(); }
    // One line for the UI: "Simulation – trainer accepted −2.00 %", or the
    // problem ("Trainer rejected the gradient command (Invalid parameter)").
    virtual QString controlStatus() const { return QString(); }
    // The last control command was rejected / not answered, or the trainer is
    // not in the mode the app asked for (controlStatus() explains).
    virtual bool controlProblem() const { return false; }
    // Supported Inclination Range (FTMS 0x2AD5), when the trainer reports it.
    virtual bool inclinationRangeKnown() const { return false; }
    virtual double minInclinationPercent() const { return -100.0; }
    virtual double maxInclinationPercent() const { return 100.0; }
    // "This trainer doesn't simulate descents below 0 %" when the range says
    // so; "" otherwise.
    virtual QString inclinationNote() const { return QString(); }

signals:
    // trainerMode() / controlStatus() / controlProblem() / inclination range changed
    void controlStatusChanged();
    void powerMeasured(int watts);
    void cadenceMeasured(double rpm);
    void speedMeasured(double kmh);
    void heartRateMeasured(int bpm);         // some trainers forward HR
    void capabilitiesChanged();
    void simulationSupportChanged();
    // The connected device turned out to be a rowing machine (Concept2 rowing
    // service, or FTMS Rower Data without Indoor Bike Data).
    void rowerDetected();
};

class HeartRateDevice : public SensorDevice
{
    Q_OBJECT
public:
    using SensorDevice::SensorDevice;

signals:
    void heartRateMeasured(int bpm);
};

// Handlebar controller for virtual shifting (Zwift Click). Reports shift
// requests only; what a shift does is up to the consumer (FreeRideSession).
class ControllerDevice : public SensorDevice
{
    Q_OBJECT
public:
    using SensorDevice::SensorDevice;

    int batteryLevel() const { return m_battery; }   // percent, -1 = unknown

signals:
    // +1 = plus / shift up (harder), -1 = minus / shift down (easier).
    // One per button press plus auto-repeat while a button is held.
    void shiftRequested(int direction);
    void buttonsChanged(bool plus, bool minus);
    void batteryChanged(int percent);

protected:
    void setBatteryLevel(int percent)
    {
        if (percent == m_battery)
            return;
        m_battery = percent;
        emit batteryChanged(percent);
    }

private:
    int m_battery = -1;
};

// Rowing machine (Concept2 PM5 over BLE, or simulated). Unlike a trainer it
// cannot be controlled; it only reports normalized live data.
//
// Consumer contract (e.g. a RowingSession in src/core):
//  * All live values are properties (QML-bindable) that share one NOTIFY
//    signal, measurementChanged(), emitted at most once per incoming update
//    batch (PM5: every sample period, 250 ms; simulator: 250 ms).
//  * strokeMeasured(strokeCount, watts) fires once per new stroke.
//  * powerMeasured(watts) fires whenever the normalized power changes value,
//    including the decay to 0. "watts" is the power of the last stroke, held
//    until the next stroke; it drops to 0 after effectivePowerHoldMs() (max of
//    powerHoldMs(), default 5 s, and 1.5 stroke periods at the current rate)
//    without a new stroke, or immediately when the monitor reports that rowing
//    stopped (active -> inactive transition).
//  * heartRateMeasured(bpm) only fires for valid readings (>0) reported by the
//    rowing monitor itself (e.g. a strap paired to the PM5).
//  * workoutReset() fires when elapsed time or distance jumps backwards, i.e.
//    a new workout was started on the monitor, or when the total work distance
//    of the workout drops. A jump backwards while the monitor is in an
//    interval state (PM5 restarts elapsed time/distance per interval) is not a
//    reset. Values are NOT reset when the BLE link drops; a reconnect
//    continues where the monitor is.
//  * hasData() is false until this device object committed its first
//    measurement (a new object never reuses the values of an older one).
//    The cumulative values are only meaningful once it is true. hasData() is
//    already true while the signals of that first measurement are emitted.
//  * workoutState is the raw PM5 OBJ_WORKOUTSTATE_T value (see
//    pm5::WorkoutState); rowingActive mirrors the PM5 rowing state.
//  * strokesPerMinute is the stroke rate reported by the monitor when it
//    reports one (> 0). When the monitor reports no stroke rate for more than
//    kStrokeRateTimeoutMs (missing characteristic, a firmware sending 0, ...)
//    while strokes come in, the rate is computed from the time between the
//    last strokes (up to 4 intervals, 60 / mean interval, clamped to
//    5..80 spm); it decays when the strokes slow down and drops to 0
//    kStrokeRateTimeoutMs after the last stroke. strokeRateEstimated tells
//    which value is shown.
class RowerDevice : public SensorDevice
{
    Q_OBJECT
    Q_PROPERTY(QString deviceId READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY measurementChanged)
    Q_PROPERTY(double distanceMeters READ distanceMeters NOTIFY measurementChanged)
    Q_PROPERTY(int watts READ watts NOTIFY measurementChanged)
    Q_PROPERTY(int averageWatts READ averageWatts NOTIFY measurementChanged)
    Q_PROPERTY(int strokesPerMinute READ strokesPerMinute NOTIFY measurementChanged)
    Q_PROPERTY(int heartRateBpm READ heartRateBpm NOTIFY measurementChanged)
    Q_PROPERTY(double pace500Seconds READ pace500Seconds NOTIFY measurementChanged)
    Q_PROPERTY(double averagePace500Seconds READ averagePace500Seconds NOTIFY measurementChanged)
    Q_PROPERTY(double speedMetersPerSecond READ speedMetersPerSecond NOTIFY measurementChanged)
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY measurementChanged)
    Q_PROPERTY(int calories READ calories NOTIFY measurementChanged)
    Q_PROPERTY(int dragFactor READ dragFactor NOTIFY measurementChanged)
    Q_PROPERTY(int workoutState READ workoutState NOTIFY measurementChanged)
    Q_PROPERTY(bool rowingActive READ rowingActive NOTIFY measurementChanged)
    Q_PROPERTY(int deviceStrokesPerMinute READ deviceStrokesPerMinute NOTIFY measurementChanged)
    Q_PROPERTY(int computedStrokesPerMinute READ computedStrokesPerMinute NOTIFY measurementChanged)
    Q_PROPERTY(bool strokeRateEstimated READ strokeRateEstimated NOTIFY measurementChanged)

public:
    static constexpr int kStrokeRateTimeoutMs = 6000;
    static constexpr int kMinComputedSpm = 5;
    static constexpr int kMaxComputedSpm = 80;
    enum class StrokeRateSource { None, Device, Strokes };
    explicit RowerDevice(QObject *parent = nullptr);

    bool connected() const { return state() == LinkState::Connected; }
    double elapsedSeconds() const { return m_elapsed; }
    double distanceMeters() const { return m_distance; }
    int watts() const { return m_watts; }
    int averageWatts() const { return m_avgWatts; }
    int strokesPerMinute() const { return m_spm; }
    int heartRateBpm() const { return m_hr; }
    double pace500Seconds() const { return m_pace; }
    double averagePace500Seconds() const { return m_avgPace; }
    double speedMetersPerSecond() const { return m_speed; }
    int strokeCount() const { return m_strokes; }
    int calories() const { return m_calories; }
    int dragFactor() const { return m_drag; }
    int workoutState() const { return m_workoutState; }
    bool rowingActive() const { return m_rowingActive; }
    // Last stroke rate value reported by the monitor (0 = none / not rowing).
    int deviceStrokesPerMinute() const { return m_deviceSpm; }
    // Stroke rate computed from the stroke timestamps (0 = not enough strokes).
    int computedStrokesPerMinute() const { return m_computedSpm; }
    StrokeRateSource strokeRateSource() const { return m_spmSource; }
    bool strokeRateEstimated() const { return m_spmSource == StrokeRateSource::Strokes; }
    static QString strokeRateSourceText(StrokeRateSource s);

    // Short description of where the live data comes from ("Concept2 rowing
    // service", "FTMS Rower Data", ...). Default: protocol().
    virtual QString dataSourceText() const { return protocol(); }
    // Multi-line diagnostics of the live data path (diagnostics report).
    virtual QString dataDiagnostics() const;

    bool hasData() const { return m_hasData; }
    int totalWorkDistance() const { return m_totalWork; }   // -1 = not reported

    int powerHoldMs() const { return m_holdMs; }
    void setPowerHoldMs(int ms) { m_holdMs = ms; }
    // Hold time actually used: max(powerHoldMs(), 1.5 * 60000 / spm).
    int effectivePowerHoldMs() const;

    // Decays held stroke power (called by an internal 250 ms timer; public so
    // tests can drive it with a fake clock). `msSinceLastStroke` < 0 means
    // "use the internal clock".
    void checkPowerHold(qint64 msSinceLastStroke = -1);
    // Re-evaluates the stroke rate (fallback decay, stale monitor value);
    // called by the same timer. Commits when the value changed.
    void checkStrokeRate();

    // Monotonic clock in ms used for the stroke-rate logic (tests inject a
    // fake one).
    using Clock = std::function<qint64()>;
    void setClockForTesting(Clock clock) { m_clock = std::move(clock); }
    qint64 nowMs() const;

    // Target power of a rowing plan (<= 0 = free rowing). A real monitor
    // cannot be controlled and ignores it; the simulated athlete follows it.
    virtual void requestTargetPower(int watts) { Q_UNUSED(watts) }

signals:
    void connectedChanged(bool connected);
    void measurementChanged();
    void powerMeasured(int watts);
    void strokeMeasured(int strokeCount, int watts);
    void heartRateMeasured(int bpm);
    void rowingActiveChanged(bool active);
    void workoutStateChanged(int state);
    void workoutReset();

protected:
    // Subclasses feed raw values, then call commitMeasurement() once.
    void applyElapsed(double seconds);
    void applyDistance(double meters);
    void applyStroke(int strokeCount, int watts);   // new stroke when count changes
    void applyStrokeRate(int spm);
    void applyHeartRate(int bpm);                    // <= 0 means invalid
    void applyPace(double pace500Seconds);
    void applyAveragePace(double pace500Seconds);
    void applySpeed(double metersPerSecond);
    void applyAverageWatts(int watts);
    void applyCalories(int kcal);
    void applyDragFactor(int drag);
    void applyWorkoutState(int workoutState, bool rowingActive);
    void applyTotalWorkDistance(int meters);         // PM5 total work distance of the workout
    void commitMeasurement();
    // Clears live (not cumulative) values, e.g. after the link dropped.
    void clearLiveValues();

private:
    void setWatts(int w);
    void updateStrokeRate();
    int computeStrokeRate(qint64 now);
    template<typename T> void assign(T &field, T value)
    {
        if (field != value) {
            field = value;
            m_dirty = true;
        }
    }

    double m_elapsed = 0, m_distance = 0, m_pace = 0, m_avgPace = 0, m_speed = 0;
    int m_watts = 0, m_avgWatts = 0, m_spm = 0, m_hr = 0, m_strokes = 0, m_calories = 0, m_drag = 0;
    int m_workoutState = 0;
    int m_totalWork = -1;
    bool m_rowingActive = false;
    bool m_haveStroke = false;
    bool m_dirty = false;
    bool m_hasData = false;
    bool m_clearing = false;
    bool m_pendingDrop = false;        // elapsed/distance went backwards in this batch
    bool m_pendingWorkDrop = false;    // total work distance went backwards in this batch
    bool m_pendingState = false, m_pendingActive = false, m_pendingStroke = false,
         m_pendingPower = false, m_pendingHr = false;
    int m_holdMs = 5000;
    QElapsedTimer m_sinceStroke;
    QTimer m_holdTimer;

    // stroke rate: monitor value and fallback from stroke timestamps
    struct StrokeMark
    {
        qint64 t;
        int count;
    };
    QVector<StrokeMark> m_strokeMarks;   // last <= 5 strokes
    int m_deviceSpm = 0;
    int m_computedSpm = 0;
    qint64 m_deviceSpmUpdateMs = -1;     // last applyStrokeRate() (any value)
    qint64 m_devicePositiveSpmMs = -1;   // last applyStrokeRate() with a value > 0
    StrokeRateSource m_spmSource = StrokeRateSource::None;
    Clock m_clock;
};

} // namespace watt

Q_DECLARE_METATYPE(watt::LinkState)
