#include "SensorDevices.h"

#include <algorithm>

namespace watt {

void SensorDevice::setState(LinkState s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void SensorDevice::setRssi(int rssi)
{
    if (m_rssi == rssi)
        return;
    m_rssi = rssi;
    emit rssiChanged(rssi);
}

// --- RowerDevice ------------------------------------------------------------

namespace {
// PM5 OBJ_WORKOUTSTATE_T values 3..9: inside an interval workout, where the
// monitor restarts elapsed time and distance for every interval.
bool isIntervalState(int workoutState)
{
    return workoutState >= 3 && workoutState <= 9;
}
} // namespace

RowerDevice::RowerDevice(QObject *parent)
    : SensorDevice(parent)
{
    m_holdTimer.setInterval(250);
    connect(&m_holdTimer, &QTimer::timeout, this, [this] { checkPowerHold(); });
    connect(this, &SensorDevice::stateChanged, this, [this](LinkState s) {
        static_cast<void>(s);
        const bool on = connected();
        if (on) {
            m_holdTimer.start();
        } else {
            m_holdTimer.stop();
            clearLiveValues();
        }
        emit connectedChanged(on);
    });
}

int RowerDevice::effectivePowerHoldMs() const
{
    // A slow rate (e.g. 16 spm = 3.75 s per stroke) must not drop the power
    // between two strokes.
    const int strokeHold = m_spm > 0 ? int(90000 / m_spm) : 0;
    return std::max(m_holdMs, strokeHold);
}

void RowerDevice::checkPowerHold(qint64 msSinceLastStroke)
{
    if (m_watts == 0)
        return;
    qint64 age = msSinceLastStroke;
    const int hold = effectivePowerHoldMs();
    if (age < 0)
        age = m_sinceStroke.isValid() ? m_sinceStroke.elapsed() : hold;
    if (age >= hold) {
        setWatts(0);
        commitMeasurement();
    }
}

void RowerDevice::applyElapsed(double seconds)
{
    seconds = std::max(0.0, seconds);
    // Different characteristics carry slightly different timestamps; only a
    // clear jump backwards means the monitor started a new workout/interval.
    if (seconds + 1.0 < m_elapsed)
        m_pendingDrop = true;
    assign(m_elapsed, seconds);
}

void RowerDevice::applyDistance(double meters)
{
    meters = std::max(0.0, meters);
    if (meters + 1.0 < m_distance)
        m_pendingDrop = true;
    assign(m_distance, meters);
}

void RowerDevice::applyTotalWorkDistance(int meters)
{
    meters = std::max(0, meters);
    if (m_totalWork >= 0 && meters < m_totalWork)
        m_pendingWorkDrop = true;
    m_totalWork = meters;
}

void RowerDevice::applyStroke(int strokeCount, int watts)
{
    strokeCount = std::max(0, strokeCount);
    if (strokeCount == 0) {
        // No stroke yet in this workout (e.g. right after a reset): nothing to
        // report, and the next counted stroke is new whatever its number.
        assign(m_strokes, 0);
        m_haveStroke = false;
        return;
    }
    if (m_haveStroke && strokeCount == m_strokes)
        return;   // repeated packet for a stroke we already handled
    m_haveStroke = true;
    assign(m_strokes, strokeCount);
    setWatts(std::max(0, watts));
    m_sinceStroke.start();
    m_pendingStroke = true;
}

void RowerDevice::applyStrokeRate(int spm) { assign(m_spm, std::max(0, spm)); }

void RowerDevice::applyHeartRate(int bpm)
{
    assign(m_hr, std::max(0, bpm));
    if (bpm > 0)
        m_pendingHr = true;
}

void RowerDevice::applyPace(double pace500Seconds) { assign(m_pace, std::max(0.0, pace500Seconds)); }
void RowerDevice::applyAveragePace(double pace500Seconds) { assign(m_avgPace, std::max(0.0, pace500Seconds)); }
void RowerDevice::applySpeed(double metersPerSecond) { assign(m_speed, std::max(0.0, metersPerSecond)); }
void RowerDevice::applyAverageWatts(int watts) { assign(m_avgWatts, std::max(0, watts)); }
void RowerDevice::applyCalories(int kcal) { assign(m_calories, std::max(0, kcal)); }
void RowerDevice::applyDragFactor(int drag) { assign(m_drag, std::max(0, drag)); }

void RowerDevice::applyWorkoutState(int workoutState, bool rowingActive)
{
    if (m_workoutState != workoutState) {
        m_workoutState = workoutState;
        m_pendingState = true;
        m_dirty = true;
    }
    if (m_rowingActive != rowingActive) {
        // Only the transition to "not rowing" drops the held power: the first
        // stroke may arrive (on another characteristic) before the general
        // status reports rowing as active.
        if (m_rowingActive && !rowingActive)
            setWatts(0);
        m_rowingActive = rowingActive;
        m_pendingActive = true;
        m_dirty = true;
    }
}

void RowerDevice::setWatts(int w)
{
    if (m_watts == w)
        return;
    m_watts = w;
    m_dirty = true;
    m_pendingPower = true;
}

void RowerDevice::clearLiveValues()
{
    setWatts(0);
    assign(m_spm, 0);
    assign(m_speed, 0.0);
    assign(m_pace, 0.0);
    assign(m_hr, 0);
    if (m_rowingActive) {
        m_rowingActive = false;
        m_pendingActive = true;
        m_dirty = true;
    }
    m_clearing = true;
    commitMeasurement();
    m_clearing = false;
}

void RowerDevice::commitMeasurement()
{
    // Values are complete before any signal is emitted, so every handler sees
    // the measurement of this batch.
    if (!m_clearing)
        m_hasData = true;
    // Elapsed/distance restart per interval inside an interval workout; that is
    // only a new workout when the total work distance dropped as well.
    const bool reset = m_pendingWorkDrop || (m_pendingDrop && !isIntervalState(m_workoutState));
    m_pendingDrop = false;
    m_pendingWorkDrop = false;
    if (reset) {
        if (!m_pendingStroke)
            m_haveStroke = false;   // the next stroke packet belongs to the new workout
        emit workoutReset();
    }
    if (m_pendingState) {
        m_pendingState = false;
        emit workoutStateChanged(m_workoutState);
    }
    if (m_pendingActive) {
        m_pendingActive = false;
        emit rowingActiveChanged(m_rowingActive);
    }
    if (m_pendingStroke) {
        m_pendingStroke = false;
        emit strokeMeasured(m_strokes, m_watts);
    }
    if (m_pendingPower) {
        m_pendingPower = false;
        emit powerMeasured(m_watts);
    }
    if (m_pendingHr) {
        m_pendingHr = false;
        emit heartRateMeasured(m_hr);
    }
    if (m_dirty) {
        m_dirty = false;
        emit measurementChanged();
    }
}

} // namespace watt
