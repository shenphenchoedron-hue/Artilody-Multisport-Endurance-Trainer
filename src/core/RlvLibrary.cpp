#include "RlvLibrary.h"

#include "Database.h"
#include "RlvRoute.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QThread>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace watt {

namespace {
const QString kSettingFolder = QStringLiteral("rlv/libraryFolder");
const QString kSettingExtra = QStringLiteral("rlv/extraFolders");
const QString kSettingCache = QStringLiteral("rlv/cache");

// Modification time (max) and size (sum) of the route file and its track:
// for an xml route the gpx it resolved to (gpx-file-path), else the
// same-basename gpx next to it.
qint64 fileStamp(const QString &path, const QString &gpxPath, qint64 *size)
{
    const QFileInfo fi(path);
    qint64 m = fi.lastModified().toMSecsSinceEpoch();
    qint64 s = fi.size();
    if (fi.suffix().compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0) {
        const QFileInfo gpx(!gpxPath.isEmpty()
                                ? gpxPath
                                : fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".gpx")));
        if (gpx.isFile()) {
            m = std::max(m, gpx.lastModified().toMSecsSinceEpoch());
            s += gpx.size();
        }
    }
    if (size)
        *size = s;
    return m;
}
} // namespace

RlvLibrary::RlvLibrary(Database *db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
{
    m_scanWatchdog.setSingleShot(true);
    connect(&m_scanWatchdog, &QTimer::timeout, this, &RlvLibrary::abandonScan);
    m_libraryFolder = toLocalPath(m_db->setting(kSettingFolder, defaultLibraryFolder()).toString());
    if (m_libraryFolder.isEmpty())
        m_libraryFolder = defaultLibraryFolder();
    for (const QString &f : m_db->setting(kSettingExtra).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        m_extraFolders << toLocalPath(f);
    m_extraFolders.removeDuplicates();
    loadCache();
}

RlvLibrary::~RlvLibrary()
{
    // (a scan that hangs in the file system is left behind rather than
    // blocking the exit; the worker touches no member)
    if (m_scanThread) {
        if (m_scanThread->wait(5000))
            delete m_scanThread.data();
    }
    for (const QPointer<QThread> &t : std::as_const(m_abandonedScans)) {
        if (t && t->wait(500))
            delete t.data();
    }
}

namespace {
std::atomic<int> s_scanDelayMs { 0 };
}

void RlvLibrary::setScanDelayForTesting(int ms)
{
    s_scanDelayMs = std::max(0, ms);
}

QString RlvLibrary::defaultLibraryFolder()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath();
    return QDir(docs).filePath(QStringLiteral("Artilody Videos"));
}

QString RlvLibrary::toLocalPath(const QString &in)
{
    QString s = in.trimmed();
    if (s.isEmpty())
        return {};
    if (s.startsWith(QLatin1String("file:"))) {
        const QUrl url(s);
        s = url.isLocalFile() ? url.toLocalFile() : url.path();
    }
    if (s.startsWith(QLatin1String("~/")))
        s = QDir::homePath() + s.mid(1);
    return QDir::cleanPath(QFileInfo(s).absoluteFilePath());
}

QStringList RlvLibrary::folders() const
{
    QStringList all;
    all << m_libraryFolder << m_extraFolders << m_tempFolders;
    all.removeAll(QString());
    all.removeDuplicates();
    return all;
}

void RlvLibrary::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

// --- model ------------------------------------------------------------------------

int RlvLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QString RlvLibrary::labelFor(const Entry &e)
{
    const QString name = e.title.isEmpty() ? e.name : e.title;
    return QStringLiteral("%1 · %2 km · %3 m")
        .arg(name, QString::number(e.distanceM / 1000.0, 'f', 1))
        .arg(int(std::lround(e.ascentM)));
}

QVariant RlvLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole: return labelFor(e);
    case NameRole: return e.name;
    case TitleRole: return e.title;
    case DisplayNameRole: return e.title.isEmpty() ? e.name : e.title;
    case CountryRole: return e.country;
    case DistanceMRole: return e.distanceM;
    case DistanceKmRole: return std::round(e.distanceM / 100.0) / 10.0;
    case AscentMRole: return int(std::lround(e.ascentM));
    case DurationSecRole: return e.durationSec;
    case FolderPathRole: return e.folderPath;
    case PathRole: return e.path;
    case VideoPathRole: return e.videoPath;
    case ValidRole: return e.valid;
    case ErrorRole: return e.error;
    case FolderUrlRole: return QUrl::fromLocalFile(e.folderPath);
    default: return {};
    }
}

