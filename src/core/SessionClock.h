#pragma once

#include <QtGlobal>

namespace watt {

// Wall-clock step of a session timer tick. The sessions advance by the time
// measured since the last tick (QElapsedTimer); after a long gap - the
// computer slept, the process was stopped - that time was not ridden: a gap
// longer than kMaxSessionClockGapMs counts as a pause (0), so no phantom
// time / distance is added. Shorter gaps (a busy GUI thread) count fully.
constexpr qint64 kMaxSessionClockGapMs = 5000;

constexpr int sessionClockStep(qint64 elapsedMs)
{
    return elapsedMs <= 0 || elapsedMs > kMaxSessionClockGapMs ? 0 : int(elapsedMs);
}

} // namespace watt
