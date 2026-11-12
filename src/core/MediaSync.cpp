#include "MediaSync.h"

#include "Database.h"
#include "DiagLog.h"
#include "RlvLibrary.h"
#include "RlvRoute.h"
#include "ServerConnection.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

namespace watt {

namespace {
const QString kServerIdKey = QStringLiteral("server/mediaServerId");
const QString kRevKey = QStringLiteral("server/mediaRev");

bool isSha1(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{40}$"));
    return re.match(s).hasMatch();
}

// sha1 of a file; stops (returns "") when cancelled. done: bytes read so far (added to).
QString sha1Of(const QString &path, const std::atomic<bool> *cancel = nullptr, std::atomic<qint64> *done = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    QByteArray buf;
    while (!f.atEnd()) {
        if (cancel && cancel->load())
            return QString();
        buf = f.read(4 * 1024 * 1024);
        if (buf.isEmpty() && f.error() != QFile::NoError)
            return QString();
        h.addData(buf);
        if (done)
            done->fetch_add(buf.size());
    }
    return QString::fromLatin1(h.result().toHex());
}

// A file name as the server keeps it (no folders, no leading dot).
QString safeName(const QString &name)
{
    QString n = QFileInfo(name).fileName();
    n.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f/\\\\:*?\"<>|]")), QStringLiteral("_"));
    while (n.startsWith(QLatin1Char('.')) || n.startsWith(QLatin1Char(' ')))
        n.remove(0, 1);
    return n.left(200);
}

bool isVideoFile(const QString &name)
{
    return RlvRoute::videoSuffixes().contains(QFileInfo(name).suffix().toLower());
}

