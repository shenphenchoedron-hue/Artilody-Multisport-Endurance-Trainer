// WorkoutPlanner: training plans - weeks of the calendar as one shareable
// file, imported onto a start date by another rider.
//
// .awplan (UTF-8 JSON, version 1):
//   { "format": "artilody-watts-trainer/plan", "version": 1, "exportedAt", "app",
//     "name", "description", "weeks", "days",
//     "workouts": [ { "key": "w1", <.awt workout object: name, category, focus,
//                     description, sport, intervals> } ],
//     "gpx": [ { "key": "g1", "fileName": "Route.gpx", "title", "contentBase64": "<the file's bytes>" } ],
//     "entries": [ { "day": 0.., "position", "kind": "workout" | "freeride" | "rlv"
//                    | "gpx" | "row" | "run", "notes",
//                    "workout": "w1"                       (kind workout),
//                    "durationMin"                         (free kinds),
//                    "options": { mode, ergWatts, difficulty, workoutErg,
//                                 "workout": "w1" }       (free ride / routes),
//                    "gpx": "g1"                           (kind gpx),
//                    "video": { "name", "fileName", "distanceM" }   (kind rlv) } ] }
// Videos are not included: an imported Video Ride is linked to a video of the
// rider's library with the same route name, else it waits for one ("choose video").
// Workouts already in the library (same name, sport and intervals) are reused;
// GPX files go into the GPX library folder (an identical file is reused).
#include "WorkoutPlanner.h"

#include "Database.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvRoute.h"
#include "Workout.h"
#include "WorkoutIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
constexpr const char *kPlanFormat = "artilody-watts-trainer/plan";
constexpr int kPlanVersion = 1;
constexpr int kMaxWeeks = 104;
constexpr qint64 kMaxPlanBytes = 64 * 1024 * 1024;
constexpr qint64 kMaxGpxBytes = 16 * 1024 * 1024;
constexpr int kMaxEntries = 2000;

QString localFile(const QString &fileOrUrl)
{
    return RlvLibrary::toLocalPath(fileOrUrl);
}

QJsonObject optionsJson(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

QString baseName(const QString &name)
{
    static const QRegularExpression suffix(QStringLiteral("\\s*\\(imported(?:\\s+\\d+)?\\)$"));
    QString n = name;
    n.remove(suffix);
    return n.trimmed();
}

// a workout of the .awt format -> sanitised Workout (std::nullopt when invalid)
std::optional<Workout> parseWorkout(const QJsonObject &o)
{
    QJsonObject single = o;
    single.remove(QStringLiteral("key"));
    QJsonObject doc { { QStringLiteral("format"), QLatin1String(workoutio::kFormatId) },
                      { QStringLiteral("version"), workoutio::kFormatVersion },
                      { QStringLiteral("workouts"), QJsonArray { single } } };
    const auto r = workoutio::parseAwt(QJsonDocument(doc).toJson(QJsonDocument::Compact));
    if (!r.ok())
        return std::nullopt;
    return r.workouts.first();
}

QString safeGpxName(QString name)
{
    name = QFileInfo(name).fileName();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (!name.endsWith(QLatin1String(".gpx"), Qt::CaseInsensitive))
        name += QStringLiteral(".gpx");
    return name.isEmpty() || name == QLatin1String(".gpx") ? QStringLiteral("Route.gpx") : name;
}

QString normal(const QString &s)
{
    return s.trimmed().toLower();
}
} // namespace

QString WorkoutPlanner::suggestedPlanUrl(const QString &name) const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QString file = workoutio::safeFileName(name.trimmed().isEmpty() ? QStringLiteral("Training plan") : name.trimmed());
    return QUrl::fromLocalFile(QDir(dir).filePath(file + QStringLiteral(".awplan"))).toString();
}

