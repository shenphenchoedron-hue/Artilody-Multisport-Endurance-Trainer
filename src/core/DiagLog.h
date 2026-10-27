#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace watt {

class Database;

// Automatic diagnostic trace log ("Logs" folder in the app data dir), written
// so a rider can fetch it afterwards (scp / USB export) instead of watching a
// terminal on the handlebar.
//
//   folder   WATT_LOG_DIR, else QStandardPaths::AppDataLocation + "/logs":
//            Raspberry Pi / Linux ~/.local/share/Artilody/Watt Trainer/logs
//            macOS ~/Library/Application Support/Artilody/Watt Trainer/logs
//            Windows %APPDATA%/Artilody/Watt Trainer/logs
//   files    one per app session: artilody-YYYYMMDD-HHMMSS.log (a session
//            larger than kMaxFileBytes continues in "...-2.log"); ride
//            markers inside. Before a new file opens, the oldest files are
//            deleted so at most kMaxFiles files / kMaxTotalBytes remain.
//   format   plain UTF-8 text, one line per event:
//            "2026-10-27 19:46:40.123 FTMS  CP write 11 00 00 ..."
//   writer   write() only formats and appends to a memory buffer (mutex);
//            a dedicated thread writes it every kFlushMs (or at 256 KB),
//            flushes and fsyncs, so BLE handling never waits for the SD card
//            and at most ~0.5 s is lost on a power cut.
//   off      WATT_LOG=0 (tests) or the Settings toggle: write() is a no-op.
class DiagLog
{
public:
    static constexpr int kMaxFiles = 20;
    static constexpr qint64 kMaxTotalBytes = 50LL * 1024 * 1024;
    static constexpr qint64 kMaxFileBytes = 20LL * 1024 * 1024;
    static constexpr int kFlushMs = 500;
    static constexpr int kBufferLimit = 256 * 1024;

    static DiagLog &instance();

    // WATT_LOG_DIR or <AppDataLocation>/logs (needs the app/org names set).
    static QString defaultDirectory();
    // WATT_LOG=0
    static bool disabledByEnvironment();
    // "artilody-20261027-194640.log"
    static QString fileNameFor(const QDateTime &t, int part = 1);
    // "11 00 00 dc fe 28 27"
    static QString hex(const QByteArray &data);
    // Deletes the oldest artilody-*.log files of dir until at most maxFiles
    // remain and their total is <= maxBytes (`keep` is never deleted).
    // Returns the number of deleted files.
    static int rotate(const QString &dir, int maxFiles = kMaxFiles, qint64 maxBytes = kMaxTotalBytes,
                      const QString &keep = QString());
    // artilody-*.log in dir, newest first (full paths).
    static QStringList logFiles(const QString &dir);
    // Copies the log files of dir into targetDir (existing copies are
    // replaced). Returns the number copied, -1 + error on failure.
    static int exportFiles(const QString &dir, const QString &targetDir, QString *error = nullptr);

    // Opens a new session file in dir (default: defaultDirectory()), rotating
    // first, and writes the header (app / Qt / OS / device). false (and
    // inactive) when disabled by WATT_LOG=0 or the folder is not writable.
    bool start(const QString &dir = QString());
    void stop();   // writes everything pending, closes the file
    bool isActive() const { return m_active.load(std::memory_order_relaxed); }
    QString directory() const;
    QString currentFile() const;

    // One line "time CAT  text"; cheap no-op while inactive. Thread-safe.
    static void write(const char *category, const QString &text);
    void append(const char *category, const QString &text);
    // Writes the buffer now (tests; fatal messages).
    void flush();

    // qWarning / qCritical / qFatal also go to the file; the previous
    // handler still prints to the console.
    static void installMessageHandler();

    // "Raspberry Pi 4 Model B Rev 1.4" from /proc/device-tree/model, else
    // the OS's product name / machine.
    static QString deviceModel();

    ~DiagLog();

private:
    DiagLog() = default;
    void writerLoop();
    void writeOut(const QByteArray &data);   // writer thread / locked callers
    bool openFile(const QString &path);
    void writeHeader();

    mutable QMutex m_stateMutex;   // m_dir, m_path
    QString m_dir;
    QString m_path;
    QDateTime m_sessionStart;
    int m_part = 1;

    std::atomic<bool> m_active { false };
    std::mutex m_bufMutex;
    std::condition_variable m_wake;
    QByteArray m_buffer;
    bool m_stopRequested = false;
    std::unique_ptr<std::thread> m_thread;

    std::mutex m_fileMutex;   // the file (writer thread, flush(), stop())
    std::unique_ptr<QFile> m_file;
};

// QML "Logs": the Settings / diagnostics side of DiagLog. enabled is saved
// in the setting "diag/logEnabled" (default on).
class LogManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool available READ available CONSTANT)          // false with WATT_LOG=0
    Q_PROPERTY(QString directory READ directory NOTIFY changed)
    Q_PROPERTY(QUrl folderUrl READ folderUrl NOTIFY changed)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY changed)
    Q_PROPERTY(QString scpCommand READ scpCommand NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)      // "7 files, 3.2 MB"

public:
    explicit LogManager(Database *db, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool available() const { return !DiagLog::disabledByEnvironment(); }
    QString directory() const { return m_dir; }
    QUrl folderUrl() const { return QUrl::fromLocalFile(m_dir); }
    QString currentFile() const { return DiagLog::instance().currentFile(); }
    // "scp pi@raspberrypi:'~/.local/share/Artilody/Watt Trainer/logs/*.log' ."
    QString scpCommand() const;
    QString summary() const;

    // Creates the folder (for "Open logs folder").
    Q_INVOKABLE bool ensureFolder() const;
    // Copies the log files into folder (FolderDialog URL or a path).
    // Returns a message for a toast.
    Q_INVOKABLE QString exportTo(const QVariant &folder);
    Q_INVOKABLE void refresh() { emit changed(); }

signals:
    void changed();

private:
    Database *m_db;
    QString m_dir;
    bool m_enabled = true;
};

} // namespace watt