QString sizeText(qint64 bytes)
{
    return QLocale(QLocale::English).formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

ServerMedia mediaFromJson(const QJsonObject &o)
{
    ServerMedia m;
    m.uuid = o.value(QStringLiteral("uuid")).toString().toLower();
    m.kind = o.value(QStringLiteral("kind")).toString();
    m.name = o.value(QStringLiteral("name")).toString().simplified().left(200);
    m.folder = safeName(o.value(QStringLiteral("folder")).toString());
    m.sizeBytes = o.value(QStringLiteral("sizeBytes")).toInteger();
    m.updatedMs = o.value(QStringLiteral("updatedMs")).toInteger();
    m.uploadedBy = o.value(QStringLiteral("uploadedBy")).toString().left(100);
    for (const QJsonValue &v : o.value(QStringLiteral("files")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString name = safeName(f.value(QStringLiteral("name")).toString());
        const QString hash = f.value(QStringLiteral("hash")).toString();
        if (!name.isEmpty() && isSha1(hash))
            m.files.append({ name, hash, f.value(QStringLiteral("size")).toInteger() });
    }
    return m;
}

// the order a video route's files are fetched in: the video, the xml, the gpx
// last (the folder is a route only once its gpx is there)
int fileOrder(const ServerMediaFile &f)
{
    const QString ext = QFileInfo(f.name).suffix().toLower();
    return ext == QLatin1String("gpx") ? 2 : ext == QLatin1String("xml") ? 1 : 0;
}
} // namespace

MediaSync::MediaSync(Database *db, RlvLibrary *gpxRoutes, RlvLibrary *videos, ServerConnection *connection, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_gpx(gpxRoutes)
    , m_videos(videos)
{
    m_hashPoll.setInterval(250);
    connect(&m_hashPoll, &QTimer::timeout, this, [this] {
        if (m_hashDone && m_jobBytes > 0)
            setProgress(double(m_hashDone->load()) / double(m_jobBytes));
    });
    m_rescanTimer.setInterval(5000);
    connect(&m_rescanTimer, &QTimer::timeout, this, &MediaSync::rescanLibraries);
    for (RlvLibrary *lib : { gpxRoutes, videos }) {
        if (lib)
            connect(lib, &RlvLibrary::scanned, this, &MediaSync::refreshLocal);
    }
    if (connection) {
        connect(connection, &ServerConnection::answered, this, [this, connection] {
            serverAnswered(connection->baseUrl(), connection->serverId(), connection->mediaRev());
        });
        connect(connection, &ServerConnection::changed, this, [this, connection] {
            if (!connection->connected())
                serverGone();
        });
    }
    m_media = m_db->serverMedia();
    rebuildItems();
}

MediaSync::~MediaSync()
{
    if (m_hashCancel)
        m_hashCancel->store(true);
    if (m_hashThread)
        m_hashThread->wait(5000);
    if (m_reply)
        m_reply->abort();
}

void MediaSync::setState(const QString &state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    if (state == QLatin1String("error") && message != m_message)
        DiagLog::write("SYNC", QStringLiteral("media: %1").arg(message));
    m_state = state;
    m_message = message;
    emit changed();
}

void MediaSync::setProgress(double p)
{
    p = std::clamp(p, 0.0, 1.0);
    if (std::abs(p - m_progress) < 0.001)
        return;
    m_progress = p;
    emit transferChanged();
}

std::optional<ServerMedia> MediaSync::item(const QString &uuid) const
{
    for (const ServerMedia &m : m_media)
        if (m.uuid == uuid)
            return m;
    return std::nullopt;
}

// --- the list -------------------------------------------------------------

void MediaSync::serverGone()
{
    if (m_url.isEmpty())
        return;
    m_url.clear();
    if (m_state != QLatin1String("error"))
        setState(QStringLiteral("off"));
    emit changed();
    rebuildItems();
}

void MediaSync::serverAnswered(const QString &baseUrl, const QString &serverId, qint64 mediaRev)
{
    if (baseUrl.isEmpty() || serverId.isEmpty())
        return;
    const bool wasGone = m_url.isEmpty();
    m_url = baseUrl;
    m_serverId = serverId;
    if (m_db->setting(kServerIdKey).toString() != serverId) {
        // another server (new / reinstalled): its list from the start
        m_db->clearServerMedia();
        m_db->setSetting(kRevKey, 0);
        m_db->setSetting(kServerIdKey, serverId);
        m_media.clear();
        rebuildItems();
    }
    if (m_state == QLatin1String("off"))
        setState(QStringLiteral("idle"));
    if (wasGone) {
        emit changed();
        rebuildItems();
    }
    if (mediaRev < 0 || mediaRev != m_db->setting(kRevKey, -1).toLongLong())
        fetchList();
    else
        queueAutomatic();
}

void MediaSync::fetchList()
{
    if (m_fetching || m_url.isEmpty())
        return;
    m_fetching = true;
    setState(QStringLiteral("syncing"));
    QNetworkRequest req(QUrl(m_url + QStringLiteral("api/media.php?since=%1").arg(m_db->setting(kRevKey, 0).toLongLong())));
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_fetching = false;
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"), QStringLiteral("the media list: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value(QStringLiteral("serverId")).toString() != m_serverId || !o.contains(QStringLiteral("media"))) {
            setState(QStringLiteral("error"), QStringLiteral("the server's media list is not readable"));
            return;
        }
        applyList(o);
        setState(QStringLiteral("idle"));
        queueAutomatic();
    });
}

void MediaSync::applyList(const QJsonObject &o)
{
    int n = 0;
    for (const QJsonValue &v : o.value(QStringLiteral("media")).toArray()) {
        const QJsonObject j = v.toObject();
        const ServerMedia m = mediaFromJson(j);
        if (QUuid(m.uuid).isNull())
            continue;
        if (j.value(QStringLiteral("deleted")).toBool()) {
            m_db->removeServerMedia(m.uuid);   // (the copy here stays)
        } else if ((m.kind == QLatin1String("gpx") || m.kind == QLatin1String("video")) && !m.files.isEmpty()) {
            m_db->storeServerMedia(m);
        }
        ++n;
    }
    m_db->setSetting(kRevKey, o.value(QStringLiteral("rev")).toInteger(0));
    if (n > 0)
        DiagLog::write("SYNC", QStringLiteral("media: %1 change(s) from the server").arg(n));
    m_media = m_db->serverMedia();
    refreshLocal();
}

