#include "DiagLog.h"

#include "Database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>
#include <chrono>
#include <cstdio>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace watt {

namespace {
const QString kSettingEnabled = QStringLiteral("diag/logEnabled");
const QString kPrefix = QStringLiteral("artilody-");

QtMessageHandler g_previousHandler = nullptr;
thread_local bool t_inHandler = false;

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (!t_inHandler) {
        t_inHandler = true;
        const char *cat = nullptr;
        switch (type) {
        case QtWarningMsg: cat = "WARN"; break;
        case QtCriticalMsg: cat = "CRIT"; break;
        case QtFatalMsg: cat = "FATAL"; break;
        default: break;
        }
        if (cat) {
            const QString where = ctx.category && qstrcmp(ctx.category, "default") != 0
                ? QStringLiteral("[%1] ").arg(QString::fromLatin1(ctx.category)) : QString();
            DiagLog::write(cat, where + msg);
            if (type == QtFatalMsg)
                DiagLog::instance().flush();
        }
        t_inHandler = false;
    }
    if (g_previousHandler)
        g_previousHandler(type, ctx, msg);
    else
        std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, ctx, msg)));
}

bool isLogName(const QString &name)
{
    return name.startsWith(kPrefix) && name.endsWith(QLatin1String(".log"));
}
} // namespace

DiagLog &DiagLog::instance()
{
    static DiagLog log;
    return log;
}

DiagLog::~DiagLog()
{
    stop();
}

QString DiagLog::defaultDirectory()
{
    const QString env = qEnvironmentVariable("WATT_LOG_DIR");
    if (!env.isEmpty())
        return QDir::cleanPath(env);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs"));
}

bool DiagLog::disabledByEnvironment()
{
    return qEnvironmentVariableIsSet("WATT_LOG") && qEnvironmentVariable("WATT_LOG").trimmed() == QLatin1String("0");
}

QString DiagLog::fileNameFor(const QDateTime &t, int part)
{
    QString n = kPrefix + t.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    if (part > 1)
        n += QStringLiteral("-%1").arg(part);
    return n + QStringLiteral(".log");
}

QString DiagLog::hex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' '));
}

QStringList DiagLog::logFiles(const QString &dir)
{
    // newest first: "artilody-YYYYMMDD-HHMMSS[-N].log" sorts by the time in
    // the name, then the part N; other names by modification time
    struct Entry
    {
        QString path, stamp;
        int part = 1;
        QDateTime modified;
    };
    QList<Entry> entries;
    const QFileInfoList infos = QDir(dir).entryInfoList({ kPrefix + QStringLiteral("*.log") }, QDir::Files);
    for (const QFileInfo &i : infos) {
        if (!isLogName(i.fileName()))
            continue;
        Entry e { i.absoluteFilePath(), QString(), 1, i.lastModified() };
        const QString base = i.completeBaseName().mid(kPrefix.size());   // YYYYMMDD-HHMMSS[-N]
        const QStringList parts = base.split(QLatin1Char('-'));
        if (parts.size() >= 2 && parts.at(0).size() == 8 && parts.at(1).size() == 6) {
            e.stamp = parts.at(0) + parts.at(1);
            if (parts.size() >= 3)
                e.part = std::max(1, parts.at(2).toInt());
        }
        entries << e;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (!a.stamp.isEmpty() && !b.stamp.isEmpty()) {
            if (a.stamp != b.stamp)
                return a.stamp > b.stamp;
            if (a.part != b.part)
                return a.part > b.part;
        }
        if (a.modified != b.modified)
            return a.modified > b.modified;
        return a.path > b.path;
    });
    QStringList out;
    for (const Entry &e : std::as_const(entries))
        out << e.path;
    return out;
}

int DiagLog::rotate(const QString &dir, int maxFiles, qint64 maxBytes, const QString &keep)
{
    const QStringList files = logFiles(dir);   // newest first
    const QString keepAbs = keep.isEmpty() ? QString() : QFileInfo(keep).absoluteFilePath();
    int kept = 0, deleted = 0;
    qint64 total = 0;
    for (const QString &f : files) {
        const qint64 size = QFileInfo(f).size();
        const bool isKeep = f == keepAbs;
        if (isKeep || (kept < maxFiles && total + size <= maxBytes)) {
            ++kept;
            total += size;
            continue;
        }
        if (QFile::remove(f))
            ++deleted;
    }
    return deleted;
}

int DiagLog::exportFiles(const QString &dir, const QString &targetDir, QString *error)
{
    if (targetDir.isEmpty() || !QDir().mkpath(targetDir)) {
        if (error)
            *error = QStringLiteral("Cannot write to %1").arg(targetDir);
        return -1;
    }
    if (QFileInfo(targetDir).canonicalFilePath() == QFileInfo(dir).canonicalFilePath()) {
        if (error)
            *error = QStringLiteral("Choose another folder than the logs folder itself");
        return -1;
    }
    instance().flush();
    int n = 0;
    for (const QString &f : logFiles(dir)) {
        const QString to = QDir(targetDir).filePath(QFileInfo(f).fileName());
        QFile::remove(to);
        if (!QFile::copy(f, to)) {
            if (error)
                *error = QStringLiteral("Could not copy %1 to %2").arg(QFileInfo(f).fileName(), targetDir);
            return -1;
        }
        ++n;
    }
    return n;
}

