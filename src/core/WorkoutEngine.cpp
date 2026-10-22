#include "WorkoutEngine.h"

#include <algorithm>
#include <cmath>

namespace watt {

void WorkoutEngine::load(const Workout &workout, int ftp)
{
    m_workout = workout;
    m_ftp = ftp;
    m_freeRide = workout.intervals.isEmpty();
    reset();
}

void WorkoutEngine::loadFreeRide()
{
    m_workout = Workout();
    m_workout.name = QStringLiteral("Free Ride");
    m_workout.category = QStringLiteral("Free Ride");
    m_freeRide = true;
    reset();
}

void WorkoutEngine::start()
{
    if (m_state == State::Idle || m_state == State::Finished) {
        m_elapsedMs = 0;
        m_state = State::Running;
    } else if (m_state == State::Paused) {
        m_state = State::Running;
    }
}

void WorkoutEngine::pause()
{
    if (m_state == State::Running)
        m_state = State::Paused;
}

void WorkoutEngine::resume()
{
    if (m_state == State::Paused)
        m_state = State::Running;
}

void WorkoutEngine::finish()
{
    if (m_state == State::Running || m_state == State::Paused)
        m_state = State::Finished;
}

void WorkoutEngine::reset()
{
    m_state = State::Idle;
    m_elapsedMs = 0;
    m_intensityOffset = 0;
}

bool WorkoutEngine::advance(int ms)
{
    if (m_state != State::Running || ms <= 0)
        return false;
    const int prevIndex = currentIndex();
    const int prevTarget = targetWatts();
    m_elapsedMs += ms;
    if (!m_freeRide && m_elapsedMs >= qint64(totalSec()) * 1000) {
        m_elapsedMs = qint64(totalSec()) * 1000;
        m_state = State::Finished;
    }
    return prevIndex != currentIndex() || prevTarget != targetWatts();
}

void WorkoutEngine::skipInterval()
{
    if (m_freeRide)
        return;
    const int idx = currentIndex();
    if (idx < 0)
        return;
    seek(m_workout.intervalStartSec(idx + 1));
}

void WorkoutEngine::seek(int second)
{
    const int total = totalSec();
    if (!m_freeRide)
        second = std::clamp(second, 0, total);
    m_elapsedMs = qint64(std::max(0, second)) * 1000;
    if (!m_freeRide && second >= total && m_state != State::Idle)
        m_state = State::Finished;
}

void WorkoutEngine::setIntensityOffset(int percent)
{
    m_intensityOffset = std::clamp(percent, -50, 50);
}

int WorkoutEngine::totalSec() const
{
    return m_freeRide ? 0 : m_workout.totalDurationSec();
}

int WorkoutEngine::remainingSec() const
{
    return m_freeRide ? 0 : std::max(0, totalSec() - elapsedSec());
}

double WorkoutEngine::progress() const
{
    const int total = totalSec();
    if (total <= 0)
        return 0.0;
    return std::clamp(m_elapsedMs / (total * 1000.0), 0.0, 1.0);
}

int WorkoutEngine::currentIndex() const
{
    if (m_freeRide)
        return -1;
    int idx = m_workout.intervalIndexAt(elapsedSec());
    if (idx < 0 && !m_workout.intervals.isEmpty() && elapsedSec() >= totalSec())
        idx = m_workout.intervals.size() - 1;
    return idx;
}

int WorkoutEngine::intervalElapsedSec() const
{
    const int idx = currentIndex();
    if (idx < 0)
        return elapsedSec();
    return elapsedSec() - m_workout.intervalStartSec(idx);
}

int WorkoutEngine::intervalRemainingSec() const
{
    const int idx = currentIndex();
    if (idx < 0)
        return 0;
    return std::max(0, m_workout.intervals.at(idx).durationSec - intervalElapsedSec());
}

int WorkoutEngine::targetWatts() const
{
    const int idx = currentIndex();
    if (idx < 0)
        return 0;
    const Interval &i = m_workout.intervals.at(idx);
    const double t = m_elapsedMs / 1000.0 - m_workout.intervalStartSec(idx);
    const double w = i.percentAt(t) * m_ftp * (100 + m_intensityOffset) / 100.0;
    return int(std::lround(w));
}

int WorkoutEngine::nextTargetWatts() const
{
    const int idx = currentIndex();
    if (idx < 0 || idx + 1 >= m_workout.intervals.size())
        return 0;
    const Interval &i = m_workout.intervals.at(idx + 1);
    return int(std::lround(i.startPercent * m_ftp * (100 + m_intensityOffset) / 100.0));
}

int WorkoutEngine::targetCadence() const
{
    const int idx = currentIndex();
    return idx < 0 ? 0 : m_workout.intervals.at(idx).cadence;
}

} // namespace watt