bool MediaSync::localValid(const ServerMedia &m) const
{
    if (m.localPath.isEmpty())
        return false;
    if (m.kind == QLatin1String("gpx"))
        return QFileInfo(m.localPath).isFile();
    for (const ServerMediaFile &f : m.files) {
        if (isVideoFile(f.name)) {
            const QFileInfo fi(QDir(m.localPath).filePath(f.name));
            return fi.isFile() && fi.size() == f.size;
        }
    }
    return false;
}

void MediaSync::refreshLocal()
{
    for (ServerMedia &m : m_media) {
        if (localValid(m))
            continue;
        QString found;
        if (m.kind == QLatin1String("gpx") && m.files.size() == 1 && m_gpx) {
            // a .gpx with the same content (by size, then sha1)
            const ServerMediaFile &f = m.files.first();
            QStringList candidates;
            for (const RlvLibrary::Entry &e : m_gpx->entries())
                candidates << e.path;
            candidates << QDir(m_gpx->libraryFolder()).filePath(f.name);
            for (const QString &path : std::as_const(candidates)) {
                const QFileInfo fi(path);
                if (fi.isFile() && fi.size() == f.size && sha1Of(path) == f.hash) {
                    found = fi.absoluteFilePath();
                    break;
                }
            }
        } else if (m.kind == QLatin1String("video") && m_videos) {
            // the video of the same name and size (videos are not hashed: GBs)
            for (const ServerMediaFile &f : m.files) {
                if (!isVideoFile(f.name))
                    continue;
                QStringList folders;
                for (const RlvLibrary::Entry &e : m_videos->entries())
                    if (QFileInfo(e.videoPath).fileName() == f.name)
                        folders << e.folderPath;
                folders << QDir(m_videos->libraryFolder()).filePath(m.folder);
                for (const QString &dir : std::as_const(folders)) {
                    const QFileInfo fi(QDir(dir).filePath(f.name));
                    if (fi.isFile() && fi.size() == f.size) {
                        found = QFileInfo(dir).absoluteFilePath();
                        break;
                    }
                }
            }
        }
        if (found != m.localPath) {
            m.localPath = found;
            m_db->setServerMediaLocalPath(m.uuid, found);
        }
    }
    rebuildItems();
}

