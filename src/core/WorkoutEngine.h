#pragma once

#include "Workout.h"

namespace watt {

// Pure, timer-free workout state machine. The caller advances the clock,
// which makes the engine fully deterministic and unit-testable.
class WorkoutEngine
{
public:
    enum class State { Idle, Running, Paused, Finished };

    void load(const Workout &workout, int ftp);
    void loadFreeRide();           // open-ended ride without targets
    void setFtp(int ftp) { m_ftp = ftp; }

    void start();
    void pause();
    void resume();
    void finish();
    void reset();

    // Advances the clock by ms when running. Returns true when the target
    // power or the current interval changed.
    bool advance(int ms);
    void skipInterval();
    void seek(int second);

    // +/- percent applied to all targets (e.g. +5 => 105%).
    void setIntensityOffset(int percent);
    int intensityOffset() const { return m_intensityOffset; }

    State state() const { return m_state; }
    bool isFreeRide() const { return m_freeRide; }
    const Workout &workout() const { return m_workout; }
    int ftp() const { return m_ftp; }

    qint64 elapsedMs() const { return m_elapsedMs; }
    int elapsedSec() const { return int(m_elapsedMs / 1000); }
    int totalSec() const;          // 0 for free ride
    int remainingSec() const;
    double progress() const;       // 0..1
    int currentIndex() const;      // -1 for free ride / outside
    int intervalElapsedSec() const;
    int intervalRemainingSec() const;
    int targetWatts() const;       // 0 when no target
    int nextTargetWatts() const;   // target of the next interval
    int targetCadence() const;

private:
    Workout m_workout;
    int m_ftp = 250;
    bool m_freeRide = false;
    State m_state = State::Idle;
    qint64 m_elapsedMs = 0;
    int m_intensityOffset = 0;
};

} // namespace watt
