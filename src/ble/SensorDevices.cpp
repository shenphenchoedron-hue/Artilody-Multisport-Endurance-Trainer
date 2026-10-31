#include "SensorDevices.h"

#include <algorithm>
#include <cmath>

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

// --- TreadmillDevice ----------------------------------------------------------

TreadmillDevice::TreadmillDevice(QObject *parent)
    : SensorDevice(parent)
{
    m_staleTimer.setInterval(1000);
    connect(&m_staleTimer, &QTimer::timeout, this, [this] { checkStale(); });
    connect(this, &SensorDevice::stateChanged, this, [this](LinkState) {
        const bool on = connected();
        if (on) {
            m_staleTimer.start();
        } else {
            m_staleTimer.stop();
            clearLiveValues();
        }
        emit connectedChanged(on);
    });
}

void TreadmillDevice::applyMeasurement(double speedKmh, double inclinePercent, bool inclineKnown, double distanceM,
                                       int heartRate)
{
    if (speedKmh >= 0 && std::isfinite(speedKmh))
        m_speed = std::clamp(speedKmh, 0.0, 40.0);
    if (inclineKnown && std::isfinite(inclinePercent)) {
        m_incline = std::clamp(inclinePercent, -10.0, 40.0);
        m_inclineKnown = true;
    }
    if (distanceM >= 0 && std::isfinite(distanceM))
        m_distance = distanceM;
    if (heartRate > 0) {
        m_hr = heartRate;
        emit heartRateMeasured(heartRate);
    }
    ++m_packets;
    m_hasData = true;
    m_sinceData.start();
    emit measurementChanged();
}

void TreadmillDevice::setSpeedOffsetKmh(double kmh)
{
    kmh = std::round(std::clamp(kmh, -kMaxSpeedOffset, kMaxSpeedOffset) * 10.0) / 10.0;
    if (qFuzzyCompare(1.0 + kmh, 1.0 + m_offset))
        return;
    m_offset = kmh;
    emit measurementChanged();
}

void TreadmillDevice::clearLiveValues()
{
    if (m_speed == 0 && m_hr == 0)
        return;
    m_speed = 0;
    m_hr = 0;
    emit measurementChanged();
}

