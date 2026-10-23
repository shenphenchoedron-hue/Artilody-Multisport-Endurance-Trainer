#include "WorkoutLibrary.h"

#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace watt {

WorkoutLibrary::WorkoutLibrary(Database *db, ProfileManager *profiles, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    // Watts and zone colours depend on the profile's FTP and zones.
    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        if (!m_visible.isEmpty())
            emit dataChanged(index(0), index(m_visible.size() - 1));
        emit selectedChanged();
    });
}

int WorkoutLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant WorkoutLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size())
        return {};
    const Workout &w = m_visible.at(index.row());
    const int ftp = m_profiles->current().ftpFor(w.sport);
    switch (role) {
    case IdRole: return w.id;
    case NameRole: return w.name;
    case CategoryRole: return w.category;
    case FocusRole: return w.focus;
    case DescriptionRole: return w.description;
    case DurationMinRole: return int(std::lround(w.totalDurationSec() / 60.0));
    case TotalSecRole: return w.totalDurationSec();
    case SummaryRole: return w.summary(ftp);
    case IntervalsLabelRole: return w.intervalsLabel();
    case IntensityRole: return int(std::lround(w.averagePercent() * 100));
    case FavoriteRole: return w.favorite;
    case BuiltInRole: return w.builtIn;
    case SportRole: return w.sport;
    case SegmentsRole: return Workout::segmentsToVariant(w.segments(ftp, m_profiles->current().zones));
    default: return {};
    }
}

QHash<int, QByteArray> WorkoutLibrary::roleNames() const
{
    return {
        { IdRole, "workoutId" }, { NameRole, "name" }, { CategoryRole, "category" }, { FocusRole, "focus" },
        { DescriptionRole, "description" }, { DurationMinRole, "durationMin" }, { SummaryRole, "summary" },
        { IntervalsLabelRole, "intervalsLabel" }, { IntensityRole, "intensity" }, { FavoriteRole, "favorite" },
        { BuiltInRole, "builtIn" }, { SegmentsRole, "segments" }, { TotalSecRole, "totalSec" },
        { SportRole, "sport" },
    };
}

void WorkoutLibrary::setSearchText(const QString &text)
{
    if (m_search == text)
        return;
    m_search = text;
    applyFilter();
    emit filterChanged();
}

void WorkoutLibrary::setCategory(const QString &category)
{
    if (m_category == category)
        return;
    m_category = category;
    applyFilter();
    emit filterChanged();
}

void WorkoutLibrary::setSortMode(int mode)
{
    if (m_sort == mode)
        return;
    m_sort = mode;
    applyFilter();
    emit filterChanged();
}

void WorkoutLibrary::setSportFilter(const QString &sport)
{
    const QString value = sport.trimmed().toLower() == QLatin1String("all") || sport.trimmed().isEmpty()
        ? QStringLiteral("all") : sport::normalize(sport);
    if (m_sportFilter == value)
        return;
    m_sportFilter = value;
    applyFilter();
    emit filterChanged();
}

QStringList WorkoutLibrary::sports() const
{
    return { QStringLiteral("all"), sport::cycling(), sport::rowing() };
}

QStringList WorkoutLibrary::categories() const
{
    return { QStringLiteral("All"), QStringLiteral("Endurance"), QStringLiteral("Intervals"),
             QStringLiteral("Recovery"), QStringLiteral("FTP Tests"), QStringLiteral("Custom") };
}

QStringList WorkoutLibrary::sortModes() const
{
    return { QStringLiteral("Most Popular"), QStringLiteral("Name A–Z"), QStringLiteral("Shortest"),
             QStringLiteral("Longest"), QStringLiteral("Favorites"), QStringLiteral("Newest") };
}

void WorkoutLibrary::setSelectedId(qint64 id)
{
    if (m_selectedId == id)
        return;
    m_selectedId = id;
    emit selectedChanged();
}

QVariantMap WorkoutLibrary::selected() const
{
    return get(m_selectedId);
}

void WorkoutLibrary::reload()
{
    m_all = m_db->workouts();
    applyFilter();
    if (m_selectedId < 0 || !workout(m_selectedId)) {
        m_selectedId = m_visible.isEmpty() ? (m_all.isEmpty() ? -1 : m_all.first().id) : m_visible.first().id;
    }
    emit selectedChanged();
}

std::optional<Workout> WorkoutLibrary::workout(qint64 id) const
{
    for (const Workout &w : m_all) {
        if (w.id == id)
            return w;
    }
    return std::nullopt;
}

