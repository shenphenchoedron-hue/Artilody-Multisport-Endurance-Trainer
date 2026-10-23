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

void RowerDevice::checkPowerHold(qint64 msSinceLastStroke)
{
    if (m_watts == 0)
        return;
    qint64 age = msSinceLastStroke;
    if (age < 0)
        age = m_sinceStroke.isValid() ? m_sinceStroke.elapsed() : m_holdMs;
    if (age >= m_holdMs) {
        setWatts(0);
        commitMeasurement();
    }
}

void RowerDevice::applyElapsed(double seconds)
{
    seconds = std::max(0.0, seconds);
    // Different characteristics carry slightly different timestamps; only a
    // clear jump backwards means the monitor started a new workout.
    if (seconds + 1.0 < m_elapsed)
        m_pendingReset = true;
    assign(m_elapsed, seconds);
}

void RowerDevice::applyDistance(double meters)
{
    meters = std::max(0.0, meters);
    if (meters + 1.0 < m_distance)
        m_pendingReset = true;
    assign(m_distance, meters);
}

void RowerDevice::applyStroke(int strokeCount, int watts)
{
    if (m_pendingReset && strokeCount < m_strokes)
        m_haveStroke = false;
    if (m_haveStroke && strokeCount == m_strokes)
        return;   // repeated packet for a stroke we already handled
    m_haveStroke = true;
    assign(m_strokes, std::max(0, strokeCount));
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
        m_rowingActive = rowingActive;
        m_pendingActive = true;
        m_dirty = true;
    }
    if (!rowingActive)
        setWatts(0);
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
    commitMeasurement();
}

void RowerDevice::commitMeasurement()
{
    if (m_pendingReset) {
        m_pendingReset = false;
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
