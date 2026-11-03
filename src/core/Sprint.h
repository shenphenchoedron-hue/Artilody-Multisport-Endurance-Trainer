#pragma once

#include "RlvRoute.h"

#include <QCryptographicHash>
#include <QString>
#include <QtEndian>
#include <cmath>

namespace watt::sprint {

// Helpers for the timed segments of a GPX Ride (sprints and KOMs, placed in
// the GPX Workshop: RouteSegment).
constexpr double kAnnounceM = 200.0;   // "SPRINT IN 200 m" before a sprint's start line

// When a line at `line` metres was crossed in a step from (d0, t0) to (d1, t1),
// interpolated (ms); NaN when the step does not cross it (d0 < line <= d1).
inline double crossingMs(double d0, double d1, double t0, double t1, double line)
{
    if (!(d0 < line && line <= d1))
        return std::nan("");
    return t0 + (t1 - t0) * (line - d0) / (d1 - d0);
}

// The route's identity for its sprint / KOM times: a fingerprint of the GPX
// track's points (position to ~1 cm, elevation to 1 cm). The same track
// stays the same route when the file is renamed or moved; an edited track
// is a new route (its sprint lies elsewhere).
inline QString routeKey(const RlvRoute &route)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    auto add = [&](qint64 v) {
        const qint64 le = qToLittleEndian(v);
        h.addData(QByteArrayView(reinterpret_cast<const char *>(&le), sizeof le));
    };
    add(route.points().size());
    for (const RlvPoint &p : route.points()) {
        add(std::llround(p.lat * 1e7));
        add(std::llround(p.lon * 1e7));
        add(std::llround(p.rawElevation * 100.0));
    }
    return QString::fromLatin1(h.result().toHex());
}

} // namespace watt::sprint