QString DiagLog::deviceModel()
{
    QFile f(QStringLiteral("/proc/device-tree/model"));
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray m = f.readAll();
        while (!m.isEmpty() && (m.endsWith('\0') || m.endsWith('\n')))
            m.chop(1);
        if (!m.isEmpty())
            return QString::fromUtf8(m);
    }
    return QSysInfo::machineHostName().isEmpty() ? QSysInfo::currentCpuArchitecture()
                                                 : QStringLiteral("%1 (%2)").arg(QSysInfo::productType(),
                                                                                 QSysInfo::currentCpuArchitecture());
}

QString DiagLog::directory() const
{
    QMutexLocker l(&m_stateMutex);
    return m_dir;
}

QString DiagLog::currentFile() const
{
    QMutexLocker l(&m_stateMutex);
    return m_path;
}

bool DiagLog::openFile(const QString &path)
{
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    std::lock_guard<std::mutex> fl(m_fileMutex);
    m_file = std::move(file);
    QMutexLocker l(&m_stateMutex);
    m_path = path;
    return true;
}

bool DiagLog::start(const QString &dirIn)
{
    stop();
    if (disabledByEnvironment())
        return false;
    const QString dir = dirIn.isEmpty() ? defaultDirectory() : QDir::cleanPath(dirIn);
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return false;
    const QDateTime now = QDateTime::currentDateTime();
    QString path = QDir(dir).filePath(fileNameFor(now));
    for (int i = 2; QFileInfo::exists(path) && i < 100; ++i)   // two sessions within a second
        path = QDir(dir).filePath(fileNameFor(now, i));
    rotate(dir, kMaxFiles - 1, kMaxTotalBytes);   // room for the new file
    if (!openFile(path))
        return false;
    {
        QMutexLocker l(&m_stateMutex);
        m_dir = dir;
        m_sessionStart = now;
        m_part = 1;
    }
    {
        std::lock_guard<std::mutex> bl(m_bufMutex);
        m_stopRequested = false;
        m_buffer.clear();
    }
    m_active.store(true);
    writeHeader();
    m_thread = std::make_unique<std::thread>([this] { writerLoop(); });
    return true;
}

void DiagLog::writeHeader()
{
    append("LOG", QStringLiteral("==== Artilody Multisport Endurance Trainer %1 – diagnostic log started ====")
                      .arg(QCoreApplication::applicationVersion()));
    append("SYS", QStringLiteral("Qt %1 (built with %2)  OS %3  kernel %4 %5  arch %6")
                      .arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR), QSysInfo::prettyProductName(),
                           QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture()));
    append("SYS", QStringLiteral("Device: %1").arg(deviceModel()));
    append("SYS", QStringLiteral("Log file: %1").arg(QDir::toNativeSeparators(currentFile())));
}

void DiagLog::stop()
{
    if (!m_thread) {
        m_active.store(false);
        return;
    }
    append("LOG", QStringLiteral("==== diagnostic log closed ===="));
    m_active.store(false);
    {
        std::lock_guard<std::mutex> bl(m_bufMutex);
        m_stopRequested = true;
    }
    m_wake.notify_all();
    m_thread->join();
    m_thread.reset();
    flush();   // anything appended after the thread's last round
    std::lock_guard<std::mutex> fl(m_fileMutex);
    if (m_file)
        m_file->close();
    m_file.reset();
}

void DiagLog::write(const char *category, const QString &text)
{
    DiagLog &log = instance();
    if (!log.isActive())
        return;
    log.append(category, text);
}

void DiagLog::append(const char *category, const QString &text)
{
    if (!isActive())
        return;
    QByteArray line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz ")).toLatin1();
    QByteArray cat(category ? category : "-");
    cat = cat.leftJustified(6, ' ');
    line += cat;
    QString t = text;
    t.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    line += t.toUtf8();
    line += '\n';
    bool wake = false;
    {
        std::lock_guard<std::mutex> bl(m_bufMutex);
        m_buffer += line;
        wake = m_buffer.size() >= kBufferLimit;
    }
    if (wake)
        m_wake.notify_one();
}

void DiagLog::writerLoop()
{
    for (;;) {
        QByteArray chunk;
        bool stopping = false;
        {
            std::unique_lock<std::mutex> bl(m_bufMutex);
            m_wake.wait_for(bl, std::chrono::milliseconds(kFlushMs),
                            [this] { return m_stopRequested || m_buffer.size() >= kBufferLimit; });
            chunk.swap(m_buffer);
            stopping = m_stopRequested;
        }
        if (!chunk.isEmpty()) {
            std::lock_guard<std::mutex> fl(m_fileMutex);
            writeOut(chunk);
        }
        if (stopping)
            return;
    }
}

