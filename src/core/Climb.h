#pragma once

#include "RlvRoute.h"

#include <algorithm>
#include <vector>

namespace watt::climb {

// The KOM of a GPX Ride: the route's longest climb averaging at least
// kMinGradePercent, found on the smoothed elevation
// (RlvRoute::elevationAtDistance) sampled every kStepM.
//
// The road is first cut into climbs: from a low point up to the top; a climb
// ends where the road drops more than kMaxDropM below its top, or runs on
// for kMaxFlatM without getting kRiseM higher (short dips and flat bits on
// the way up belong to it). In each climb the longest stretch averaging at
// least kMinGradePercent counts, without the flat lead-in / run-out at its
// ends (the first / last kStepM must be that steep too). The longest such
// stretch of the route (at least kMinLengthM) is the KOM.
constexpr double kStepM = 20.0;
constexpr double kMaxDropM = 10.0;
constexpr double kMaxFlatM = 800.0;
constexpr double kRiseM = 2.0;
constexpr double kMinGradePercent = 2.5;
constexpr double kMinLengthM = 100.0;
constexpr double kEdgeM = 10.0;   // the start line is crossed coming from before it

struct Climb
{
    bool valid = false;
    double start = 0, finish = 0;   // metres along the route (app distance)
    double gainM = 0;
    double length() const { return finish - start; }
    double gradePercent() const { return length() > 0 ? gainM / length() * 100.0 : 0.0; }
};

inline Climb longest(const RlvRoute &route)
{
    Climb best;
    const double total = route.totalDistance();
    if (!route.isValid() || total < kMinLengthM + kEdgeM)
        return best;
    std::vector<double> d, e;
    for (double x = 0;; x += kStepM) {
        const double at = std::min(x, total);
        d.push_back(at);
        e.push_back(route.elevationAtDistance(at));
        if (at >= total)
            break;
    }
    const double g = kMinGradePercent / 100.0;
    auto steep = [&](size_t i) { return e[i + 1] - e[i] >= g * (d[i + 1] - d[i]); };
    auto consider = [&](size_t lo, size_t top) {
        // the longest a < b in lo..top with an average grade >= g, i.e.
        // f(b) >= f(a) for f = e - g d: in linear time with the running
        // minimum of f from the left and its maximum from the right (two
        // pointers; argMin / argMax give the pair itself)
        size_t a = 0, b = 0;
        if (top <= lo)
            return;
        const size_t m = top - lo + 1;
        std::vector<double> f(m), leftMin(m), rightMax(m);
        std::vector<size_t> argMin(m), argMax(m);
        for (size_t k = 0; k < m; ++k)
            f[k] = e[lo + k] - g * d[lo + k];
        for (size_t k = 0; k < m; ++k) {
            const bool lower = k == 0 || f[k] < leftMin[k - 1];
            leftMin[k] = lower ? f[k] : leftMin[k - 1];
            argMin[k] = lower ? k : argMin[k - 1];
        }
        for (size_t k = m; k-- > 0;) {
            const bool higher = k + 1 == m || f[k] > rightMax[k + 1];
            rightMax[k] = higher ? f[k] : rightMax[k + 1];
            argMax[k] = higher ? k : argMax[k + 1];
        }
        for (size_t i = 0, j = 0; i < m && j < m;) {
            if (leftMin[i] <= rightMax[j] + 1e-9) {
                const size_t ia = lo + argMin[i], jb = lo + argMax[j];
                if (jb > ia && d[jb] - d[ia] > d[b] - d[a]) {
                    a = ia;
                    b = jb;
                }
                ++j;
            } else {
                ++i;
            }
        }
        while (a < b && !steep(a))   // no flat lead-in / run-out
            ++a;
        while (b > a && !steep(b - 1))
            --b;
        Climb c;
        c.start = std::max(d[a], kEdgeM);
        c.finish = d[b];
        if (b <= a || c.length() < kMinLengthM)
            return;
        c.gainM = route.elevationAtDistance(c.finish) - route.elevationAtDistance(c.start);
        c.valid = c.gradePercent() >= kMinGradePercent - 1e-9;
        if (c.valid && (!best.valid || c.length() > best.length()))
            best = c;
    };
    size_t lo = 0, top = 0, rise = 0;   // rise: where it last got kRiseM higher
    for (size_t i = 1; i < d.size(); ++i) {
        if (e[i] > e[top])
            top = i;
        if (e[i] > e[rise] + kRiseM)
            rise = i;
        if (e[top] - e[i] > kMaxDropM || d[i] - d[rise] > kMaxFlatM) {
            consider(lo, top);
            lo = top = rise = i;
        }
        if (e[i] < e[lo])
            lo = top = rise = i;   // still going down: the climb starts lower
    }
    consider(lo, top);
    return best;
}

} // namespace watt::climb