QVariantMap WorkoutLibrary::toMap(const Workout &w) const
{
    const int ftp = m_profiles->current().ftpFor(w.sport);
    QVariantMap m;
    m.insert(QStringLiteral("workoutId"), w.id);
    m.insert(QStringLiteral("name"), w.name);
    m.insert(QStringLiteral("category"), w.category);
    m.insert(QStringLiteral("focus"), w.focus);
    m.insert(QStringLiteral("description"), w.description);
    m.insert(QStringLiteral("durationMin"), int(std::lround(w.totalDurationSec() / 60.0)));
    m.insert(QStringLiteral("totalSec"), w.totalDurationSec());
    m.insert(QStringLiteral("summary"), w.summary(ftp));
    m.insert(QStringLiteral("intervalsLabel"), w.intervalsLabel());
    m.insert(QStringLiteral("intensity"), int(std::lround(w.averagePercent() * 100)));
    m.insert(QStringLiteral("tss"), int(std::lround(w.estimatedTss())));
    m.insert(QStringLiteral("favorite"), w.favorite);
    m.insert(QStringLiteral("builtIn"), w.builtIn);
    m.insert(QStringLiteral("sport"), w.sport);
    m.insert(QStringLiteral("ftp"), ftp);
    m.insert(QStringLiteral("deletable"), !w.builtIn);
    m.insert(QStringLiteral("isFtpTest"), w.isFtpTest());
    m.insert(QStringLiteral("requiredForFtp"), w.isProtected());
    m.insert(QStringLiteral("blocks"), w.intervals.size());
    m.insert(QStringLiteral("segments"), Workout::segmentsToVariant(w.segments(ftp, m_profiles->current().zones)));
    return m;
}

QVariantMap WorkoutLibrary::get(qint64 id) const
{
    if (auto w = workout(id))
        return toMap(*w);
    return {};
}

void WorkoutLibrary::toggleFavorite(qint64 id)
{
    for (Workout &w : m_all) {
        if (w.id == id) {
            w.favorite = !w.favorite;
            m_db->setWorkoutFavorite(id, w.favorite);
            break;
        }
    }
    for (int row = 0; row < m_visible.size(); ++row) {
        if (m_visible[row].id == id) {
            m_visible[row].favorite = !m_visible[row].favorite;
            emit dataChanged(index(row), index(row), { FavoriteRole });
        }
    }
    if (id == m_selectedId)
        emit selectedChanged();
}

bool WorkoutLibrary::remove(qint64 id)
{
    // Built-in workouts are never deleted (the Database additionally protects
    // the built-in FTP test, which is the basis for FTP testing).
    auto w = workout(id);
    if (!w)
        w = m_db->workout(id);
    if (!w || w->builtIn)
        return false;
    if (!m_db->deleteWorkout(id))
        return false;
    if (id == m_selectedId)
        m_selectedId = -1;
    reload();
    return true;
}

qint64 WorkoutLibrary::duplicate(qint64 id)
{
    auto w = workout(id);
    if (!w)
        return -1;
    Workout copy = *w;
    copy.id = -1;
    copy.builtIn = false;
    copy.timesUsed = 0;
    copy.favorite = false;
    copy.name += QStringLiteral(" (copy)");
    const qint64 newId = m_db->saveWorkout(copy);
    reload();
    return newId;
}

