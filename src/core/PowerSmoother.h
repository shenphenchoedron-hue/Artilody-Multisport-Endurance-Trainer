#pragma once

#include <QtGlobal>
#include <deque>

namespace watt {

// Time-based rolling average of power samples for the DISPLAY of cycling
// watts (the "3 s power" of most training apps). Pedalling is never exactly
// even between both legs, so the raw per-sample power of a trainer jumps up
// and down; the average over the last few seconds is much easier to read.
//
// The average is time-weighted, not sample-count based: every sample holds
// its value until the next sample arrives (trainers send at 1-4 Hz and not
// always regularly), so a burst of fast samples does not outweigh a slower
// part of the window. A sample holds for at most holdMs (default: the window
// length) - when the trainer stops sending, its last value fades out of the
// window and the average falls to 0 once no sample is younger than holdMs.
//
// Timestamps are plain milliseconds of a monotonic clock supplied by the
// caller, which keeps the class deterministic for tests.
class PowerSmoother
{
public:
    explicit PowerSmoother(int windowMs = 3000);

    int windowMs() const { return m_windowMs; }
    void setWindowMs(int ms);
    // Maximum time a sample's value is held without a newer one (<= 0: window).
    int holdMs() const { return m_holdMs > 0 ? m_holdMs : m_windowMs; }
    void setHoldMs(int ms) { m_holdMs = ms; }

    void addSample(qint64 timeMs, int watts);
    void clear();
    bool isEmpty() const { return m_samples.empty(); }

    // Average power over (nowMs - window, nowMs]. With a single fresh sample
    // (no elapsed time yet) that sample's value is returned; 0 when there is
    // no sample younger than holdMs.
    double average(qint64 nowMs) const;
    int averageRounded(qint64 nowMs) const;

private:
    void prune(qint64 nowMs);

    struct Sample { qint64 t; int watts; };
    std::deque<Sample> m_samples;
    int m_windowMs;
    int m_holdMs = 0;
};

} // namespace watt