bool WorkoutPlanner::exportPlan(const QVariant &firstDay, int weeks, const QString &name, const QString &description,
                                const QString &fileOrUrl)
{
    QDate from = toDate(firstDay);
    if (!from.isValid() || weeks < 1 || weeks > kMaxWeeks) {
        setError(QStringLiteral("Choose a first week and 1–%1 weeks").arg(kMaxWeeks));
        return false;
    }
    from = from.addDays(1 - from.dayOfWeek());   // the Monday of that week
    const QDate to = from.addDays(weeks * 7 - 1);
    QString path = localFile(fileOrUrl);
    if (path.isEmpty()) {
        setError(QStringLiteral("No file chosen"));
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".awplan");
    const QVector<PlannedWorkout> entries = m_db->plannedWorkouts(m_profiles->profileId(), from, to);
    if (entries.isEmpty()) {
        setError(QStringLiteral("Nothing is planned in these weeks"));
        return false;
    }

    QJsonArray workouts, gpxFiles, out;
    QHash<qint64, QString> workoutKeys;
    QHash<QString, QString> gpxKeys;   // route file -> key
    auto workoutKey = [&](qint64 id) -> QString {
        if (id < 0)
            return {};
        if (workoutKeys.contains(id))
            return workoutKeys.value(id);
        const auto w = m_db->workout(id);
        if (!w)
            return {};
        const QJsonObject doc = QJsonDocument::fromJson(workoutio::toAwt({ *w }, QCoreApplication::applicationVersion())).object();
        QJsonObject o = doc.value(QStringLiteral("workouts")).toArray().first().toObject();
        const QString key = QStringLiteral("w%1").arg(workouts.size() + 1);
        o.insert(QStringLiteral("key"), key);
        workouts.append(o);
        workoutKeys.insert(id, key);
        return key;
    };
    QStringList problems;
    for (const PlannedWorkout &e : entries) {
        QJsonObject j { { QStringLiteral("day"), int(from.daysTo(e.date)) },
                        { QStringLiteral("position"), e.position },
                        { QStringLiteral("kind"), e.kind } };
        if (!e.notes.isEmpty())
            j.insert(QStringLiteral("notes"), e.notes);
        QJsonObject opts = optionsJson(e.options);
        if (opts.contains(QStringLiteral("workoutId"))) {
            const QString key = workoutKey(opts.value(QStringLiteral("workoutId")).toInteger(-1));
            opts.remove(QStringLiteral("workoutId"));
            if (!key.isEmpty())
                opts.insert(QStringLiteral("workout"), key);
        }
        if (e.kind == plankind::workout()) {
            const QString key = workoutKey(e.workoutId);
            if (key.isEmpty())
                continue;
            j.insert(QStringLiteral("workout"), key);
        } else if (plankind::isFree(e.kind)) {
            j.insert(QStringLiteral("durationMin"), int(std::lround(e.plannedDurationSec / 60.0)));
            if (!opts.isEmpty())
                j.insert(QStringLiteral("options"), opts);
        } else if (e.kind == plankind::gpx()) {
            const QString file = RlvLibrary::toLocalPath(e.routePath);
            QString key = gpxKeys.value(file);
            if (key.isEmpty()) {
                QFile f(file);
                if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxGpxBytes) {
                    problems << QFileInfo(file).fileName();
                    continue;
                }
                key = QStringLiteral("g%1").arg(gpxFiles.size() + 1);
                gpxFiles.append(QJsonObject { { QStringLiteral("key"), key },
                                              { QStringLiteral("fileName"), QFileInfo(file).fileName() },
                                              { QStringLiteral("title"), e.routeName },
                                              { QStringLiteral("contentBase64"), QString::fromLatin1(f.readAll().toBase64()) } });
                gpxKeys.insert(file, key);
            }
            j.insert(QStringLiteral("gpx"), key);
            j.insert(QStringLiteral("options"), opts);
        } else if (e.kind == plankind::video()) {
            const RouteInfo route = e.routePath.isEmpty() ? RouteInfo() : lookupRoute(e.routePath, false, m_videos.data());
            const QFileInfo fi(RlvLibrary::toLocalPath(e.routePath));
            j.insert(QStringLiteral("video"),
                     QJsonObject { { QStringLiteral("name"), route.inLibrary ? route.name : e.routeName },
                                   { QStringLiteral("fileName"), e.routePath.isEmpty() ? QString()
                                                                 : fi.isDir() ? fi.fileName() : fi.dir().dirName() },
                                   { QStringLiteral("distanceM"), std::round(route.distanceM) } });
            j.insert(QStringLiteral("options"), opts);
        }
        out.append(j);
    }
    if (!problems.isEmpty()) {
        setError(QStringLiteral("Cannot read the GPX file(s): %1").arg(problems.join(QStringLiteral(", "))));
        return false;
    }
    const QJsonObject plan {
        { QStringLiteral("format"), QLatin1String(kPlanFormat) },
        { QStringLiteral("version"), kPlanVersion },
        { QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) },
        { QStringLiteral("app"), QStringLiteral("Artilody Multisport Endurance Trainer %1").arg(QCoreApplication::applicationVersion()) },
        { QStringLiteral("name"), name.trimmed().isEmpty() ? QStringLiteral("Training plan") : name.trimmed() },
        { QStringLiteral("description"), description.trimmed() },
        { QStringLiteral("weeks"), weeks },
        { QStringLiteral("days"), weeks * 7 },
        { QStringLiteral("workouts"), workouts },
        { QStringLiteral("gpx"), gpxFiles },
        { QStringLiteral("entries"), out },
    };
    QString error;
    if (!workoutio::writeFile(path, QJsonDocument(plan).toJson(QJsonDocument::Indented), &error)) {
        setError(error.isEmpty() ? QStringLiteral("Cannot write %1").arg(path) : error);
        return false;
    }
    m_lastMessage = QStringLiteral("%1 entries, %2 workouts and %3 GPX routes exported")
                        .arg(out.size()).arg(workouts.size()).arg(gpxFiles.size());
    setError(QString());
    return true;
}

