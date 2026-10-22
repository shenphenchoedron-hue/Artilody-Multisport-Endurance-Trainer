#include "WorkoutLibrary.h"

#include "Database.h"
#include "ProfileManager.h"

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
    const int ftp = m_profiles->ftp();
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
    const int ftp = m_profiles->ftp();
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

} // namespace watt