QString MediaSync::gpxTarget(const ServerMedia &m, bool copy) const
{
    if (!m_gpx || m.files.isEmpty())
        return QString();
    const QDir dir(m_gpx->libraryFolder());
    const QString name = m.files.first().name;
    if (!copy)
        return dir.filePath(name);
    const QString base = QFileInfo(name).completeBaseName();
    for (int i = 1; i < 100; ++i) {
        const QString candidate = dir.filePath(
            i == 1 ? QStringLiteral("%1 (server).gpx").arg(base) : QStringLiteral("%1 (server %2).gpx").arg(base).arg(i));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

void MediaSync::rebuildItems()
{
    QVariantList list;
    for (const ServerMedia &m : std::as_const(m_media)) {
        QString state;
        if (m_job && m_job->type == Job::Download && m_job->uuid == m.uuid)
            state = QStringLiteral("downloading");
        else if (std::any_of(m_queue.begin(), m_queue.end(), [&](const Job &j) { return j.type == Job::Download && j.uuid == m.uuid; }))
            state = QStringLiteral("queued");
        else if (localValid(m))
            state = QStringLiteral("local");
        else if (m.kind == QLatin1String("gpx") && QFileInfo::exists(gpxTarget(m, false)))
            state = QStringLiteral("conflict");
        else
            state = QStringLiteral("available");
        list.append(QVariantMap {
            { QStringLiteral("uuid"), m.uuid },
            { QStringLiteral("kind"), m.kind },
            { QStringLiteral("name"), m.name },
            { QStringLiteral("sizeBytes"), m.sizeBytes },
            { QStringLiteral("sizeText"), sizeText(m.sizeBytes) },
            { QStringLiteral("uploadedBy"), m.uploadedBy },
            { QStringLiteral("localPath"), m.localPath },
            { QStringLiteral("state"), state },
        });
    }
    if (list != m_items) {
        m_items = list;
        emit itemsChanged();
    }
}

bool MediaSync::onServer(const QString &path) const
{
    const QString p = QFileInfo(RlvLibrary::toLocalPath(path)).absoluteFilePath();
    const QString dir = QFileInfo(p).absolutePath();
    for (const ServerMedia &m : m_media) {
        if (m.localPath.isEmpty())
            continue;
        if (m.kind == QLatin1String("gpx") ? m.localPath == p : m.localPath == dir)
            return true;
    }
    return false;
}

// GPX routes this station lacks are fetched by themselves (not during a ride).
void MediaSync::queueAutomatic()
{
    if (m_url.isEmpty() || busy())
        return;
    for (const ServerMedia &m : std::as_const(m_media)) {
        if (m.kind != QLatin1String("gpx") || localValid(m) || QFileInfo::exists(gpxTarget(m, false)))
            continue;
        const bool known = (m_job && m_job->uuid == m.uuid)
                           || std::any_of(m_queue.begin(), m_queue.end(), [&](const Job &j) { return j.uuid == m.uuid; });
        if (!known) {
            Job j;
            j.uuid = m.uuid;
            j.name = m.name;
            j.automatic = true;
            enqueue(j);
        }
    }
    startNext();
}

// --- transfers ------------------------------------------------------------

bool MediaSync::download(const QString &uuid)
{
    const std::optional<ServerMedia> m = item(uuid);
    if (!m) {
        setState(m_state, QStringLiteral("not on the server any more"));
        return false;
    }
    Job j;
    j.uuid = uuid;
    j.name = m->name;
    j.copy = m->kind == QLatin1String("gpx") && !localValid(*m) && QFileInfo::exists(gpxTarget(*m, false));
    enqueue(j);
    startNext();
    return true;
}

bool MediaSync::upload(const QString &path, const QString &kind)
{
    RlvLibrary *lib = kind == QLatin1String("video") ? m_videos.data() : m_gpx.data();
    const QString p = RlvLibrary::toLocalPath(path);
    if (!lib || lib->indexOfPath(p) < 0) {
        setState(m_state, QStringLiteral("not a route of the library"));
        return false;
    }
    const RlvLibrary::Entry &e = lib->entries().at(lib->indexOfPath(p));
    Job j;
    j.type = Job::Upload;
    j.path = e.path;
    j.kind = kind == QLatin1String("video") ? QStringLiteral("video") : QStringLiteral("gpx");
    j.name = e.title.isEmpty() ? e.name : e.title;
    enqueue(j);
    startNext();
    return true;
}

void MediaSync::enqueue(const Job &job)
{
    const auto same = [&](const Job &j) {
        return j.type == job.type && (job.type == Job::Download ? j.uuid == job.uuid : j.path == job.path);
    };
    if ((m_job && same(*m_job)) || std::any_of(m_queue.begin(), m_queue.end(), same))
        return;
    m_queue.append(job);
    emit transferChanged();
    rebuildItems();
}

void MediaSync::cancel()
{
    m_queue.clear();
    if (!m_job) {
        emit transferChanged();
        rebuildItems();
        return;
    }
    if (m_hashCancel)
        m_hashCancel->store(true);
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
    // a download's .part: removed (cancelled: not resumed); an upload's file
    // is the user's own - only closed (endJob)
    if (m_job->type == Job::Download && m_file.isOpen() && m_file.fileName().endsWith(QLatin1String(".part"))) {
        const QString part = m_file.fileName();
        m_file.close();
        QFile::remove(part);
    }
    endJob(false, QStringLiteral("cancelled"));
}

void MediaSync::startNext()
{
    if (m_job || m_queue.isEmpty() || m_url.isEmpty())
        return;
    m_job = m_queue.takeFirst();
    m_progress = 0;
    m_steps.clear();
    m_jobBytes = m_jobDone = 0;
    emit transferChanged();
    rebuildItems();
    if (m_job->type == Job::Download)
        startDownload();
    else
        startUpload();
}

void MediaSync::endJob(bool ok, const QString &error)
{
    if (!m_job)
        return;
    const Job job = *m_job;
    m_job.reset();
    if (m_file.isOpen())
        m_file.close();   // (a download's .part stays: resumed next time)
    m_hashPoll.stop();
    m_transferKind.clear();
    m_progress = 0;
    const QString what = job.type == Job::Download ? QStringLiteral("%1 downloaded").arg(job.name)
                                                   : QStringLiteral("%1 sent to the server").arg(job.name);
    if (ok)
        DiagLog::write("SYNC", QStringLiteral("media: %1").arg(what));
    else if (error != QLatin1String("cancelled"))
        setState(QStringLiteral("error"), QStringLiteral("%1: %2").arg(job.name, error));
    if (ok && m_state == QLatin1String("error"))
        setState(QStringLiteral("idle"));
    emit transferChanged();
    rebuildItems();
    if (!job.automatic || !ok)
        emit transferFinished(ok, what, error);
    QTimer::singleShot(0, this, &MediaSync::startNext);
}

void MediaSync::scheduleRescan(RlvLibrary *library)
{
    if (library == m_gpx)
        m_rescanGpx = true;
    else
        m_rescanVideos = true;
    if (!busy())
        QTimer::singleShot(0, this, &MediaSync::rescanLibraries);
    else if (!m_rescanTimer.isActive())
        m_rescanTimer.start();
}

void MediaSync::rescanLibraries()
{
    if (busy())
        return;   // (after the ride: the timer asks again)
    m_rescanTimer.stop();
    if (m_rescanGpx && m_gpx)
        m_gpx->rescan();
    if (m_rescanVideos && m_videos)
        m_videos->rescan();
    m_rescanGpx = m_rescanVideos = false;
}

// --- downloads ------------------------------------------------------------

void MediaSync::startDownload()
{
    const std::optional<ServerMedia> m = item(m_job->uuid);
    if (!m) {
        endJob(false, QStringLiteral("not on the server any more"));
        return;
    }
    m_transferKind = QStringLiteral("download");
    if (m->kind == QLatin1String("gpx")) {
        if (!m_gpx) {
            endJob(false, QStringLiteral("no GPX library"));
            return;
        }
        m_gpx->ensureLibraryFolder();
        m_jobTarget = gpxTarget(*m, m_job->copy);
        if (m_jobTarget.isEmpty() || (!m_job->copy && QFileInfo::exists(m_jobTarget))) {
            // (a file of that name here: never overwritten - see "conflict")
            endJob(false, QStringLiteral("a route of that name is already here"));
            return;
        }
        m_steps.append({ m_jobTarget, m->files.first() });
    } else {
        if (!m_videos) {
            endJob(false, QStringLiteral("no video library"));
            return;
        }
        const QDir base(m_videos->ensureLibraryFolder());
        // a folder of that name with other files: "<folder> (server)"
        QString folder = base.filePath(m->folder);
        for (int i = 1; i < 100; ++i) {
            bool clash = false;
            for (const ServerMediaFile &f : m->files) {
                const QFileInfo fi(QDir(folder).filePath(f.name));
                clash |= fi.exists() && fi.size() != f.size;
            }
            if (!clash)
                break;
            folder = base.filePath(i == 1 ? QStringLiteral("%1 (server)").arg(m->folder)
                                          : QStringLiteral("%1 (server %2)").arg(m->folder).arg(i));
        }
        if (!QDir().mkpath(folder)) {
            endJob(false, QStringLiteral("cannot create %1").arg(folder));
            return;
        }
        m_jobTarget = folder;
        QVector<ServerMediaFile> files = m->files;
        std::stable_sort(files.begin(), files.end(), [](const ServerMediaFile &a, const ServerMediaFile &b) {
            return fileOrder(a) < fileOrder(b);
        });
        for (const ServerMediaFile &f : std::as_const(files))
            m_steps.append({ QDir(folder).filePath(f.name), f });
    }
    for (const FileStep &s : std::as_const(m_steps))
        m_jobBytes += s.file.size;
    emit transferChanged();
    downloadFile(0);
}

void MediaSync::downloadFile(int index)
{
    if (index >= m_steps.size()) {
        m_db->setServerMediaLocalPath(m_job->uuid, m_jobTarget);
        for (ServerMedia &m : m_media)
            if (m.uuid == m_job->uuid)
                m.localPath = m_jobTarget;
        const std::optional<ServerMedia> m = item(m_job->uuid);
        scheduleRescan(m && m->kind == QLatin1String("video") ? m_videos.data() : m_gpx.data());
        endJob(true);
        return;
    }
    const FileStep &s = m_steps.at(index);
    const QFileInfo done(s.local);
    if (done.isFile() && done.size() == s.file.size) {   // (already here: a resumed job)
        m_jobDone += s.file.size;
        downloadFile(index + 1);
        return;
    }
    m_file.setFileName(s.local + QStringLiteral(".part"));
    if (!m_file.open(QIODevice::ReadWrite)) {
        endJob(false, QStringLiteral("cannot write %1").arg(m_file.fileName()));
        return;
    }
    m_fileOffset = std::min(m_file.size(), s.file.size);
    if (m_fileOffset >= s.file.size)
        m_fileOffset = 0;   // (complete but not verified / renamed: again)
    m_file.resize(m_fileOffset);
    m_file.seek(m_fileOffset);
    m_statusChecked = false;
    QNetworkRequest req(QUrl(m_url + QStringLiteral("blobs/") + s.file.hash));
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    if (m_fileOffset > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_fileOffset) + "-");
    QNetworkReply *reply = m_net.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300) {   // (an error page: never part of the file)
            reply->readAll();
            return;
        }
        if (!m_statusChecked) {
            m_statusChecked = true;
            // asked for the rest but given all of it: from the start
            if (m_fileOffset > 0 && status == 200) {
                m_fileOffset = 0;
                m_file.resize(0);
                m_file.seek(0);
            }
        }
        const QByteArray data = reply->readAll();
        if (m_file.write(data) != data.size()) {
            reply->abort();
            return;
        }
        if (m_jobBytes > 0)
            setProgress(double(m_jobDone + m_file.size()) / double(m_jobBytes));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, index] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            const qint64 have = m_file.size();
            const QString part = m_file.fileName();
            m_file.close();   // (kept: resumed next time)
            if (have == 0)
                QFile::remove(part);
            endJob(false, have > 0 ? QStringLiteral("%1 (resumes at %2)").arg(reply->errorString(), sizeText(have))
                                   : reply->errorString());
            return;
        }
        finishFile(index);
    });
}

