#pragma once

#include <QFile>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "PlanTypes.h"

class QNetworkReply;

namespace watt {

class Database;
class RlvLibrary;
class ServerConnection;

// The server's media library on this station (docs/server-plan.md, step 4).
// Whenever the server answers (ServerConnection::answered) the list is
// fetched when it changed (api/media.php ?since=<rev>) and kept here
// (server_media), so it shows while the server is off too. Then:
//   - GPX routes this station does not have are fetched by themselves into
//     the GPX library folder (small); a file of the same name with other
//     content (edited here) is never overwritten: it is "conflict", and
//     download() fetches the server's as "<name> (server).gpx",
//   - videos are fetched when asked (download(): several GB) into the video
//     library folder, "<folder>/" with its files - the video first, the .gpx
//     last (only then is it a route); an interrupted download resumes.
//   - workouts (.awt / .json / .zwo) are fetched by themselves too and
//     added to the workout library (setWorkoutImporter: duplicates skipped);
//     training plans (.awplan) when asked (importPlan(): planReady() - the
//     Calendar imports it onto a start date). Both are kept in fileFolder.
//   - upload(): a route of this station's libraries is sent to the server
//     (uploadFile(): a workout / plan file):
//     its files hashed (worker thread), the ones the server lacks uploaded in
//     8 MB pieces (api/upload.php), then the item (api/media.php).
// One transfer at a time (a queue). While a ride runs nothing is fetched by
// itself and the libraries are rescanned only afterwards. Exposed to QML as
// "MediaLibrary".
//
// items: [{ uuid, kind ("gpx" / "video" / "workout" / "plan"), name, sizeBytes,
//           sizeText, uploadedBy, localPath,
//           state: "local" | "available" | "conflict" | "queued" | "downloading" }]
class MediaSync : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)        // "off", "idle", "syncing", "error"
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool serverThere READ serverThere NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool transferring READ transferring NOTIFY transferChanged)
    Q_PROPERTY(QString transferKind READ transferKind NOTIFY transferChanged)   // "download", "upload", "checking"
    Q_PROPERTY(QString transferName READ transferName NOTIFY transferChanged)
    Q_PROPERTY(double transferProgress READ transferProgress NOTIFY transferChanged)   // 0..1
    Q_PROPERTY(int queued READ queued NOTIFY transferChanged)

public:
    static constexpr qint64 kChunkBytes = 8 * 1024 * 1024;        // (the server's MEDIA_CHUNK_MAX_BYTES)
    static constexpr qint64 kVerifyMaxBytes = 64 * 1024 * 1024;   // fetched files up to this size: sha1 checked
    static constexpr int kTimeoutMs = 30000;                      // no data for this long: given up

    MediaSync(Database *db, RlvLibrary *gpxRoutes, RlvLibrary *videos, ServerConnection *connection,
              QObject *parent = nullptr);
    ~MediaSync() override;

    // While it returns true (a ride runs) nothing is fetched by itself and the
    // libraries are rescanned only afterwards.
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }
    // where the server's workout and plan files are kept here; a fetched
    // workout file is given to the importer (the workout library)
    void setFileFolder(const QString &dir) { m_fileFolder = dir; }
    void setWorkoutImporter(std::function<int(const QString &path)> import) { m_importWorkout = std::move(import); }

    QString state() const { return m_state; }
    QString message() const { return m_message; }
    bool serverThere() const { return !m_url.isEmpty(); }
    QVariantList items() const { return m_items; }
    bool transferring() const { return m_job.has_value(); }
    QString transferKind() const { return m_transferKind; }
    QString transferName() const { return m_job ? m_job->name : QString(); }
    double transferProgress() const { return m_progress; }
    int queued() const { return m_queue.size(); }

    // Fetches an item (a video, or a GPX route in conflict: as a copy).
    Q_INVOKABLE bool download(const QString &uuid);
    // Sends a route of the libraries to the server: kind "gpx" / "video", path
    // as in the library (its .gpx / .xml). False with message when it cannot.
    Q_INVOKABLE bool upload(const QString &path, const QString &kind);
    // Sends a workout / plan file to the server (kind "workout" / "plan"; name
    // shown there). False with message when it cannot.
    Q_INVOKABLE bool uploadFile(const QString &fileOrUrl, const QString &kind, const QString &name);
    // A new file to write a workout / plan into before uploadFile() ("" on
    // error); removed once the item is on the server.
    Q_INVOKABLE QString outgoingFile(const QString &name, const QString &suffix);
    // Is an item of this kind and name on the server?
    Q_INVOKABLE bool hasItem(const QString &kind, const QString &name) const;
    // A training plan of the server: fetched (when not here yet), then planReady().
    Q_INVOKABLE bool importPlan(const QString &uuid);
    // A workout of the server into the workout library, when asked (e.g. once
    // deleted there): fetched when not here yet, otherwise added from the copy
    // here (one already in the library is not added twice). transferFinished().
    Q_INVOKABLE bool addWorkout(const QString &uuid);
    Q_INVOKABLE void cancel();   // the transfer under way and the queue
    // The route (library path) is on the server.
    Q_INVOKABLE bool onServer(const QString &path) const;
    // Where the items are on this station, looked for again.
    Q_INVOKABLE void refreshLocal();

    void serverAnswered(const QString &baseUrl, const QString &serverId, qint64 mediaRev);
    void serverGone();