void WorkoutLibrary::applyFilter()
{
    beginResetModel();
    m_visible.clear();
    const QString needle = m_search.trimmed();
    for (const Workout &w : m_all) {
        if (m_sportFilter != QLatin1String("all") && w.sport != m_sportFilter)
            continue;
        if (m_category != QLatin1String("All")) {
            const bool custom = m_category == QLatin1String("Custom");
            if (custom ? w.builtIn : w.category != m_category)
                continue;
        }
        if (!needle.isEmpty() && !w.name.contains(needle, Qt::CaseInsensitive)
            && !w.focus.contains(needle, Qt::CaseInsensitive) && !w.category.contains(needle, Qt::CaseInsensitive))
            continue;
        m_visible.append(w);
    }
    auto byName = [](const Workout &a, const Workout &b) { return a.name.localeAwareCompare(b.name) < 0; };
    switch (m_sort) {
    case MostPopular:
        std::stable_sort(m_visible.begin(), m_visible.end(), [](const Workout &a, const Workout &b) {
            return a.timesUsed > b.timesUsed;
        });
        break;
    case NameAZ:
        std::stable_sort(m_visible.begin(), m_visible.end(), byName);
        break;
    case Shortest:
        std::stable_sort(m_visible.begin(), m_visible.end(), [](const Workout &a, const Workout &b) {
            return a.totalDurationSec() < b.totalDurationSec();
        });
        break;
    case Longest:
        std::stable_sort(m_visible.begin(), m_visible.end(), [](const Workout &a, const Workout &b) {
            return a.totalDurationSec() > b.totalDurationSec();
        });
        break;
    case FavoritesFirst:
        std::stable_sort(m_visible.begin(), m_visible.end(), [](const Workout &a, const Workout &b) {
            return a.favorite && !b.favorite;
        });
        break;
    case Newest:
        std::stable_sort(m_visible.begin(), m_visible.end(), [](const Workout &a, const Workout &b) {
            return a.id > b.id;
        });
        break;
    }
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------
// Sharing: export / import
// ---------------------------------------------------------------------------
namespace {

QString pluralWorkouts(int n)
{
    return n == 1 ? QStringLiteral("1 workout") : QStringLiteral("%1 workouts").arg(n);
}

// "Name (imported)" / "Name (imported 3)" -> "Name"
QString baseImportName(const QString &name)
{
    static const QRegularExpression suffix(QStringLiteral("\\s*\\(imported(?:\\s+\\d+)?\\)$"));
    QString n = name;
    n.remove(suffix);
    return n.trimmed();
}

bool isDuplicate(const Workout &w, const QVector<Workout> &existing)
{
    const QString base = baseImportName(w.name);
    for (const Workout &e : existing) {
        if (e.intervals == w.intervals && baseImportName(e.name).compare(base, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString uniqueName(const QString &name, const QVector<Workout> &existing)
{
    QSet<QString> used;
    for (const Workout &e : existing)
        used.insert(e.name.toLower());
    if (!used.contains(name.toLower()))
        return name;
    QString candidate = name + QStringLiteral(" (imported)");
    for (int n = 2; used.contains(candidate.toLower()); ++n)
        candidate = name + QStringLiteral(" (imported %1)").arg(n);
    return candidate;
}

QString exportDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return dir.isEmpty() ? QDir::homePath() : dir;
}

} // namespace

QString WorkoutLibrary::localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

void WorkoutLibrary::setMessage(const QString &message)
{
    m_lastMessage = message;
    emit lastMessageChanged();
}

int WorkoutLibrary::customCount() const
{
    return int(std::count_if(m_all.cbegin(), m_all.cend(), [](const Workout &w) { return !w.builtIn; }));
}

QUrl WorkoutLibrary::suggestedExportUrl(qint64 id, const QString &format) const
{
    const QDir dir(exportDir());
    const auto w = workout(id);
    if (format == QLatin1String("all") || !w)
        return QUrl::fromLocalFile(dir.filePath(QStringLiteral("My workouts.awt")));
    const QString suffix = format == QLatin1String("zwo") ? QStringLiteral(".zwo") : QStringLiteral(".awt");
    return QUrl::fromLocalFile(dir.filePath(workoutio::safeFileName(w->name) + suffix));
}

bool WorkoutLibrary::exportWorkout(qint64 id, const QUrl &file, const QString &formatHint)
{
    auto w = workout(id);
    if (!w)
        w = m_db->workout(id);
    if (!w) {
        setMessage(QStringLiteral("No workout selected"));
        return false;
    }
    QString path = localPath(file);
    if (path.isEmpty()) {
        setMessage(QStringLiteral("Export failed: no file chosen"));
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool knownSuffix = suffix == QLatin1String("awt") || suffix == QLatin1String("json")
                          || suffix == QLatin1String("zwo") || suffix == QLatin1String("xml");
    workoutio::Format format;
    if (knownSuffix) {
        format = workoutio::formatForFileName(path);
    } else {
        format = formatHint.compare(QLatin1String("zwo"), Qt::CaseInsensitive) == 0 ? workoutio::Format::Zwo
                                                                                   : workoutio::Format::Awt;
        path += format == workoutio::Format::Zwo ? QStringLiteral(".zwo") : QStringLiteral(".awt");
    }
    const QByteArray data = format == workoutio::Format::Zwo
        ? workoutio::toZwo(*w)
        : workoutio::toAwt({ *w }, QCoreApplication::applicationVersion());
    QString err;
    if (!workoutio::writeFile(path, data, &err)) {
        setMessage(QStringLiteral("Export failed: %1").arg(err));
        return false;
    }
    setMessage(QStringLiteral("Exported \"%1\" to %2").arg(w->name, QDir::toNativeSeparators(path)));
    return true;
}

int WorkoutLibrary::writeAwt(const QVector<Workout> &workouts, const QString &rawPath)
{
    QString path = rawPath;
    if (path.isEmpty()) {
        setMessage(QStringLiteral("Export failed: no file chosen"));
        return 0;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("awt") && suffix != QLatin1String("json"))
        path += QStringLiteral(".awt");   // several workouts are always written as .awt
    QString err;
    if (!workoutio::writeFile(path, workoutio::toAwt(workouts, QCoreApplication::applicationVersion()), &err)) {
        setMessage(QStringLiteral("Export failed: %1").arg(err));
        return 0;
    }
    setMessage(QStringLiteral("Exported %1 to %2").arg(pluralWorkouts(workouts.size()), QDir::toNativeSeparators(path)));
    return workouts.size();
}

int WorkoutLibrary::exportWorkouts(const QVariantList &ids, const QUrl &file)
{
    QVector<Workout> list;
    for (const QVariant &v : ids) {
        auto w = workout(v.toLongLong());
        if (!w)
            w = m_db->workout(v.toLongLong());
        if (w)
            list.append(*w);
    }
    if (list.isEmpty()) {
        setMessage(QStringLiteral("No workouts to export"));
        return 0;
    }
    return writeAwt(list, localPath(file));
}

int WorkoutLibrary::exportAllCustom(const QUrl &file)
{
    QVector<Workout> list;
    const QVector<Workout> all = m_db->workouts();
    for (const Workout &w : all) {
        if (!w.builtIn)
            list.append(w);
    }
    if (list.isEmpty()) {
        setMessage(QStringLiteral("You have no custom workouts to export yet"));
        return 0;
    }
    return writeAwt(list, localPath(file));
}

int WorkoutLibrary::importFile(const QUrl &file)
{
    return importFiles({ file });
}

int WorkoutLibrary::importFiles(const QList<QUrl> &files)
{
    if (files.isEmpty()) {
        setMessage(QStringLiteral("No file selected"));
        return 0;
    }
    QVector<Workout> existing = m_db->workouts();
    int imported = 0;
    int duplicates = 0;
    qint64 firstId = -1;
    QStringList warnings;
    QStringList errors;
    for (const QUrl &url : files) {
        const QString path = localPath(url);
        const QString prefix = files.size() > 1 ? QFileInfo(path).fileName() + QStringLiteral(": ") : QString();
        const workoutio::ImportResult res = workoutio::parseFile(path);
        if (!res.error.isEmpty())
            errors.append(prefix + res.error);
        for (Workout w : res.workouts) {
            if (isDuplicate(w, existing)) {
                ++duplicates;
                continue;
            }
            w.name = uniqueName(w.name, existing);
            const qint64 id = m_db->saveWorkout(w);
            if (id < 0) {
                errors.append(prefix + QStringLiteral("could not save \"%1\" (%2)").arg(w.name, m_db->lastError()));
                continue;
            }
            w.id = id;
            existing.append(w);
            ++imported;
            if (firstId < 0)
                firstId = id;
        }
        for (const QString &warning : res.warnings)
            warnings.append(prefix + warning);
    }

    if (imported > 0) {
        reload();
        // make sure the imported workout is visible and selected
        const bool visible = std::any_of(m_visible.cbegin(), m_visible.cend(),
                                         [firstId](const Workout &w) { return w.id == firstId; });
        if (!visible) {
            m_category = QStringLiteral("All");
            m_search.clear();
            applyFilter();
            emit filterChanged();
        }
        setSelectedId(firstId);
    }

    QString msg;
    if (imported > 0) {
        msg = QStringLiteral("Imported %1").arg(pluralWorkouts(imported));
        if (duplicates > 0)
            msg += duplicates == 1 ? QStringLiteral(" (1 duplicate skipped)")
                                   : QStringLiteral(" (%1 duplicates skipped)").arg(duplicates);
        msg += QLatin1Char('.');
        if (!errors.isEmpty())
            msg += QStringLiteral(" Not imported: %1").arg(errors.join(QStringLiteral("; ")));
    } else if (duplicates > 0) {
        msg = duplicates == 1 ? QStringLiteral("Nothing imported – the workout is already in your library.")
                              : QStringLiteral("Nothing imported – all %1 workouts are already in your library.").arg(duplicates);
    } else {
        msg = QStringLiteral("Import failed: %1").arg(errors.isEmpty() ? QStringLiteral("no workouts found") : errors.join(QStringLiteral("; ")));
    }
    if (!warnings.isEmpty()) {
        msg += QStringLiteral(" Warnings: %1").arg(warnings.mid(0, 3).join(QStringLiteral("; ")));
        if (warnings.size() > 3)
            msg += QStringLiteral(" (+%1 more)").arg(warnings.size() - 3);
    }
    setMessage(msg);
    return imported;
}

} // namespace watt
