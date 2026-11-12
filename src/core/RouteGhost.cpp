#include "RouteGhost.h"

#include "Database.h"
#include "DiagLog.h"
#include "TimeFormat.h"

#include <QLocale>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
bool isPreset(const QString &c)
{
    return c == QLatin1String("off") || c == QLatin1String("best") || c == QLatin1String("latest");
}

qint64 idOf(const QString &choice)
{
    if (!choice.startsWith(QLatin1String("id:")))
        return -1;
    bool ok = false;
    const qint64 id = choice.mid(3).toLongLong(&ok);
    return ok ? id : -1;
}

double avgSpeed(const RouteTrace &t)
{
    return t.durationSec > 0 ? t.distanceM / t.durationSec : 0.0;
}
} // namespace

RouteGhost::RouteGhost(Database *db, const QString &sport, QObject *parent)
    : RouteRival(parent)
    , m_db(db)
    , m_sport(sport)
{
    const QString stored = m_db ? m_db->setting(settingKey(), QStringLiteral("best")).toString() : QStringLiteral("best");
    m_choice = isPreset(stored) ? stored : QStringLiteral("best");
    rebuildChoices();
}

// --- choices -------------------------------------------------------------------------

void RouteGhost::setContext(const Context &c)
{
    const bool same = c.profileId == m_context.profileId && c.routeKey == m_context.routeKey
                      && std::abs(c.startM - m_context.startM) < 0.01 && c.loop == m_context.loop;
    m_context.routeName = c.routeName;
    if (same)
        return;
    m_context = c;
    setRoute(!m_context.routeKey.isEmpty(), m_context.loop);
    if (!isPreset(m_choice)) {   // a chosen ride belongs to the route it was ridden on
        const QString stored = m_db ? m_db->setting(settingKey(), QStringLiteral("best")).toString() : QString();
        m_choice = isPreset(stored) ? stored : QStringLiteral("best");
    }
    loadCandidates();
    rebuildChoices();
    emit choicesChanged();
    if (!m_recording)
        resolve();
    if (!m_context.routeKey.isEmpty())
        DiagLog::write("GHOST", QStringLiteral("%1 route %2 (\"%3\") from %4 m%5: %6 earlier rides, ghost: %7")
                                    .arg(m_sport, m_context.routeKey, m_context.routeName)
                                    .arg(m_context.startM, 0, 'f', 0)
                                    .arg(m_context.loop ? QStringLiteral(" looped") : QString())
                                    .arg(m_candidates.size())
                                    .arg(available() ? ghostLabel() : QStringLiteral("none")));
}

void RouteGhost::refreshChoices()
{
    loadCandidates();
    rebuildChoices();
    emit choicesChanged();
}

void RouteGhost::loadCandidates()
{
    m_candidates.clear();
    m_others.clear();
    if (!m_db || m_context.routeKey.isEmpty() || m_context.profileId < 0)
        return;
    m_candidates = m_db->routeTraces(m_context.profileId, m_sport, m_context.routeKey, m_context.startM, m_context.loop);
    // the other riders' rides on the route (this station's and, with a server, all of them)
    m_others = m_db->routeTracesOfOthers(m_context.profileId, m_sport, m_context.routeKey, m_context.startM, m_context.loop);
}

const RouteTrace *RouteGhost::findTrace(qint64 id) const
{
    for (const QVector<RouteTrace> *list : { &m_candidates, &m_others })
        for (const RouteTrace &t : *list)
            if (t.id == id)
                return &t;
    return nullptr;
}

QString RouteGhost::describe(const RouteTrace &t) const
{
    const QLocale c = QLocale::c();
    const QDateTime when = t.createdAt.toLocalTime();
    return QStringLiteral("%1 · %2 · %3 km")
        .arg(c.toString(when, QStringLiteral("d MMM HH:mm")), formatClock(int(std::lround(t.durationSec))),
             QString::number(t.distanceM / 1000.0, 'f', 1));
}

qint64 RouteGhost::bestId() const
{
    // a point-to-point route: the fastest ride to its end; a loop (or no ride
    // reached the end): the highest average speed
    const RouteTrace *best = nullptr;
    if (!m_context.loop) {
        for (const RouteTrace &t : m_candidates)
            if (t.completed && (!best || t.durationSec < best->durationSec))
                best = &t;
    }
    if (!best) {
        for (const RouteTrace &t : m_candidates)
            if (t.durationSec >= kMinTraceSec && (!best || avgSpeed(t) > avgSpeed(*best)))
                best = &t;
    }
    return best ? best->id : -1;
}

