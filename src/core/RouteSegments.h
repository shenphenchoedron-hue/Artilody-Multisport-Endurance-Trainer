#pragma once

#include <QString>
#include <QVector>

#include <functional>
#include <utility>

namespace watt {

// A timed stretch of a GPX route that the rider placed in the GPX Workshop:
// a sprint or a KOM (climb), from start to finish metres along the route
// (RlvRoute distance). Stored in the GPX file itself, as a GPX extension at
// the end of the file (other apps ignore it):
//
//   <extensions>
//     <artilody:segments xmlns:artilody="https://github.com/shenphenchoedron-hue/Artilody-Multisport-Endurance-Trainer/gpx/1">
//       <artilody:segment kind="sprint" start="5200.0" finish="5400.0" name="Town sign"
//                         startLat=".." startLon=".." finishLat=".." finishLon=".."/>
//     </artilody:segments>
//   </extensions>
//
// The positions are informative (other tools); the app uses the distances.
struct RouteSegment
{
    enum Kind { Sprint, Kom };
    Kind kind = Sprint;
    double start = 0, finish = 0;   // m along the route
    QString name;                   // optional
    double length() const { return finish - start; }
    QString kindName() const { return kind == Kom ? QStringLiteral("kom") : QStringLiteral("sprint"); }
    // "Sprint 2" / "KOM 1" when unnamed (number: 1-based among its kind)
    QString displayName(int number) const;
    bool operator==(const RouteSegment &o) const
    {
        return kind == o.kind && start == o.start && finish == o.finish && name == o.name;
    }
};

namespace segments {
inline constexpr char kNamespace[] = "https://github.com/shenphenchoedron-hue/Artilody-Multisport-Endurance-Trainer/gpx/1";
constexpr double kMinLengthM = 50.0;
constexpr double kDefaultSprintM = 200.0;

// Sorted by start; true when every segment lies inside [0, total], is at
// least kMinLengthM long and none overlaps another (error: why not).
bool validate(QVector<RouteSegment> &list, double totalM, QString *error = nullptr);
// Writes the segments into the GPX file at path (replacing segments written
// before; an empty list removes them). The track itself is not touched.
// positionAt(d) -> (lat, lon) for the informative positions (may be empty).
// maxGrade > 0: the route's own gradient limit (RlvRoute::maxCleanGrade),
// written as <artilody:segments maxGrade="..">.
bool writeToGpx(const QString &path, const QVector<RouteSegment> &list,
                const std::function<std::pair<double, double>(double)> &positionAt, QString *error = nullptr,
                double maxGrade = 0);
} // namespace segments

} // namespace watt
