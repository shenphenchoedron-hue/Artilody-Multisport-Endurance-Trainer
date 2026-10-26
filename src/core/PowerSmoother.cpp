#include "PowerSmoother.h"

#include <algorithm>
#include <cmath>

namespace watt {

PowerSmoother::PowerSmoother(int windowMs)
    : m_windowMs(std::max(1, windowMs))
{
}

void PowerSmoother::setWindowMs(int ms)
{
    m_windowMs = std::max(1, ms);
}

void PowerSmoother::addSample(qint64 timeMs, int watts)
{
    // keep the deque ordered (a clock never runs backwards, but be safe)
    if (!m_samples.empty() && timeMs < m_samples.back().t)
        timeMs = m_samples.back().t;
    if (!m_samples.empty() && m_samples.back().t == timeMs)
        m_samples.back().watts = std::max(0, watts);   // same instant: newest wins
    else
        m_samples.push_back({ timeMs, std::max(0, watts) });
    prune(timeMs);
}

void PowerSmoother::clear()
{
    m_samples.clear();
}

void PowerSmoother::prune(qint64 nowMs)
{
    // Drop samples that can no longer reach into the window: of the samples
    // before the window start only the last one is needed (its value holds
    // into the window).
    const qint64 start = nowMs - m_windowMs;
    while (m_samples.size() >= 2 && m_samples[1].t <= start)
        m_samples.pop_front();
}

double PowerSmoother::average(qint64 nowMs) const
{
    if (m_samples.empty())
        return 0;
    const qint64 hold = holdMs();
    const Sample &last = m_samples.back();
    if (nowMs - last.t >= hold && nowMs > last.t)
        return 0;   // the trainer stopped sending: nothing fresh to show

    const qint64 start = nowMs - m_windowMs;
    double energy = 0;     // watt * ms
    qint64 covered = 0;    // ms of the window covered by a held sample
    for (size_t i = 0; i < m_samples.size(); ++i) {
        const Sample &s = m_samples[i];
        if (s.t > nowMs)
            break;
        qint64 end = (i + 1 < m_samples.size()) ? std::min(m_samples[i + 1].t, nowMs) : nowMs;
        end = std::min(end, s.t + hold);
        const qint64 from = std::max(s.t, start);
        if (end > from) {
            energy += double(s.watts) * double(end - from);
            covered += end - from;
        }
    }
    if (covered <= 0)
        return last.t <= nowMs ? last.watts : 0;   // first sample, no time elapsed yet
    return energy / double(covered);
}

int PowerSmoother::averageRounded(qint64 nowMs) const
{
    return int(std::lround(average(nowMs)));
}

} // namespace watt