void RouteGhost::rebuildChoices()
{
    m_choices.clear();
    auto add = [this](const QString &value, const QString &label) {
        m_choices.append(QVariantMap { { QStringLiteral("value"), value }, { QStringLiteral("label"), label } });
    };
    auto find = [this](qint64 id) -> const RouteTrace * {
        for (const RouteTrace &t : m_candidates)
            if (t.id == id)
                return &t;
        return nullptr;
    };
    add(QStringLiteral("off"), QStringLiteral("No ghost"));
    const RouteTrace *best = find(bestId());
    const RouteTrace *latest = m_candidates.isEmpty() ? nullptr : &m_candidates.first();
    const auto brief = [](const RouteTrace &t) {
        return QStringLiteral("%1 · %2").arg(formatClock(int(std::lround(t.durationSec))),
                                             QLocale::c().toString(t.createdAt.toLocalTime(), QStringLiteral("d MMM")));
    };
    add(QStringLiteral("best"), best ? QStringLiteral("Best ride (%1)").arg(brief(*best)) : QStringLiteral("Best ride"));
    add(QStringLiteral("latest"), latest ? QStringLiteral("Latest ride (%1)").arg(brief(*latest)) : QStringLiteral("Latest ride"));
    for (const RouteTrace &t : m_candidates)
        add(QStringLiteral("id:%1").arg(t.id), describe(t));
    for (const RouteTrace &t : m_others)   // (after one's own: "Anne · 12 Nov 18:00 · 32:10 · 12.0 km")
        add(QStringLiteral("id:%1").arg(t.id), QStringLiteral("%1 · %2").arg(t.riderName, describe(t)));
}

int RouteGhost::choiceIndex() const
{
    for (int i = 0; i < m_choices.size(); ++i)
        if (m_choices.at(i).toMap().value(QStringLiteral("value")).toString() == m_choice)
            return i;
    return 0;
}

void RouteGhost::setChoice(const QString &choice)
{
    if (m_recording || choice == m_choice)
        return;
    if (!isPreset(choice)) {
        if (!findTrace(idOf(choice)))
            return;
    } else if (m_db) {
        m_db->setSetting(settingKey(), choice);
    }
    m_choice = choice;
    emit choicesChanged();
    resolve();
}

void RouteGhost::resolve()
{
    qint64 id = -1;
    m_traceKind.clear();
    if (m_choice == QLatin1String("best")) {
        id = bestId();
        m_traceKind = QStringLiteral("Best");
    } else if (m_choice == QLatin1String("latest")) {
        id = m_candidates.isEmpty() ? -1 : m_candidates.first().id;
        m_traceKind = QStringLiteral("Latest");
    } else if (m_choice != QLatin1String("off")) {
        id = idOf(m_choice);
        if (const RouteTrace *t = findTrace(id); t && !t->riderName.isEmpty())
            m_traceKind = t->riderName;   // (another rider's: "Anne · 32:10 · 12 Nov")
    }
    // (the same ride again - start, seek, reset: not loaded and parsed again)
    RouteTrace t;
    if (id >= 0 && m_db) {
        if (id == m_trace.id && !m_trace.trace.isEmpty())
            t = m_trace;
        else if (auto loaded = m_db->routeTrace(id))
            t = *loaded;
    }
    load(t);
}

QString RouteGhost::ghostLabel() const
{
    if (!available())
        return QString();
    const QString when = QLocale::c().toString(m_trace.createdAt.toLocalTime(), QStringLiteral("d MMM"));
    const QString time = formatClock(int(std::lround(m_trace.durationSec)));
    return m_traceKind.isEmpty() ? QStringLiteral("%1 · %2").arg(time, when)
                                 : QStringLiteral("%1 · %2 · %3").arg(m_traceKind, time, when);
}

// --- the ride ----------------------------------------------------------------------------

void RouteGhost::begin()
{
    m_recording = false;   // (resolve() first: "best" may be another ride now)
    loadCandidates();
    rebuildChoices();
    emit choicesChanged();
    clearTimes();
    resolve();
    m_recording = true;
    m_recorded.clear();
    m_recorded.append({ 0.0, 0.0 });
}

void RouteGhost::advanced()
{
    if (!m_recording)
        return;
    const TracePoint &last = m_recorded.last();
    if (m_t - last.t >= 1.0 - 1e-9)   // one point per second
        m_recorded.append({ m_t, std::max(last.d, m_youM) });
}

qint64 RouteGhost::saveRecording(qint64 rideId, bool completed)
{
    if (!m_recording)
        return -1;
    m_recording = false;
    if (!m_recorded.isEmpty() && m_t > m_recorded.last().t)
        m_recorded.append({ m_t, std::max(m_recorded.last().d, m_youM) });
    const TracePoint end = m_recorded.isEmpty() ? TracePoint() : m_recorded.last();
    qint64 id = -1;
    if (m_db && rideId >= 0 && end.t >= kMinTraceSec && !m_context.routeKey.isEmpty() && m_context.profileId >= 0) {
        RouteTrace t;
        t.profileId = m_context.profileId;
        t.rideId = rideId;
        t.sport = m_sport;
        t.routeKey = m_context.routeKey;
        t.routeName = m_context.routeName;
        t.startM = m_context.startM;
        t.loop = m_context.loop;
        t.completed = completed;
        t.durationSec = end.t;
        t.distanceM = end.d;
        t.createdAt = QDateTime::currentDateTime();
        t.trace = m_recorded;
        id = m_db->saveRouteTrace(t);
    }
    m_recorded.clear();
    refreshChoices();   // (the ghost raced stays until the next start)
    return id;
}

void RouteGhost::discardRecording()
{
    m_recording = false;
    m_recorded.clear();
}

void RouteGhost::reset()
{
    m_recording = false;
    m_recorded.clear();
    clearTimes();
    resolve();   // ("best" may be the ride just saved)
}

} // namespace watt
