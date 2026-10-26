#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace watt {

class Database;

// Library of Real Life Video routes. Exposed to QML as "Videos" (Artilody.Backend).
//
// Scans the library folder (default "~/Documents/Artilody Videos", setting
// "rlv/libraryFolder") and the extra folders (setting "rlv/extraFolders",
// newline separated) recursively for routes: gpx-import .xml files (+ gpx +
// video) and .gpx files with a same-basename video. gpx files referenced by
// an xml are not listed twice. Parsed metadata is cached in memory and in the
// setting "rlv/cache" (JSON, keyed by file path + modification time + size),
// so a restart does not parse every track again.
//
// Temporary folders (addTemporaryFolder(), env WATT_RLV_DIR in main.cpp) are
// scanned but never saved.
class RlvLibrary : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString libraryFolder READ libraryFolder WRITE setLibraryFolder NOTIFY foldersChanged)
    Q_PROPERTY(QString defaultLibraryFolder READ defaultLibraryFolder CONSTANT)
    Q_PROPERTY(QStringList extraFolders READ extraFolders NOTIFY foldersChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)     // all scanned folders
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        TitleRole,
        DisplayNameRole,    // title, else name
        CountryRole,
        DistanceMRole,
        DistanceKmRole,
        AscentMRole,
        DurationSecRole,
        FolderPathRole,
        PathRole,           // xml or gpx file the route is loaded from
        VideoPathRole,
        ValidRole,
        ErrorRole,
        LabelRole,          // "Menglonnais · 12.0 km · 154 m" (drop-down text)
    };
    Q_ENUM(Roles)

    struct Entry
    {
        QString path;
        QString name, title, country, folderPath, videoPath, error;
        double distanceM = 0;
        double ascentM = 0;
        int durationSec = 0;
        bool valid = false;
        qint64 mtime = 0;     // ms since epoch of the route file + gpx (max)
        qint64 size = 0;
    };

    explicit RlvLibrary(Database *db, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }
    const QVector<Entry> &entries() const { return m_entries; }
    QString libraryFolder() const { return m_libraryFolder; }
    void setLibraryFolder(const QString &folder);
    static QString defaultLibraryFolder();
    QStringList extraFolders() const { return m_extraFolders; }
    QStringList folders() const;
    QString lastError() const { return m_lastError; }

    // Scanned but not persisted (screenshot / development folders).
    void addTemporaryFolder(const QString &folder);

    Q_INVOKABLE void rescan();
    Q_INVOKABLE bool addFolder(const QString &folderOrUrl);       // extra folder (persisted) + rescan
    Q_INVOKABLE bool removeFolder(const QString &folderOrUrl);
    // Adds a route folder (or file) to the library and returns its row, -1 on error (lastError).
    Q_INVOKABLE int importRoute(const QString &folderOrUrl);
    Q_INVOKABLE QVariantMap get(int row) const;                   // all roles by name, {} when out of range
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int indexOfPath(const QString &path) const;       // route file or its folder
    Q_INVOKABLE QString ensureLibraryFolder();                    // creates the library folder, returns it

    // "file:///x" or "/x" -> cleaned local path
    static QString toLocalPath(const QString &folderOrUrl);
    static QString labelFor(const Entry &e);
    // Route files under folder (recursive): xml routes first, gpx(+video) not referenced by them.
    static QStringList findRoutes(const QString &folder);

signals:
    void countChanged();
    void foldersChanged();
    void lastErrorChanged();
    void scanned();

private:
    Entry entryFor(const QString &path);
    void saveFolders();
    void loadCache();
    void saveCache();
    void setError(const QString &e);

    Database *m_db;
    QString m_libraryFolder;
    QStringList m_extraFolders;
    QStringList m_tempFolders;
    QVector<Entry> m_entries;
    QHash<QString, Entry> m_cache;
    bool m_cacheDirty = false;
    QString m_lastError;
};

} // namespace watt