void MediaSync::finishFile(int index)
{
    const FileStep &s = m_steps.at(index);
    m_file.flush();
    const qint64 size = m_file.size();
    QString hash;
    if (size == s.file.size && size <= kVerifyMaxBytes) {
        m_file.seek(0);
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(&m_file);
        hash = QString::fromLatin1(h.result().toHex());
    }
    const QString part = m_file.fileName();
    m_file.close();
    if (size != s.file.size || (size <= kVerifyMaxBytes && hash != s.file.hash)) {
        QFile::remove(part);
        endJob(false, QStringLiteral("%1 arrived damaged").arg(s.file.name));
        return;
    }
    if (QFileInfo::exists(s.local) || !QFile::rename(part, s.local)) {
        endJob(false, QStringLiteral("cannot save %1").arg(s.local));
        return;
    }
    m_jobDone += s.file.size;
    downloadFile(index + 1);
}

// --- uploads --------------------------------------------------------------

void MediaSync::startUpload()
{
    RlvLibrary *lib = m_job->kind == QLatin1String("video") ? m_videos.data() : m_gpx.data();
    const int row = lib ? lib->indexOfPath(m_job->path) : -1;
    if (row < 0) {
        endJob(false, QStringLiteral("not in the library any more"));
        return;
    }
    const RlvLibrary::Entry e = lib->entries().at(row);
    QStringList paths;
    if (m_job->kind == QLatin1String("gpx")) {
        paths << e.path;
        m_jobTarget = QFileInfo(e.path).absoluteFilePath();
    } else {
        if (!QFileInfo(e.videoPath).isFile() || !QFileInfo(e.gpxPath).isFile()) {
            endJob(false, QStringLiteral("its video or .gpx is missing"));
            return;
        }
        paths << e.videoPath << e.gpxPath;
        if (e.path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive))
            paths << e.path;
        m_jobTarget = QFileInfo(e.folderPath).absoluteFilePath();
    }
    for (const QString &p : std::as_const(paths)) {
        FileStep s;
        s.local = p;
        s.file.name = safeName(p);
        s.file.size = QFileInfo(p).size();
        m_steps.append(s);
        m_jobBytes += s.file.size;
    }
    m_uploadItem = QJsonObject {
        { QStringLiteral("kind"), m_job->kind },
        { QStringLiteral("name"), m_job->name },
        { QStringLiteral("folder"), m_job->kind == QLatin1String("video") ? QFileInfo(e.folderPath).fileName() : QString() },
        { QStringLiteral("station"), QSysInfo::machineHostName().left(90) },
    };

    // 1. the files' sha1 (worker thread: a video is GBs)
    m_transferKind = QStringLiteral("checking");
    emit transferChanged();
    m_hashCancel = std::make_shared<std::atomic<bool>>(false);
    m_hashDone = std::make_shared<std::atomic<qint64>>(0);
    auto hashes = std::make_shared<QStringList>();
    const auto cancel = m_hashCancel;
    const auto done = m_hashDone;
    QThread *t = QThread::create([paths, hashes, cancel, done] {
        for (const QString &p : paths)
            hashes->append(sha1Of(p, cancel.get(), done.get()));
    });
    m_hashThread = t;
    connect(t, &QThread::finished, this, [this, t, hashes, cancel] {
        t->deleteLater();
        if (cancel->load() || !m_job || m_job->type != Job::Upload)
            return;
        m_hashPoll.stop();
        for (int i = 0; i < m_steps.size(); ++i) {
            if (i >= hashes->size() || hashes->at(i).isEmpty()) {
                endJob(false, QStringLiteral("cannot read %1").arg(m_steps.at(i).local));
                return;
            }
            m_steps[i].file.hash = hashes->at(i);
        }
        m_transferKind = QStringLiteral("upload");
        m_progress = 0;
        emit transferChanged();
        uploadFile(0);
    });
    m_hashPoll.start();
    t->start(QThread::LowPriority);
}