void TreadmillDevice::checkStale(qint64 msSinceData)
{
    if (msSinceData < 0)
        msSinceData = m_sinceData.isValid() ? m_sinceData.elapsed() : qint64(kStaleMs) + 1;
    if (msSinceData > kStaleMs && m_speed > 0) {
        m_speed = 0;
        emit measurementChanged();
    }
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
    connect(&m_holdTimer, &QTimer::timeout, this, [this] {
        checkPowerHold();
        checkStrokeRate();
    });
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

qint64 RowerDevice::nowMs() const
{
    if (m_clock)
        return m_clock();
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    return clock.elapsed();
}

QString RowerDevice::strokeRateSourceText(StrokeRateSource s)
{
    switch (s) {
    case StrokeRateSource::Device: return QStringLiteral("monitor");
    case StrokeRateSource::Strokes: return QStringLiteral("computed from stroke timing");
    case StrokeRateSource::None: break;
    }
    return QStringLiteral("none");
}

QString RowerDevice::dataDiagnostics() const
{
    const qint64 now = nowMs();
    auto ago = [now](qint64 t) {
        return t < 0 ? QStringLiteral("never") : QStringLiteral("%1 s ago").arg((now - t) / 1000.0, 0, 'f', 1);
    };
    QStringList l;
    l << QStringLiteral("Stroke rate: %1 SPM (source: %2)").arg(m_spm).arg(strokeRateSourceText(m_spmSource));
    l << QStringLiteral("  monitor value: %1 SPM (last update %2, last value > 0 %3)")
             .arg(m_deviceSpm)
             .arg(ago(m_deviceSpmUpdateMs), ago(m_devicePositiveSpmMs));
    l << QStringLiteral("  computed from strokes: %1 SPM (%2 strokes in window, last stroke %3)")
             .arg(m_computedSpm)
             .arg(m_strokeMarks.size())
             .arg(ago(m_strokeMarks.isEmpty() ? -1 : m_strokeMarks.last().t));
    l << QStringLiteral("Live: %1 W, stroke count %2, distance %3 m, elapsed %4 s, pace %5 s/500m, HR %6, rowing %7, workout state %8")
             .arg(m_watts)
             .arg(m_strokes)
             .arg(m_distance, 0, 'f', 1)
             .arg(m_elapsed, 0, 'f', 1)
             .arg(m_pace, 0, 'f', 1)
             .arg(m_hr)
             .arg(m_rowingActive ? QStringLiteral("active") : QStringLiteral("inactive"))
             .arg(m_workoutState);
    return l.join(QLatin1Char('\n'));
}

int RowerDevice::computeStrokeRate(qint64 now)
{
    if (m_strokeMarks.isEmpty())
        return 0;
    const StrokeMark &last = m_strokeMarks.last();
    const qint64 since = now - last.t;
    if (since > kStrokeRateTimeoutMs) {
        m_strokeMarks.clear();   // stopped: the next stroke starts a new window
        return 0;
    }
    if (m_strokeMarks.size() < 2)
        return 0;
    const StrokeMark &first = m_strokeMarks.first();
    const qint64 span = last.t - first.t;
    const int strokes = last.count - first.count;   // > 1 per interval when packets were missed
    if (strokes <= 0 || span < 100 * strokes)
        return 0;   // implausible timing (> 600 spm)
    double perStroke = double(span) / strokes;
    // Slowing down / stopped: the running gap bounds the rate, so it decays
    // instead of holding the last value until the timeout.
    if (since > 1.5 * perStroke)
        perStroke = double(since);
    const int spm = int(std::lround(60000.0 / perStroke));
    if (spm < kMinComputedSpm)
        return 0;
    return std::min(spm, kMaxComputedSpm);
}

void RowerDevice::updateStrokeRate()
{
    const qint64 now = nowMs();
    m_computedSpm = computeStrokeRate(now);
    // The monitor's value counts while it keeps reporting one. A monitor that
    // just reported a positive rate and now says 0 has stopped rowing; a
    // monitor that never reports a positive rate (or stopped reporting) is
    // replaced by the rate computed from the strokes.
    const bool deviceReporting = m_deviceSpmUpdateMs >= 0 && now - m_deviceSpmUpdateMs <= kStrokeRateTimeoutMs;
    int spm = 0;
    StrokeRateSource source = StrokeRateSource::None;
    if (deviceReporting && m_deviceSpm > 0) {
        spm = m_deviceSpm;
        source = StrokeRateSource::Device;
    } else if (deviceReporting && m_devicePositiveSpmMs >= 0 && now - m_devicePositiveSpmMs <= kStrokeRateTimeoutMs) {
        source = StrokeRateSource::Device;   // monitor reports "stopped"
    } else if (m_computedSpm > 0) {
        spm = m_computedSpm;
        source = StrokeRateSource::Strokes;
    }
    if (m_spmSource != source) {
        m_spmSource = source;
        m_dirty = true;
    }
    assign(m_spm, spm);
}

void RowerDevice::checkStrokeRate()
{
    const int before = m_spm;
    const StrokeRateSource sourceBefore = m_spmSource;
    const int computedBefore = m_computedSpm;
    updateStrokeRate();
    if (m_spm != before || m_spmSource != sourceBefore || m_computedSpm != computedBefore) {
        m_dirty = true;
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
    const qint64 now = nowMs();
    if (!m_strokeMarks.isEmpty()
        && (strokeCount <= m_strokeMarks.last().count || now - m_strokeMarks.last().t > kStrokeRateTimeoutMs))
        m_strokeMarks.clear();   // new workout or a pause: start a new timing window
    m_strokeMarks.append({ now, strokeCount });
    while (m_strokeMarks.size() > 5)   // 4 intervals
        m_strokeMarks.removeFirst();
    m_haveStroke = true;
    assign(m_strokes, strokeCount);
    setWatts(std::max(0, watts));
    m_sinceStroke.start();
    m_pendingStroke = true;
}

void RowerDevice::applyStrokeRate(int spm)
{
    spm = std::max(0, spm);
    const qint64 now = nowMs();
    m_deviceSpmUpdateMs = now;
    if (spm > 0)
        m_devicePositiveSpmMs = now;
    assign(m_deviceSpm, spm);
}

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
    assign(m_deviceSpm, 0);
    m_computedSpm = 0;
    m_strokeMarks.clear();
    m_deviceSpmUpdateMs = m_devicePositiveSpmMs = -1;
    m_spmSource = StrokeRateSource::None;
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
    if (!m_clearing) {
        m_hasData = true;
        updateStrokeRate();
    }
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
