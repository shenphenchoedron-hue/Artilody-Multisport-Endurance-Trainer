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
//
// A gravel section (kind="gravel") is not timed: a stretch of gravel road.
// GPX Ride draws the road there as gravel and, outside ERG (no ERG mode, no
// workout), the trainer gets the gravel's extra rolling resistance
// (RlvSession::gravelGradePercent) and a Tacx NEO its road feel.
// Gravel sections may overlap sprints and KOMs, not each other.
struct RouteSegment
{
    enum Kind { Sprint, Kom, Gravel };
    Kind kind = Sprint;
    double start = 0, finish = 0;   // m along the route
    QString name;                   // optional
    double length() const { return finish - start; }
    bool timed() const { return kind != Gravel; }
    QString kindName() const
    {
        return kind == Kom ? QStringLiteral("kom") : kind == Gravel ? QStringLiteral("gravel") : QStringLiteral("sprint");
    }
    static Kind kindFromName(QStringView name)
    {
        return name == QLatin1String("kom") ? Kom : name == QLatin1String("gravel") ? Gravel : Sprint;
    }
    // "Sprint 2" / "KOM 1" / "Gravel 3" when unnamed (number: 1-based among its kind)
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
// Fine, packed gravel rolls with Crr ~0.009 against ~0.004 on good asphalt:
// kGravelExtraCrr more. Rolling resistance and climbing both scale with m·g,
// so it is ridden as kGravelExtraCrr·100 % more gradient (+0.5 %): the extra
// watts follow from rider weight and speed, ΔCrr·m·g·v/η (77 kg at 27 km/h:
// ~29 W). (Rough / loose gravel would be 0.01-0.02 more.)
constexpr double kGravelExtraCrr = 0.005;
constexpr double kGravelGradePercent = kGravelExtraCrr * 100.0;
// The extra resistance builds up over the first kGravelRampM of a section and
// fades over its last kGravelRampM (no step in watts at the edge; ~4 s at
// 27 km/h). The road feel starts and stops at the edge itself.
constexpr double kGravelRampM = 30.0;

// Sorted by start; true when every segment lies inside [0, total], is at
// least kMinLengthM long and none overlaps another of its class (sprints and
// KOMs among themselves, gravel sections among themselves; error: why not).
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