namespace {
struct PlanDoc
{
    QJsonObject root;
    QString error;
};

PlanDoc readPlan(const QString &path)
{
    PlanDoc d;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        d.error = QStringLiteral("Cannot open %1").arg(QFileInfo(path).fileName());
        return d;
    }
    if (f.size() > kMaxPlanBytes) {
        d.error = QStringLiteral("The plan file is too large");
        return d;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        d.error = QStringLiteral("Not a training plan file");
        return d;
    }
    d.root = doc.object();
    if (d.root.value(QStringLiteral("format")).toString() != QLatin1String(kPlanFormat)) {
        d.error = QStringLiteral("Not a training plan file");
        d.root = {};
    } else if (d.root.value(QStringLiteral("version")).toInt() > kPlanVersion) {
        d.error = QStringLiteral("This plan was made by a newer version of the app");
        d.root = {};
    }
    return d;
}

QString entryLabel(const QJsonObject &e, const QHash<QString, QString> &workoutNames, const QHash<QString, QString> &gpxNames)
{
    const QString kind = e.value(QStringLiteral("kind")).toString();
    const int minutes = e.value(QStringLiteral("durationMin")).toInt();
    const QString dur = minutes > 0 ? QStringLiteral(" · %1 min").arg(minutes) : QString();
    const QString attached = workoutNames.value(e.value(QStringLiteral("options")).toObject().value(QStringLiteral("workout")).toString());
    const QString plus = attached.isEmpty() ? QString() : QStringLiteral(" + ") + attached;
    if (kind == plankind::workout())
        return workoutNames.value(e.value(QStringLiteral("workout")).toString(), QStringLiteral("Workout"));
    if (kind == plankind::freeRide())
        return QStringLiteral("Free Ride") + dur;
    if (kind == plankind::freeRow())
        return QStringLiteral("Free Row") + dur;
    if (kind == plankind::freeRun())
        return QStringLiteral("Free Run") + dur;
    if (kind == plankind::gpx())
        return QStringLiteral("GPX Ride: ") + gpxNames.value(e.value(QStringLiteral("gpx")).toString()) + plus;
    if (kind == plankind::video())
        return QStringLiteral("Video Ride: ") + e.value(QStringLiteral("video")).toObject().value(QStringLiteral("name")).toString() + plus;
    return kind;
}
} // namespace