void DiagLog::writeOut(const QByteArray &data)
{
    // m_fileMutex held
    if (!m_file || !m_file->isOpen())
        return;
    m_file->write(data);
    m_file->flush();
#ifdef Q_OS_UNIX
    ::fsync(m_file->handle());   // survive a power cut (the Pi is often just switched off)
#endif
    if (m_file->size() < kMaxFileBytes)
        return;
    // continue in "<session>-N.log"
    QString dir;
    QDateTime start;
    int part = 0;
    {
        QMutexLocker l(&m_stateMutex);
        dir = m_dir;
        start = m_sessionStart;
        part = ++m_part;
    }
    const QString next = QDir(dir).filePath(fileNameFor(start, part));
    m_file->write(QStringLiteral("(continued in %1)\n").arg(QFileInfo(next).fileName()).toUtf8());
    m_file->close();
    auto file = std::make_unique<QFile>(next);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    m_file = std::move(file);
    {
        QMutexLocker l(&m_stateMutex);
        m_path = next;
    }
    rotate(dir, kMaxFiles, kMaxTotalBytes, next);
}

void DiagLog::flush()
{
    QByteArray chunk;
    {
        std::lock_guard<std::mutex> bl(m_bufMutex);
        chunk.swap(m_buffer);
    }
    std::lock_guard<std::mutex> fl(m_fileMutex);
    if (!chunk.isEmpty())
        writeOut(chunk);
}

void DiagLog::installMessageHandler()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    g_previousHandler = qInstallMessageHandler(messageHandler);
}

// --- LogManager ----------------------------------------------------------------

LogManager::LogManager(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_dir(DiagLog::defaultDirectory())
{
    m_enabled = m_db ? m_db->setting(kSettingEnabled, true).toBool() : true;
}

void LogManager::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (m_db)
        m_db->setSetting(kSettingEnabled, on);
    if (on) {
        DiagLog::instance().start(m_dir);
        DiagLog::write("LOG", QStringLiteral("Diagnostic log turned on in Settings"));
    } else {
        DiagLog::write("LOG", QStringLiteral("Diagnostic log turned off in Settings"));
        DiagLog::instance().stop();
    }
    emit changed();
}

QString LogManager::scpCommand() const
{
    // Relative to the home folder, "?" for the space of "Watt Trainer" and
    // single quotes: works with the old scp protocol (remote shell) and the
    // SFTP mode of OpenSSH 9, and in zsh / bash on the Mac.
    QString dir = QDir::cleanPath(m_dir);
    const QString home = QDir::homePath();
    if (dir.startsWith(home + QLatin1Char('/')))
        dir = dir.mid(home.size() + 1);
    dir.replace(QLatin1Char(' '), QLatin1Char('?'));
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = QStringLiteral("pi");
    QString host = QSysInfo::machineHostName();
    if (host.isEmpty())
        host = QStringLiteral("raspberrypi");
    if (!host.contains(QLatin1Char('.')))
        host += QStringLiteral(".local");
    return QStringLiteral("scp '%1@%2:%3/*.log' .").arg(user, host, dir);
}

QString LogManager::summary() const
{
    const QStringList files = DiagLog::logFiles(m_dir);
    qint64 total = 0;
    for (const QString &f : files)
        total += QFileInfo(f).size();
    const double mb = total / (1024.0 * 1024.0);
    return QStringLiteral("%1 file%2, %3")
        .arg(files.size())
        .arg(files.size() == 1 ? QString() : QStringLiteral("s"))
        .arg(mb >= 0.1 ? QStringLiteral("%1 MB").arg(mb, 0, 'f', 1) : QStringLiteral("%1 KB").arg(total / 1024.0, 0, 'f', 0));
}

bool LogManager::ensureFolder() const
{
    return QDir().mkpath(m_dir);
}

QString LogManager::exportTo(const QVariant &folder)
{
    QString path;
    if (folder.canConvert<QUrl>() && folder.toUrl().isLocalFile())
        path = folder.toUrl().toLocalFile();
    else
        path = folder.toString();
    if (path.startsWith(QLatin1String("file:")))
        path = QUrl(path).toLocalFile();
    QString error;
    const int n = DiagLog::exportFiles(m_dir, path, &error);
    emit changed();
    if (n < 0)
        return error;
    DiagLog::write("LOG", QStringLiteral("Exported %1 log files to %2").arg(n).arg(path));
    return n == 0 ? QStringLiteral("No log files to export")
                  : QStringLiteral("Copied %1 log file%2 to %3").arg(n).arg(n == 1 ? QString() : QStringLiteral("s"),
                                                                       QDir::toNativeSeparators(path));
}

} // namespace watt
