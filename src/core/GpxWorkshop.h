#pragma once

#include "RlvRoute.h"
#include "RouteSegments.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>

namespace watt {

class RlvLibrary;
class RlvSession;

// The GPX Workshop (sidebar, under the Workout Builder): open a GPX route,
// see its elevation profile, place sprints, KOMs and gravel sections on it and save them in
// the GPX file (segments::writeToGpx; the track itself is not changed, so the
// route keeps its sprint / KOM times). GPX Ride draws and times exactly these
// (RlvRoute::segments()); a route without any has none.
class GpxWorkshop : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY routeChanged)
    Q_PROPERTY(QString path READ path NOTIFY routeChanged)
    Q_PROPERTY(QString title READ title NOTIFY routeChanged)
    Q_PROPERTY(double totalM READ totalM NOTIFY routeChanged)
    Q_PROPERTY(double minEle READ minEle NOTIFY routeChanged)
    Q_PROPERTY(double maxEle READ maxEle NOTIFY routeChanged)
    Q_PROPERTY(QVariantList profile READ profile NOTIFY routeChanged)   // [{d, ele}]
    // [{kind "sprint"/"kom"/"gravel", start, finish, length, name, label, gain, grade}], sorted by start
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)
    Q_PROPERTY(int sprintCount READ sprintCount NOTIFY segmentsChanged)
    Q_PROPERTY(int komCount READ komCount NOTIFY segmentsChanged)
    Q_PROPERTY(int gravelCount READ gravelCount NOTIFY segmentsChanged)
    // the gravel's extra rolling resistance as gradient (+0.5 %)
    Q_PROPERTY(double gravelGradePercent READ gravelGradePercent CONSTANT)
    Q_PROPERTY(bool dirty READ dirty NOTIFY segmentsChanged)            // unsaved changes
    // elevation cleaning: the gradient limit (%, the route's own or the
    // default), the steepest 40 m grade as recorded and as ridden now
    Q_PROPERTY(double maxGrade READ maxGrade WRITE setMaxGrade NOTIFY routeChanged)
    Q_PROPERTY(double defaultMaxGrade READ defaultMaxGrade CONSTANT)
    Q_PROPERTY(double rawMaxGrade READ rawMaxGrade NOTIFY routeChanged)
    Q_PROPERTY(double steepestGrade READ steepestGrade NOTIFY routeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    static constexpr int kProfileSamples = 600;

    GpxWorkshop(RlvLibrary *library, RlvSession *session, QObject *parent = nullptr);

    bool loaded() const { return m_route.isValid(); }
    QString path() const { return m_path; }
    QString title() const { return loaded() ? m_route.title() : QString(); }
    double totalM() const { return loaded() ? m_route.totalDistance() : 0.0; }
    double minEle() const { return m_route.minElevation(); }
    double maxEle() const { return m_route.maxElevation(); }
    QVariantList profile() const { return m_profile; }
    QVariantList segments() const;
    int sprintCount() const;
    int komCount() const;
    int gravelCount() const;
    double gravelGradePercent() const { return segments::kGravelGradePercent; }
    bool dirty() const { return m_segments != m_saved || m_route.ownMaxCleanGrade() != m_savedMaxGrade; }
    double maxGrade() const { return m_route.maxCleanGrade(); }
    void setMaxGrade(double percent);   // recomputes the profile (default value: no own limit)
    double defaultMaxGrade() const { return RlvRoute::kMaxCleanGrade; }
    double rawMaxGrade() const { return m_route.rawMaxGrade(); }
    double steepestGrade() const { return m_steepest; }
    QString lastError() const { return m_error; }
    const QVector<RouteSegment> &segmentList() const { return m_segments; }

    Q_INVOKABLE bool open(const QString &pathOrUrl);   // a .gpx file (file:// URLs ok)
    Q_INVOKABLE bool openRow(int row);                 // row in GpxRoutes
    Q_INVOKABLE void close();
    Q_INVOKABLE double elevationAt(double meters) const;
    Q_INVOKABLE QString formatDistance(double meters) const;   // "1.25 km" / "850 m"
    // Each returns "" when done, else why not (nothing is changed then).
    Q_INVOKABLE QString addSprint(double startM, double lengthM, const QString &name = QString());
    Q_INVOKABLE QString addKom(double startM, double finishM, const QString &name = QString());
    // a gravel section from start to finish, placed like a KOM (not timed:
    // the road is gravel, extra rolling resistance outside ERG / workouts)
    Q_INVOKABLE QString addGravel(double startM, double finishM, const QString &name = QString());
    Q_INVOKABLE QString setSegment(int index, double startM, double finishM, const QString &name);
    Q_INVOKABLE void removeSegment(int index);
    // a KOM on the longest climb averaging >= 2.5 % (climb::longest) that is still free
    Q_INVOKABLE QString suggestKom();
    Q_INVOKABLE QString save();     // into the GPX file; GPX Ride picks it up
    Q_INVOKABLE void revert();      // back to the file's segments

signals:
    void routeChanged();
    void segmentsChanged();
    void lastErrorChanged();
    void saved(const QString &path);

private:
    QString apply(QVector<RouteSegment> next);   // validate, then take
    void setError(const QString &error);

    QPointer<RlvLibrary> m_library;
    QPointer<RlvSession> m_session;
    RlvRoute m_route;
    QString m_path;
    QVariantList m_profile;
    QVector<RouteSegment> m_segments, m_saved;
    double m_savedMaxGrade = 0;
    double m_steepest = 0;
    void rebuildProfile();
    QString m_error;
};

} // namespace watt