signals:
    void changed();
    void itemsChanged();
    void transferChanged();
    // a transfer ended: ok, what ("Menglonnais downloaded"), why not
    void transferFinished(bool ok, const QString &what, const QString &error);
    // a training plan of the server is here to import (importPlan())
    void planReady(const QString &path, const QString &name);

private:
    struct Job
    {
        enum Type { Download, Upload } type = Download;
        QString uuid;              // download
        bool copy = false;         // download a GPX route in conflict as "<name> (server).gpx"
        QString path, kind;        // upload (a route: its library path; a workout / plan: the file)
        QString name;              // shown
        bool automatic = false;    // (a GPX route / workout fetched by itself)
        bool openPlan = false;     // (a plan: planReady() when here)
    };
    struct FileStep              // one file of the job under way
    {
        QString local;             // its path here
        ServerMediaFile file;
    };

    void fetchList();
    void applyList(const QJsonObject &o);
    void queueAutomatic();
    void enqueue(const Job &job);
    void startNext();
    void endJob(bool ok, const QString &error = {});
    // downloads
    void startDownload();
    void downloadFile(int index);
    void finishFile(int index);
    // uploads
    void startUpload();
    void startHashing(const QStringList &paths);
    void uploadFile(int index);
    void uploadPiece(int index, const QString &id, qint64 offset);
    void sendItem();

    void post(const QString &path, const QByteArray &body, const QByteArray &type,
              std::function<void(QNetworkReply *)> done, bool allowConflict = false);
    void rebuildItems();
    void scheduleRescan(RlvLibrary *library);
    void rescanLibraries();
    void setState(const QString &state, const QString &message = {});
    void setProgress(double p);
    std::optional<ServerMedia> item(const QString &uuid) const;
    QString gpxTarget(const ServerMedia &m, bool copy) const;   // "" when a copy would have no name
    // a workout / plan: its place in m_fileFolder ("" when its uuid / file type is not one it can be)
    QString fileTarget(const ServerMedia &m) const;
    QString outgoingDir() const;                                // outgoingFile()'s folder
    static bool isFileKind(const QString &kind) { return kind == QLatin1String("workout") || kind == QLatin1String("plan"); }
    bool localValid(const ServerMedia &m) const;
    bool busy() const { return m_busy && m_busy(); }

    Database *m_db;
    QPointer<RlvLibrary> m_gpx, m_videos;
    std::function<bool()> m_busy;
    QString m_fileFolder;
    std::function<int(const QString &)> m_importWorkout;
    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;

    QString m_url, m_serverId;
    bool m_fetching = false;
    QString m_state = QStringLiteral("off"), m_message;
    QVector<ServerMedia> m_media;
    QVariantList m_items;

    QList<Job> m_queue;
    std::optional<Job> m_job;
    QString m_transferKind;
    double m_progress = 0;
    // the job under way
    QVector<FileStep> m_steps;
    QString m_jobTarget;          // the gpx file / video folder it goes to
    qint64 m_jobBytes = 0, m_jobDone = 0;
    QFile m_file;
    qint64 m_fileOffset = 0;
    bool m_statusChecked = false;
    QJsonObject m_uploadItem;
    // hashing (upload) in a worker thread
    QPointer<QThread> m_hashThread;
    std::shared_ptr<std::atomic<bool>> m_hashCancel;
    std::shared_ptr<std::atomic<qint64>> m_hashDone;
    QTimer m_hashPoll;
    QTimer m_rescanTimer;
    bool m_rescanGpx = false, m_rescanVideos = false;
};

} // namespace watt
