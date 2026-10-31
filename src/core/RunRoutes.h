#pragma once

#include "RlvRoute.h"

#include <QString>

namespace watt {

// Built-in routes of the Treadmill Run.
namespace runroutes {

// Path used for the standard route (RunSession::routePath()).
inline QString standardPath() { return QStringLiteral("builtin:artilody-lakeside-10k"); }
inline QString standardName() { return QStringLiteral("Artilody Lakeside 10K"); }

// The standard route: a closed ~10 km loop (start = finish) with eleven
// corners (56–108°, some sharp 90° street corners), gently rolling (±15 m, grades up to about 3 %), generated in code
// (no file). Placed near Farum, Denmark.
RlvRoute standardRoute();

// A route whose start and finish lie within kLoopToleranceM: running past
// the end continues on the next lap.
constexpr double kLoopToleranceM = 60.0;
bool isLoop(const RlvRoute &route);

} // namespace runroutes
} // namespace watt
