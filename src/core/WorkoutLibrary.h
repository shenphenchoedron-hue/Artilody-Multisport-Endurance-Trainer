#pragma once

#include "Workout.h"

#include <QAbstractListModel>
#include <QUrl>
#include <QVariantMap>
#include <optional>

namespace watt {

class Database;
class ProfileManager;

// Filterable/sortable list of saved workouts. Exposed to QML as "Workouts".
class WorkoutLibrary : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY filterChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY filterChanged)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QStringList sortModes READ sortModes CONSTANT)
    Q_PROPERTY(qint64 selectedId READ selectedId WRITE setSelectedId NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)
    Q_PROPERTY(int customCount READ customCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, CategoryRole, FocusRole, DescriptionRole, DurationMinRole,
        SummaryRole, IntervalsLabelRole, IntensityRole, FavoriteRole, BuiltInRole, SegmentsRole, TotalSecRole
    };
    enum SortMode { MostPopular, NameAZ, Shortest, Longest, FavoritesFirst, Newest };
    Q_ENUM(SortMode)

    WorkoutLibrary(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchText() const { return m_search; }
    void setSearchText(const QString &text);
    QString category() const { return m_category; }
    void setCategory(const QString &category);
    int sortMode() const { return m_sort; }
    void setSortMode(int mode);
    QStringList categories() const;
    QStringList sortModes() const;
    qint64 selectedId() const { return m_selectedId; }
    void setSelectedId(qint64 id);
    QVariantMap selected() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE QVariantMap get(qint64 id) const;
    Q_INVOKABLE void toggleFavorite(qint64 id);
    Q_INVOKABLE bool remove(qint64 id);   // false for built-in workouts (never deleted)
    Q_INVOKABLE qint64 duplicate(qint64 id);

    // --- sharing (see WorkoutIO.h) ---
    // Exports one workout. The format follows the file extension (.zwo = Zwift,
    // .awt/.json = Artilody JSON); without a known extension formatHint
    // ("awt"/"zwo") decides and the matching extension is appended.
    Q_INVOKABLE bool exportWorkout(qint64 id, const QUrl &file, const QString &formatHint = QString());
    // Exports several workouts into one .awt file. Returns the number exported.
    Q_INVOKABLE int exportWorkouts(const QVariantList &ids, const QUrl &file);
    // Exports all custom (non built-in) workouts into one .awt file.
    Q_INVOKABLE int exportAllCustom(const QUrl &file);
    // Imports .awt/.json/.zwo files as custom workouts. Returns the number imported.
    Q_INVOKABLE int importFile(const QUrl &file);
    Q_INVOKABLE int importFiles(const QList<QUrl> &files);
    // format: "awt", "zwo" or "all" (bulk export of custom workouts).
    Q_INVOKABLE QUrl suggestedExportUrl(qint64 id, const QString &format) const;

    QString lastMessage() const { return m_lastMessage; }
    int customCount() const;
    std::optional<Workout> workout(qint64 id) const;

signals:
    void filterChanged();
    void selectedChanged();
    void countChanged();
    void lastMessageChanged();

private:
    void applyFilter();
    void setMessage(const QString &message);
    int writeAwt(const QVector<Workout> &workouts, const QString &path);
    static QString localPath(const QUrl &url);
    QVariantMap toMap(const Workout &w) const;

    Database *m_db;
    ProfileManager *m_profiles;
    QVector<Workout> m_all;
    QVector<Workout> m_visible;
    QString m_search;
    QString m_category = QStringLiteral("All");
    int m_sort = MostPopular;
    qint64 m_selectedId = -1;
    QString m_lastMessage;
};

} // namespace watt