void MediaSync::post(const QString &path, const QByteArray &body, const QByteArray &type,
                     std::function<void(QNetworkReply *)> done, bool allowConflict)
{
    QNetworkRequest req(QUrl(m_url + path));
    req.setTransferTimeout(kTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, type);
    req.setRawHeader("X-Artilody-Station", m_db->stationId().toLatin1());   // (the server refuses POSTs without it)
    QNetworkReply *reply = m_net.post(req, body);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, allowConflict] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && !(allowConflict && status == 409)) {
            const QString why = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("error")).toString();
            endJob(false, why.isEmpty() ? reply->errorString() : why);
            return;
        }
        done(reply);
    });
}

// 2. each file the server does not have: in pieces
void MediaSync::uploadFile(int index)
{
    if (index >= m_steps.size()) {
        sendItem();
        return;
    }
    const FileStep &s = m_steps.at(index);
    // already on the server (the same file of another item, or an earlier try)
    QNetworkRequest head(QUrl(m_url + QStringLiteral("blobs/") + s.file.hash));
    head.setTransferTimeout(kTimeoutMs);
    head.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_net.head(head);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, index] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        m_reply = nullptr;
        const FileStep &s = m_steps.at(index);
        if (reply->error() == QNetworkReply::NoError
            && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() == s.file.size) {
            m_jobDone += s.file.size;
            setProgress(double(m_jobDone) / double(std::max<qint64>(1, m_jobBytes)));
            uploadFile(index + 1);
            return;
        }
        post(QStringLiteral("api/upload.php?action=start&size=%1").arg(s.file.size), {}, "application/octet-stream",
             [this, index](QNetworkReply *r) {
                 const QString id = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("id")).toString();
                 if (id.isEmpty()) {
                     endJob(false, QStringLiteral("the server did not start the upload"));
                     return;
                 }
                 m_file.setFileName(m_steps.at(index).local);
                 if (!m_file.open(QIODevice::ReadOnly)) {
                     endJob(false, QStringLiteral("cannot read %1").arg(m_file.fileName()));
                     return;
                 }
                 uploadPiece(index, id, 0);
             });
    });
}