QVariantMap WorkoutPlanner::planInfo(const QString &fileOrUrl) const
{
    QVariantMap m;
    const PlanDoc d = readPlan(localFile(fileOrUrl));
    if (!d.error.isEmpty()) {
        m.insert(QStringLiteral("ok"), false);
        m.insert(QStringLiteral("error"), d.error);
        return m;
    }
    QHash<QString, QString> workoutNames, gpxNames;
    for (const QJsonValue v : d.root.value(QStringLiteral("workouts")).toArray())
        workoutNames.insert(v.toObject().value(QStringLiteral("key")).toString(), v.toObject().value(QStringLiteral("name")).toString());
    for (const QJsonValue v : d.root.value(QStringLiteral("gpx")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString t = o.value(QStringLiteral("title")).toString();
        gpxNames.insert(o.value(QStringLiteral("key")).toString(), t.isEmpty() ? QFileInfo(o.value(QStringLiteral("fileName")).toString()).completeBaseName() : t);
    }
    const QJsonArray entries = d.root.value(QStringLiteral("entries")).toArray();
    QVariantList items;
    int videos = 0, free = 0, maxDay = 0;
    for (const QJsonValue v : entries) {
        const QJsonObject e = v.toObject();
        const QString kind = e.value(QStringLiteral("kind")).toString();
        if (kind == plankind::video())
            ++videos;
        if (plankind::isFree(kind))
            ++free;
        const int day = e.value(QStringLiteral("day")).toInt();
        maxDay = std::max(maxDay, day);
        items.append(QVariantMap { { QStringLiteral("day"), day }, { QStringLiteral("kind"), kind },
                                   { QStringLiteral("label"), entryLabel(e, workoutNames, gpxNames) } });
    }
    m.insert(QStringLiteral("ok"), true);
    m.insert(QStringLiteral("error"), QString());
    m.insert(QStringLiteral("name"), d.root.value(QStringLiteral("name")).toString());
    m.insert(QStringLiteral("description"), d.root.value(QStringLiteral("description")).toString());
    m.insert(QStringLiteral("weeks"), d.root.value(QStringLiteral("weeks")).toInt(maxDay / 7 + 1));
    m.insert(QStringLiteral("days"), d.root.value(QStringLiteral("days")).toInt(maxDay + 1));
    m.insert(QStringLiteral("entries"), int(entries.size()));
    m.insert(QStringLiteral("workouts"), int(d.root.value(QStringLiteral("workouts")).toArray().size()));
    m.insert(QStringLiteral("gpxRoutes"), int(d.root.value(QStringLiteral("gpx")).toArray().size()));
    m.insert(QStringLiteral("videos"), videos);
    m.insert(QStringLiteral("freeSessions"), free);
    m.insert(QStringLiteral("items"), items);
    return m;
}

int WorkoutPlanner::importPlan(const QString &fileOrUrl, const QVariant &startDay)
{
    m_lastMessage.clear();
    const QDate start = toDate(startDay);
    if (!start.isValid() || m_profiles->profileId() < 0) {
        setError(QStringLiteral("Invalid start date"));
        return -1;
    }
    const PlanDoc d = readPlan(localFile(fileOrUrl));
    if (!d.error.isEmpty()) {
        setError(d.error);
        return -1;
    }
    const QJsonArray entries = d.root.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty() || entries.size() > kMaxEntries) {
        setError(entries.isEmpty() ? QStringLiteral("The plan is empty") : QStringLiteral("The plan has too many entries"));
        return -1;
    }

    // only what the entries use (a plan cannot fill the library with extras)
    QSet<QString> usedWorkouts, usedGpx;
    for (const QJsonValue v : entries) {
        const QJsonObject e = v.toObject();
        const QString kind = e.value(QStringLiteral("kind")).toString();
        if (kind == plankind::workout())
            usedWorkouts.insert(e.value(QStringLiteral("workout")).toString());
        if (plankind::isRoute(kind))
            usedWorkouts.insert(e.value(QStringLiteral("options")).toObject().value(QStringLiteral("workout")).toString());
        if (kind == plankind::gpx())
            usedGpx.insert(e.value(QStringLiteral("gpx")).toString());
    }
    usedWorkouts.remove(QString());
    usedGpx.remove(QString());

    // 1. workouts: reuse identical ones (same base name, sport, intervals), add the rest
    QVector<Workout> existing = m_db->workouts();
    auto identity = [](const Workout &w) { return baseName(w.name).toLower() + QLatin1Char('\x1f') + sport::normalize(w.sport); };
    QMultiHash<QString, int> byIdentity;   // -> index in existing
    QSet<QString> names;
    for (int i = 0; i < existing.size(); ++i) {
        byIdentity.insert(identity(existing.at(i)), i);
        names.insert(existing.at(i).name.toLower());
    }
    QHash<QString, qint64> workoutIds;
    int added = 0, reused = 0;
    for (const QJsonValue v : d.root.value(QStringLiteral("workouts")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        if (!usedWorkouts.contains(key) || workoutIds.contains(key))
            continue;
        auto w = parseWorkout(o);
        if (!w)
            continue;
        qint64 id = -1;
        for (auto it = byIdentity.constFind(identity(*w)); it != byIdentity.cend() && it.key() == identity(*w); ++it)
            if (existing.at(it.value()).intervals == w->intervals) {
                id = existing.at(it.value()).id;
                break;
            }
        if (id >= 0) {
            ++reused;
        } else {
            QString name = w->name;
            if (names.contains(name.toLower())) {
                QString candidate = name + QStringLiteral(" (imported)");
                for (int n = 2; names.contains(candidate.toLower()); ++n)
                    candidate = name + QStringLiteral(" (imported %1)").arg(n);
                name = candidate;
            }
            w->name = name;
            id = m_db->saveWorkout(*w);
            if (id < 0)
                continue;
            w->id = id;
            existing.append(*w);
            byIdentity.insert(identity(*w), int(existing.size()) - 1);
            names.insert(name.toLower());
            ++added;
        }
        workoutIds.insert(key, id);
    }
    if (added > 0)
        emit workoutsImported();

    // 2. GPX files into the GPX library folder
    QHash<QString, QString> gpxPaths;
    int gpxAdded = 0, gpxReused = 0;
    const QJsonArray gpxFiles = d.root.value(QStringLiteral("gpx")).toArray();
    if (!gpxFiles.isEmpty()) {
        const QString folder = m_gpxRoutes ? m_gpxRoutes->ensureLibraryFolder()
                                           : QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                                 .filePath(QStringLiteral("Artilody GPX Routes"));
        QDir().mkpath(folder);
        for (const QJsonValue v : gpxFiles) {
            const QJsonObject o = v.toObject();
            // the file's bytes as they were (any encoding); "content" (text): older plan files
            const QByteArray content = o.contains(QStringLiteral("contentBase64"))
                                           ? QByteArray::fromBase64(o.value(QStringLiteral("contentBase64")).toString().toLatin1())
                                           : o.value(QStringLiteral("content")).toString().toUtf8();
            const QString key = o.value(QStringLiteral("key")).toString();
            if (!usedGpx.contains(key) || gpxPaths.contains(key))
                continue;
            if (key.isEmpty() || content.isEmpty() || content.size() > kMaxGpxBytes
                || (!content.contains("<gpx") && !content.contains(QByteArray("<\0g\0p\0x", 7))))
                continue;
            const QString fileName = safeGpxName(o.value(QStringLiteral("fileName")).toString());
            const QFileInfo fi(fileName);
            QString target = QDir(folder).filePath(fileName);
            for (int n = 2;; ++n) {
                QFile existingFile(target);
                if (!existingFile.exists())
                    break;
                if (existingFile.open(QIODevice::ReadOnly) && existingFile.readAll() == content)
                    break;   // the same route: reuse
                target = QDir(folder).filePath(QStringLiteral("%1 (%2).gpx").arg(fi.completeBaseName()).arg(n));
            }
            if (QFileInfo::exists(target)) {
                ++gpxReused;
            } else {
                QSaveFile out(target);
                if (!out.open(QIODevice::WriteOnly) || out.write(content) != content.size() || !out.commit())
                    continue;
                ++gpxAdded;
            }
            gpxPaths.insert(key, QFileInfo(target).absoluteFilePath());
        }
        if (m_gpxRoutes && gpxAdded > 0)
            m_gpxRoutes->rescanNow();
    }

    // 3. the entries, day 1 on start
    int count = 0, videosLinked = 0, videosOpen = 0, skipped = 0;
    auto mapOptions = [&](QJsonObject opts) {
        QVariantMap m = opts.toVariantMap();
        const QString wk = opts.value(QStringLiteral("workout")).toString();
        m.remove(QStringLiteral("workout"));
        m.insert(QStringLiteral("workoutId"), wk.isEmpty() ? qint64(-1) : workoutIds.value(wk, -1));
        return m;
    };
    auto findVideo = [&](const QJsonObject &video) -> QString {
        if (!m_videos)
            return {};
        const QString name = normal(video.value(QStringLiteral("name")).toString());
        const QString folder = normal(video.value(QStringLiteral("fileName")).toString());
        for (const RlvLibrary::Entry &e : m_videos->entries()) {
            if (!e.valid)
                continue;
            if ((!name.isEmpty() && (normal(e.title) == name || normal(e.name) == name))
                || (!folder.isEmpty() && normal(QFileInfo(e.folderPath).fileName()) == folder))
                return e.path;
        }
        return {};
    };
    QVector<QJsonObject> sorted;
    for (const QJsonValue v : entries)
        sorted.append(v.toObject());
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const int da = a.value(QStringLiteral("day")).toInt(), db = b.value(QStringLiteral("day")).toInt();
        return da != db ? da < db : a.value(QStringLiteral("position")).toInt() < b.value(QStringLiteral("position")).toInt();
    });
    const qint64 profile = m_profiles->profileId();
    for (const QJsonObject &e : sorted) {
        const int day = e.value(QStringLiteral("day")).toInt(-1);
        if (day < 0 || day > kMaxWeeks * 7) {
            ++skipped;
            continue;
        }
        const QDate date = start.addDays(day);
        const QString kind = e.value(QStringLiteral("kind")).toString();
        const QString notes = e.value(QStringLiteral("notes")).toString();
        const QJsonObject opts = e.value(QStringLiteral("options")).toObject();
        PlannedWorkout p;
        p.profileId = profile;
        p.date = date;
        p.kind = kind;
        p.notes = notes;
        if (kind == plankind::workout()) {
            p.workoutId = workoutIds.value(e.value(QStringLiteral("workout")).toString(), -1);
            if (p.workoutId < 0) {
                ++skipped;
                continue;
            }
        } else if (plankind::isFree(kind)) {
            p.plannedDurationSec = std::clamp(e.value(QStringLiteral("durationMin")).toInt(), 0, int(kMaxFreeRideMin)) * 60;
            QVariantMap o;
            if (kind == plankind::freeRide() && !normalizeOptions(kind, opts.toVariantMap(), &o)) {
                ++skipped;
                continue;
            }
            p.options = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(o)).toJson(QJsonDocument::Compact));
        } else if (plankind::isRoute(kind)) {
            QVariantMap o;
            if (!normalizeOptions(kind, mapOptions(opts), &o)) {
                // an attached workout that could not be imported: ride without it
                QVariantMap plain = mapOptions(opts);
                plain.insert(QStringLiteral("workoutId"), qint64(-1));
                if (!normalizeOptions(kind, plain, &o)) {
                    ++skipped;
                    continue;
                }
            }
            p.options = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(o)).toJson(QJsonDocument::Compact));
            if (kind == plankind::gpx()) {
                p.routePath = gpxPaths.value(e.value(QStringLiteral("gpx")).toString());
                if (p.routePath.isEmpty()) {
                    ++skipped;
                    continue;
                }
                const RouteInfo route = lookupRoute(p.routePath, true, m_gpxRoutes.data());
                p.routeName = route.found ? route.name : QFileInfo(p.routePath).completeBaseName();
            } else {
                const QJsonObject video = e.value(QStringLiteral("video")).toObject();
                p.routeName = video.value(QStringLiteral("name")).toString().trimmed();
                if (p.routeName.isEmpty())
                    p.routeName = QStringLiteral("Video Ride");
                p.routePath = findVideo(video);
                p.routePath.isEmpty() ? ++videosOpen : ++videosLinked;
            }
        } else {
            ++skipped;
            continue;
        }
        if (m_db->addPlannedEntry(p) >= 0)
            ++count;
        else
            ++skipped;
    }
    QStringList parts;
    parts << QStringLiteral("%1 entries planned from %2").arg(count).arg(QLocale().toString(start, QLocale::ShortFormat));
    if (added + reused > 0)
        parts << QStringLiteral("%1 workouts added%2").arg(added).arg(reused > 0 ? QStringLiteral(" (%1 already in your library)").arg(reused) : QString());
    if (gpxAdded + gpxReused > 0)
        parts << QStringLiteral("%1 GPX routes saved in your GPX library").arg(gpxAdded + gpxReused);
    if (videosLinked > 0)
        parts << QStringLiteral("%1 video rides linked to your videos").arg(videosLinked);
    if (videosOpen > 0)
        parts << QStringLiteral("%1 video rides need your video (Edit the entry)").arg(videosOpen);
    if (skipped > 0)
        parts << QStringLiteral("%1 entries skipped").arg(skipped);
    m_lastMessage = parts.join(QStringLiteral(" · "));
    if (count == 0) {
        setError(QStringLiteral("Nothing could be imported"));
        return -1;
    }
    setError(QString());
    refresh();
    return count;
}

} // namespace watt