QHash<int, QByteArray> RlvLibrary::roleNames() const
{
    return { { NameRole, "name" },           { TitleRole, "title" },         { DisplayNameRole, "displayName" },
             { CountryRole, "country" },     { DistanceMRole, "distanceM" }, { DistanceKmRole, "distanceKm" },
             { AscentMRole, "ascentM" },     { DurationSecRole, "durationSec" },
             { FolderPathRole, "folderPath" }, { PathRole, "path" },         { VideoPathRole, "videoPath" },
             { ValidRole, "valid" },         { ErrorRole, "error" },         { LabelRole, "label" },
             { FolderUrlRole, "folderUrl" } };
}

QVariantMap RlvLibrary::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_entries.size())
        return m;
    const QModelIndex idx = index(row);
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
    m.insert(QStringLiteral("index"), row);
    return m;
}

QString RlvLibrary::pathAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).path : QString();
}

int RlvLibrary::indexOfPath(const QString &pathIn) const
{
    const QString path = toLocalPath(pathIn);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == path)
            return i;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).folderPath == path)
            return i;
    }
    return -1;
}

// --- folders ----------------------------------------------------------------------

void RlvLibrary::setLibraryFolder(const QString &folderIn)
{
    const QString folder = folderIn.trimmed().isEmpty() ? defaultLibraryFolder() : toLocalPath(folderIn);
    if (folder == m_libraryFolder)
        return;
    m_libraryFolder = folder;
    saveFolders();
    emit foldersChanged();
    rescan();
}

QString RlvLibrary::ensureLibraryFolder()
{
    QDir().mkpath(m_libraryFolder);
    return m_libraryFolder;
}

void RlvLibrary::saveFolders()
{
    m_db->setSetting(kSettingFolder, m_libraryFolder);
    m_db->setSetting(kSettingExtra, m_extraFolders.join(QLatin1Char('\n')));
}

bool RlvLibrary::addFolder(const QString &folderOrUrl)
{
    const QString folder = toLocalPath(folderOrUrl);
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        setError(QStringLiteral("Folder not found: %1").arg(folderOrUrl));
        return false;
    }
    setError(QString());
    if (!folders().contains(folder) || m_tempFolders.contains(folder)) {
        m_tempFolders.removeAll(folder);
        if (folder != m_libraryFolder && !m_extraFolders.contains(folder))
            m_extraFolders << folder;
        saveFolders();
        emit foldersChanged();
    }
    rescan();
    return true;
}

bool RlvLibrary::removeFolder(const QString &folderOrUrl)
{
    const QString folder = toLocalPath(folderOrUrl);
    const bool removed = m_extraFolders.removeAll(folder) + m_tempFolders.removeAll(folder) > 0;
    if (!removed)
        return false;
    saveFolders();
    emit foldersChanged();
    rescan();
    return true;
}

void RlvLibrary::addTemporaryFolder(const QString &folderIn)
{
    const QString folder = toLocalPath(folderIn);
    if (folder.isEmpty() || folders().contains(folder))
        return;
    m_tempFolders << folder;
    emit foldersChanged();
}

int RlvLibrary::importRoute(const QString &folderOrUrl)
{
    const QString path = toLocalPath(folderOrUrl);
    QString error;
    const RlvRoute route = RlvRoute::fromPath(path, &error);
    if (!route.isValid()) {
        setError(error.isEmpty() ? QStringLiteral("No route found in %1").arg(path) : error);
        return -1;
    }
    if (!route.hasVideo()) {
        setError(QStringLiteral("No video found for %1").arg(route.sourcePath()));
        return -1;
    }
    setError(QString());
    // already covered by a scanned folder?
    const QString routeFolder = route.folderPath();
    bool covered = false;
    for (const QString &f : folders()) {
        if (routeFolder == f || routeFolder.startsWith(f + QLatin1Char('/')))
            covered = true;
    }
    if (!covered) {
        m_extraFolders << routeFolder;
        saveFolders();
        emit foldersChanged();
    }
    rescanNow();   // the row is returned right away
    const int row = indexOfPath(route.sourcePath());
    return row >= 0 ? row : indexOfPath(routeFolder);
}

// --- scanning -----------------------------------------------------------------------

