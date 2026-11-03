#include "GpxWorkshop.h"

#include "Climb.h"
#include "FreeRideSession.h"
#include "TrainingSession.h"
#include "RlvLibrary.h"
#include "RlvSession.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
QString localPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
double metre(double v)
{
    return std::round(v);
}
} // namespace

GpxWorkshop::GpxWorkshop(RlvLibrary *library, RlvSession *session, QObject *parent)
    : QObject(parent), m_library(library), m_session(session)
{
}

bool GpxWorkshop::open(const QString &pathOrUrl)
{
    const QString file = QFileInfo(localPath(pathOrUrl)).absoluteFilePath();
    RlvRoute route;
    QString error;
    if (!file.endsWith(QLatin1String(".gpx"), Qt::CaseInsensitive) || !route.load(file, &error) || !route.isValid()) {
        setError(error.isEmpty() ? QStringLiteral("%1 is not a GPX route.").arg(QFileInfo(file).fileName()) : error);
        return false;
    }
    m_route = std::move(route);
    m_path = file;
    m_savedMaxGrade = m_route.ownMaxCleanGrade();
    rebuildProfile();
    m_segments = m_saved = m_route.segments();
    setError(QString());
    emit routeChanged();
    emit segmentsChanged();
    return true;
}

void GpxWorkshop::rebuildProfile()
{
    m_profile.clear();
    for (const QPointF &p : m_route.profile(kProfileSamples))
        m_profile.append(QVariantMap { { QStringLiteral("d"), p.x() }, { QStringLiteral("ele"), p.y() } });
    m_steepest = m_route.steepestGrade();
}

void GpxWorkshop::setMaxGrade(double percent)
{
    if (!loaded())
        return;
    // the default is no own limit (the file stays without one)
    const double own = std::abs(percent - RlvRoute::kMaxCleanGrade) < 0.05 ? 0.0 : percent;
    if (own == m_route.ownMaxCleanGrade())
        return;
    m_route.setMaxCleanGrade(own);
    rebuildProfile();
    emit routeChanged();
    emit segmentsChanged();   // dirty; KOM gains / grades
}

bool GpxWorkshop::openRow(int row)
{
    return m_library && open(m_library->pathAt(row));
}

void GpxWorkshop::close()
{
    m_route = RlvRoute();
    m_path.clear();
    m_profile.clear();
    m_segments.clear();
    m_saved.clear();
    emit routeChanged();
    emit segmentsChanged();
}

double GpxWorkshop::elevationAt(double meters) const
{
    return loaded() ? m_route.elevationAtDistance(meters) : 0.0;
}

QString GpxWorkshop::formatDistance(double meters) const
{
    if (meters >= 1000)
        return QStringLiteral("%1 km").arg(meters / 1000.0, 0, 'f', meters >= 10000 ? 1 : 2);
    return QStringLiteral("%1 m").arg(int(std::lround(meters)));
}

QVariantList GpxWorkshop::segments() const
{
    QVariantList out;
    int numbers[2] = { 0, 0 };
    for (const RouteSegment &s : m_segments) {
        const double gain = m_route.elevationAtDistance(s.finish) - m_route.elevationAtDistance(s.start);
        out.append(QVariantMap {
            { QStringLiteral("kind"), s.kindName() },
            { QStringLiteral("start"), s.start },
            { QStringLiteral("finish"), s.finish },
            { QStringLiteral("length"), s.length() },
            { QStringLiteral("name"), s.name },
            { QStringLiteral("label"), s.displayName(++numbers[s.kind]) },
            { QStringLiteral("gain"), gain },
            { QStringLiteral("grade"), s.length() > 0 ? gain / s.length() * 100.0 : 0.0 },
        });
    }
    return out;
}

int GpxWorkshop::sprintCount() const
{
    return int(std::count_if(m_segments.begin(), m_segments.end(), [](const RouteSegment &s) { return s.kind == RouteSegment::Sprint; }));
}