void MediaSync::uploadPiece(int index, const QString &id, qint64 offset)
{
    const FileStep &s = m_steps.at(index);
    if (offset >= s.file.size) {
        m_file.close();
        post(QStringLiteral("api/upload.php?action=finish&id=%1").arg(id), {}, "application/octet-stream",
             [this, index](QNetworkReply *r) {
                 const QString hash = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("hash")).toString();
                 if (hash != m_steps.at(index).file.hash) {
                     endJob(false, QStringLiteral("%1 arrived damaged at the server").arg(m_steps.at(index).file.name));
                     return;
                 }
                 m_jobDone += m_steps.at(index).file.size;
                 uploadFile(index + 1);
             });
        return;
    }
    m_file.seek(offset);
    const QByteArray piece = m_file.read(kChunkBytes);
    if (piece.isEmpty()) {
        m_file.close();
        endJob(false, QStringLiteral("cannot read %1").arg(s.local));
        return;
    }
    post(QStringLiteral("api/upload.php?action=chunk&id=%1&offset=%2").arg(id).arg(offset), piece, "application/octet-stream",
         [this, index, id](QNetworkReply *r) {
             // (409: the server had a different amount - go on from there)
             const qint64 received = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("received")).toInteger(-1);
             if (received < 0) {
                 m_file.close();
                 endJob(false, QStringLiteral("the server did not take a piece"));
                 return;
             }
             setProgress(double(m_jobDone + received) / double(std::max<qint64>(1, m_jobBytes)));
             uploadPiece(index, id, received);
         },
         true);
}

// 3. the item, its files on the server now
void MediaSync::sendItem()
{
    QJsonArray files;
    for (const FileStep &s : std::as_const(m_steps))
        files.append(QJsonObject { { QStringLiteral("name"), s.file.name }, { QStringLiteral("hash"), s.file.hash } });
    m_uploadItem.insert(QStringLiteral("files"), files);
    post(QStringLiteral("api/media.php"), QJsonDocument(m_uploadItem).toJson(QJsonDocument::Compact), "application/json",
         [this](QNetworkReply *r) {
             const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
             const ServerMedia m = mediaFromJson(o.value(QStringLiteral("item")).toObject());
             if (QUuid(m.uuid).isNull()) {
                 endJob(false, QStringLiteral("the server did not take the route"));
                 return;
             }
             m_db->storeServerMedia(m);
             m_db->setServerMediaLocalPath(m.uuid, m_jobTarget);
             m_media = m_db->serverMedia();
             endJob(true);
         });
}

} // namespace watt