QStringList RlvLibrary::findRoutes(const QString &folder)
{
    QStringList out;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return out;
    QStringList xmls, gpxs;
    QDirIterator it(folder, { QStringLiteral("*.xml"), QStringLiteral("*.XML"), QStringLiteral("*.gpx"),
                              QStringLiteral("*.GPX") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = QDir::cleanPath(it.next());
        if (p.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive))
            xmls << p;
        else
            gpxs << p;
    }
    xmls.sort();
    gpxs.sort();
    QSet<QString> routeDirs;
    for (const QString &x : std::as_const(xmls)) {
        if (RlvRoute::isRouteXml(x)) {
            out << x;
            routeDirs.insert(QFileInfo(x).absolutePath());
        }
    }
    for (const QString &g : std::as_const(gpxs)) {
        if (routeDirs.contains(QFileInfo(g).absolutePath()))
            continue;   // the track of an xml route
        if (!RlvRoute::findVideoFor(g).isEmpty())
            out << g;
    }
    return out;
}

RlvLibrary::Entry RlvLibrary::entryFor(const QString &path, QHash<QString, Entry> &cache, bool *changed)
{
    const auto it = cache.constFind(path);
    if (it != cache.constEnd() && it->valid) {
        qint64 size = 0;
        const qint64 mtime = fileStamp(path, it->gpxPath, &size);
        if (it->mtime == mtime && it->size == size && QFileInfo(it->videoPath).isFile())
            return it.value();
    }

    Entry e;
    e.path = path;
    QString error;
    const RlvRoute r = RlvRoute::fromPath(path, &error);
    e.folderPath = QFileInfo(path).absolutePath();
    e.name = QFileInfo(path).completeBaseName();
    if (r.isValid()) {
        e.name = r.name();
        e.title = r.title() != r.name() ? r.title() : QString();
        e.country = r.country();
        e.videoPath = r.videoPath();
        e.gpxPath = r.gpxPath();
        e.distanceM = r.totalDistance();
        e.ascentM = r.ascent();
        e.durationSec = int(std::lround(r.videoDuration() - r.videoOffset()));
        e.valid = r.hasVideo();
        e.error = e.valid ? QString() : QStringLiteral("Video file not found");
    } else {
        e.error = error.isEmpty() ? QStringLiteral("Invalid route") : error;
    }
    e.mtime = fileStamp(path, e.gpxPath, &e.size);
    cache.insert(path, e);
    if (changed)
        *changed = true;
    return e;
}

RlvLibrary::ScanResult RlvLibrary::scanFolders(const QStringList &folders, QHash<QString, Entry> cache)
{
    ScanResult res;
    QSet<QString> seen;
    for (const QString &folder : folders) {
        for (const QString &p : findRoutes(folder)) {
            if (seen.contains(p))
                continue;
            seen.insert(p);
            res.found.append(entryFor(p, cache, &res.cacheChanged));
        }
    }
    std::sort(res.found.begin(), res.found.end(), [](const Entry &a, const Entry &b) {
        const QString na = a.title.isEmpty() ? a.name : a.title;
        const QString nb = b.title.isEmpty() ? b.name : b.title;
        // case-insensitive: localeAwareCompare is unreliable under the C/POSIX locale
        const int c = na.compare(nb, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.path < b.path;
    });
    // forget cache entries of files that are gone
    for (auto it = cache.begin(); it != cache.end();) {
        if (!seen.contains(it.key()) && !QFileInfo::exists(it.key())) {
            it = cache.erase(it);
            res.cacheChanged = true;
        } else {
            ++it;
        }
    }
    res.cache = std::move(cache);
    return res;
}

void RlvLibrary::rescan()
{
    if (m_scanThread) {
        m_rescanPending = true;   // folders changed while scanning: once more afterwards
        return;
    }
    m_rescanPending = false;
    auto result = std::make_shared<ScanResult>();
    const QStringList dirs = folders();
    const QHash<QString, Entry> cache = m_cache;
    QThread *thread = QThread::create([result, dirs, cache] {
        if (const int delay = s_scanDelayMs.load(); delay > 0)
            QThread::msleep(delay);
        try {
            *result = scanFolders(dirs, cache);
        } catch (...) {
            result->failed = true;
        }
    });
    m_scanThread = thread;
    m_scanResult = result;
    const QPointer<QThread> guard(thread);   // rescanNow() may delete it first
    connect(thread, &QThread::finished, this, [this, guard, result] {
        if (!guard)
            return;
        if (m_scanThread == guard.data()) {
            m_scanWatchdog.stop();
            m_scanThread = nullptr;
            m_scanResult.reset();
            if (result->failed) {
                setError(QStringLiteral("Could not scan the video library"));
                emit scanned();   // the list stays as it was
            } else {
                if (m_lastError.startsWith(QLatin1String("Scanning the video library"))
                    || m_lastError.startsWith(QLatin1String("Could not scan")))
                    setError(QString());   // an earlier failed / given-up scan
                applyScan(std::move(*result));
            }
            emit scanningChanged();
        }
        guard->deleteLater();
        if (m_rescanPending && !m_scanThread)
            rescan();
    });
    thread->start(QThread::LowPriority);
    m_scanWatchdog.setSingleShot(true);
    m_scanWatchdog.start(std::max(1, m_scanTimeoutMs));
    emit scanningChanged();
}

void RlvLibrary::abandonScan()
{
    if (!m_scanThread)
        return;
    // the worker keeps running (it cannot be interrupted inside the file
    // system); its result is ignored and the thread deleted when it ends
    m_abandonedScans.removeAll(QPointer<QThread>());
    m_abandonedScans.append(m_scanThread);
    m_scanThread = nullptr;
    m_scanResult.reset();
    setError(QStringLiteral("Scanning the video library took too long (a folder may be on a slow or "
                            "disconnected drive). Rescan to try again."));
    emit scanningChanged();
    emit scanned();   // the list stays as it was
    m_rescanPending = false;   // the same folders would hang again: only on request
}

void RlvLibrary::rescanNow()
{
    if (m_scanThread) {
        // a running scan works on older folders: let it finish, then scan anew
        m_scanWatchdog.stop();
        QThread *thread = m_scanThread;
        thread->wait();
        m_scanThread = nullptr;
        m_scanResult.reset();
        delete thread;
        emit scanningChanged();
    }
    m_rescanPending = false;
    applyScan(scanFolders(folders(), m_cache));
}

bool RlvLibrary::waitForScan(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (m_scanThread && !deadline.hasExpired()) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (m_scanThread)
            m_scanThread->wait(20);
    }
    QCoreApplication::processEvents();
    return !m_scanThread;
}