int GpxWorkshop::komCount() const
{
    return int(m_segments.size()) - sprintCount();
}

QString GpxWorkshop::apply(QVector<RouteSegment> next)
{
    if (!loaded())
        return QStringLiteral("Open a GPX route first.");
    QString error;
    if (!segments::validate(next, totalM(), &error))
        return error;
    m_segments = next;
    emit segmentsChanged();
    return QString();
}

QString GpxWorkshop::addSprint(double startM, double lengthM, const QString &name)
{
    RouteSegment s;
    s.kind = RouteSegment::Sprint;
    s.start = metre(std::clamp(startM, 0.0, totalM()));
    s.finish = metre(s.start + lengthM);
    s.name = name.trimmed();
    if (s.finish > totalM())
        return QStringLiteral("The sprint would end after the route (%1 left).").arg(formatDistance(totalM() - s.start));
    QVector<RouteSegment> next = m_segments;
    next.append(s);
    return apply(next);
}

QString GpxWorkshop::addKom(double startM, double finishM, const QString &name)
{
    RouteSegment s;
    s.kind = RouteSegment::Kom;
    s.start = metre(std::clamp(std::min(startM, finishM), 0.0, totalM()));
    s.finish = metre(std::clamp(std::max(startM, finishM), 0.0, totalM()));
    s.name = name.trimmed();
    QVector<RouteSegment> next = m_segments;
    next.append(s);
    return apply(next);
}

QString GpxWorkshop::setSegment(int index, double startM, double finishM, const QString &name)
{
    if (index < 0 || index >= m_segments.size())
        return QStringLiteral("No such segment.");
    QVector<RouteSegment> next = m_segments;
    RouteSegment &s = next[index];
    s.start = metre(std::clamp(std::min(startM, finishM), 0.0, totalM()));
    s.finish = metre(std::clamp(std::max(startM, finishM), 0.0, totalM()));
    s.name = name.trimmed();
    return apply(next);
}

void GpxWorkshop::removeSegment(int index)
{
    if (index < 0 || index >= m_segments.size())
        return;
    m_segments.removeAt(index);
    emit segmentsChanged();
}

QString GpxWorkshop::suggestKom()
{
    if (!loaded())
        return QStringLiteral("Open a GPX route first.");
    const climb::Climb c = climb::longest(m_route);
    if (!c.valid)
        return QStringLiteral("This route has no climb averaging %1 % or more.").arg(climb::kMinGradePercent);
    return addKom(c.start, c.finish);
}

QString GpxWorkshop::save()
{
    if (!loaded())
        return QStringLiteral("Open a GPX route first.");
    QString error;
    auto position = [this](double d) {
        const QPointF p = m_route.positionAtDistance(d);   // (lon, lat)
        return std::make_pair(p.y(), p.x());
    };
    if (!segments::writeToGpx(m_path, m_segments, position, &error, m_route.ownMaxCleanGrade())) {
        setError(error);
        return error;
    }
    m_saved = m_segments;
    m_savedMaxGrade = m_route.ownMaxCleanGrade();
    setError(QString());
    emit segmentsChanged();
    // GPX Ride: the library re-reads the file; a selected route reloads (not mid-ride)
    if (m_library)
        m_library->rescan();
    // (GPX Ride mid-ride on this route: it reads them when the route is selected again)
    if (m_session && !m_session->active() && QFileInfo(m_session->routePath()).absoluteFilePath() == m_path)
        m_session->selectPath(m_path);
    emit saved(m_path);
    return QString();
}

void GpxWorkshop::revert()
{
    m_segments = m_saved;
    if (m_route.ownMaxCleanGrade() != m_savedMaxGrade) {
        m_route.setMaxCleanGrade(m_savedMaxGrade);
        rebuildProfile();
        emit routeChanged();
    }
    emit segmentsChanged();
}

void GpxWorkshop::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit lastErrorChanged();
}

} // namespace watt
