#pragma once
#include <QVector3D>

namespace route3d {
// A vertex of the 3D view's figures (rider, bike, runner) in world space.
struct GhostVertex {QVector3D position,normal,color;};
} // namespace route3d