void RlvLibrary::applyScan(ScanResult result)
{
    m_cache = std::move(result.cache);
    m_cacheDirty = m_cacheDirty || result.cacheChanged;
    const int before = m_entries.size();
    beginResetModel();
    m_entries = std::move(result.found);
    endResetModel();
    if (before != m_entries.size())
        emit countChanged();
    saveCache();
    emit scanned();
}

bool RlvLibrary::isTemporaryPath(const QString &path) const
{
    auto under = [&](const QString &folder) {
        return !folder.isEmpty() && (path == folder || path.startsWith(folder + QLatin1Char('/')));
    };
    bool temp = false;
    for (const QString &f : m_tempFolders)
        temp = temp || under(f);
    if (!temp)
        return false;
    // also inside a persisted folder: it belongs there
    if (under(m_libraryFolder))
        return false;
    for (const QString &f : m_extraFolders) {
        if (under(f))
            return false;
    }
    return true;
}

// --- cache -----------------------------------------------------------------------------

void RlvLibrary::loadCache()
{
    const QJsonObject root = QJsonDocument::fromJson(m_db->setting(kSettingCache).toString().toUtf8()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.path = it.key();
        e.mtime = qint64(o.value(QStringLiteral("m")).toDouble());
        e.size = qint64(o.value(QStringLiteral("s")).toDouble());
        e.name = o.value(QStringLiteral("name")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        e.country = o.value(QStringLiteral("country")).toString();
        e.folderPath = o.value(QStringLiteral("folder")).toString();
        e.videoPath = o.value(QStringLiteral("video")).toString();
        e.distanceM = o.value(QStringLiteral("dist")).toDouble();
        e.ascentM = o.value(QStringLiteral("asc")).toDouble();
        e.durationSec = o.value(QStringLiteral("dur")).toInt();
        e.gpxPath = o.value(QStringLiteral("gpx")).toString();
        e.valid = true;   // only valid entries are stored
        m_cache.insert(e.path, e);
    }
}

void RlvLibrary::saveCache()
{
    if (!m_cacheDirty)
        return;
    m_cacheDirty = false;
    QJsonObject root;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const Entry &e = it.value();
        if (!e.valid || isTemporaryPath(e.path))
            continue;   // routes of temporary (WATT_RLV_DIR / screenshot) folders are not saved
        root.insert(e.path, QJsonObject { { QStringLiteral("m"), double(e.mtime) },
                                          { QStringLiteral("s"), double(e.size) },
                                          { QStringLiteral("name"), e.name },
                                          { QStringLiteral("title"), e.title },
                                          { QStringLiteral("country"), e.country },
                                          { QStringLiteral("folder"), e.folderPath },
                                          { QStringLiteral("video"), e.videoPath },
                                          { QStringLiteral("gpx"), e.gpxPath },
                                          { QStringLiteral("dist"), e.distanceM },
                                          { QStringLiteral("asc"), e.ascentM },
                                          { QStringLiteral("dur"), e.durationSec } });
    }
    m_db->setSetting(kSettingCache, QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

} // namespace watt
