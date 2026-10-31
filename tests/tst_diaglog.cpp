// DiagLog: the automatic diagnostic log file (writes, flushes, rotation /
// retention, export, message handler, disabled = no-op) and LogManager.
#include "Database.h"
#include "DiagLog.h"

#include <QtTest>
#include <QTemporaryDir>

using namespace watt;

namespace {
QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

void makeFile(const QString &dir, const QString &name, qint64 bytes)
{
    QFile f(QDir(dir).filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x'));
}
} // namespace

class TestDiagLog : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        qunsetenv("WATT_LOG");
        qunsetenv("WATT_LOG_DIR");
        DiagLog::instance().stop();
    }
    void cleanup()
    {
        DiagLog::instance().stop();
        qunsetenv("WATT_LOG");
        qunsetenv("WATT_LOG_DIR");
    }

    void namesAndHex()
    {
        const QDateTime t(QDate(2026, 10, 27), QTime(19, 46, 40));
        QCOMPARE(DiagLog::fileNameFor(t), QStringLiteral("artilody-20261027-194640.log"));
        QCOMPARE(DiagLog::fileNameFor(t, 2), QStringLiteral("artilody-20261027-194640-2.log"));
        QCOMPARE(DiagLog::hex(QByteArray::fromHex("11000024ff2827")), QStringLiteral("11 00 00 24 ff 28 27"));
        QCOMPARE(DiagLog::hex(QByteArray()), QString());
    }

    void defaultDirectory()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("Artilody"));
        QCoreApplication::setApplicationName(QStringLiteral("Watt Trainer"));
        const QString d = DiagLog::defaultDirectory();
        QVERIFY2(d.endsWith(QStringLiteral("Artilody/Watt Trainer/logs")), qPrintable(d));
        qputenv("WATT_LOG_DIR", "/tmp/some/where/");
        QCOMPARE(DiagLog::defaultDirectory(), QStringLiteral("/tmp/some/where"));
    }

    void writesHeaderAndLinesAndFlushes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9"));
        DiagLog &log = DiagLog::instance();
        QVERIFY(log.start(tmp.path()));
        QVERIFY(log.isActive());
        QCOMPARE(log.directory(), QDir::cleanPath(tmp.path()));
        const QString file = log.currentFile();
        QVERIFY(QFileInfo(file).fileName().startsWith(QStringLiteral("artilody-")));
        DiagLog::write("FTMS", QStringLiteral("CP write [05 96 00] = 0x05 Set Target Power: 150 W"));
        DiagLog::write("TEST", QStringLiteral("two\nlines æøå"));
        // the writer thread flushes within kFlushMs without any call
        QTRY_VERIFY_WITH_TIMEOUT(readAll(file).contains(QStringLiteral("Set Target Power: 150 W")), 3000);
        const QString text = readAll(file);
        QVERIFY2(text.contains(QStringLiteral("Artilody Multisport Endurance Trainer 9.9.9")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("SYS   Qt ")));
        QVERIFY(text.contains(QStringLiteral("Device: ")));
        QVERIFY(text.contains(QStringLiteral("two | lines æøå")));   // one line per event, UTF-8
        // "yyyy-MM-dd HH:mm:ss.zzz CAT   text"
        const QRegularExpression line(QStringLiteral(R"(^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} FTMS  CP write)"),
                                      QRegularExpression::MultilineOption);
        QVERIFY2(line.match(text).hasMatch(), qPrintable(text));
        // flush() writes at once
        DiagLog::write("TEST", QStringLiteral("immediately"));
        log.flush();
        QVERIFY(readAll(file).contains(QStringLiteral("immediately")));
        log.stop();
        QVERIFY(!log.isActive());
        QVERIFY(readAll(file).contains(QStringLiteral("diagnostic log closed")));
        // after stop: no-op
        DiagLog::write("TEST", QStringLiteral("after stop"));
        log.flush();
        QVERIFY(!readAll(file).contains(QStringLiteral("after stop")));
    }

    void newFilePerSession()
    {
        QTemporaryDir tmp;
        DiagLog &log = DiagLog::instance();
        QVERIFY(log.start(tmp.path()));
        const QString a = log.currentFile();
        QVERIFY(log.start(tmp.path()));   // same second: "-2"
        const QString b = log.currentFile();
        QVERIFY(a != b);
        log.stop();
        QCOMPARE(DiagLog::logFiles(tmp.path()).size(), 2);
        QCOMPARE(DiagLog::logFiles(tmp.path()).first(), b);   // newest first
    }

    void rotatesByCountAndSize()
    {
        QTemporaryDir tmp;
        // 25 old sessions
        for (int i = 0; i < 25; ++i)
            makeFile(tmp.path(), QStringLiteral("artilody-202601%1-120000.log").arg(i + 1, 2, 10, QLatin1Char('0')), 100);
        makeFile(tmp.path(), QStringLiteral("notes.txt"), 10);   // never touched
        QCOMPARE(DiagLog::rotate(tmp.path(), 20, DiagLog::kMaxTotalBytes), 5);
        QStringList left = DiagLog::logFiles(tmp.path());
        QCOMPARE(left.size(), 20);
        QVERIFY(left.first().endsWith(QStringLiteral("artilody-20260125-120000.log")));   // newest kept
        QVERIFY(left.last().endsWith(QStringLiteral("artilody-20260106-120000.log")));
        QVERIFY(QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral("notes.txt"))));
        // by size: 1000 bytes allowed -> the newest 10 files of 100 bytes
        QCOMPARE(DiagLog::rotate(tmp.path(), 20, 1000), 10);
        QCOMPARE(DiagLog::logFiles(tmp.path()).size(), 10);
        // start(): room for the new file, at most kMaxFiles in total
        for (int i = 0; i < 25; ++i)
            makeFile(tmp.path(), QStringLiteral("artilody-202502%1-080000.log").arg(i + 1, 2, 10, QLatin1Char('0')), 10);
        QVERIFY(DiagLog::instance().start(tmp.path()));
        DiagLog::instance().stop();
        left = DiagLog::logFiles(tmp.path());
        QCOMPARE(left.size(), DiagLog::kMaxFiles);
        QVERIFY(left.first().endsWith(QFileInfo(DiagLog::instance().currentFile()).fileName()));
        // the part of a session sorts after (newer than) its first file
        QTemporaryDir parts;
        makeFile(parts.path(), QStringLiteral("artilody-20260101-120000.log"), 10);
        makeFile(parts.path(), QStringLiteral("artilody-20260101-120000-2.log"), 10);
        QVERIFY(DiagLog::logFiles(parts.path()).first().endsWith(QStringLiteral("-2.log")));
    }

    void disabledIsANoOp()
    {
        QTemporaryDir tmp;
        qputenv("WATT_LOG", "0");
        QVERIFY(DiagLog::disabledByEnvironment());
        QVERIFY(!DiagLog::instance().start(tmp.path()));
        QVERIFY(!DiagLog::instance().isActive());
        DiagLog::write("TEST", QStringLiteral("nothing"));   // no crash, no file
        DiagLog::instance().flush();
        DiagLog::instance().stop();
        QVERIFY(DiagLog::logFiles(tmp.path()).isEmpty());
        qputenv("WATT_LOG", "1");
        QVERIFY(!DiagLog::disabledByEnvironment());
    }

    void unwritableFolder()
    {
        QTemporaryDir tmp;
        makeFile(tmp.path(), QStringLiteral("file"), 1);
        // a FILE where the folder should be
        QVERIFY(!DiagLog::instance().start(QDir(tmp.path()).filePath(QStringLiteral("file"))));
        QVERIFY(!DiagLog::instance().isActive());
        DiagLog::write("TEST", QStringLiteral("dropped"));
    }

    void messageHandlerWritesWarnings()
    {
        QTemporaryDir tmp;
        DiagLog::installMessageHandler();
        QVERIFY(DiagLog::instance().start(tmp.path()));
        qWarning("trainer said %s", "no");
        qCritical("something critical");
        qDebug("debug not in the file");
        DiagLog::instance().flush();
        const QString text = readAll(DiagLog::instance().currentFile());
        QVERIFY2(text.contains(QStringLiteral("WARN  trainer said no")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("CRIT  something critical")));
        QVERIFY(!text.contains(QStringLiteral("debug not in the file")));
    }

    void exportCopiesFiles()
    {
        QTemporaryDir tmp, usb;
        QVERIFY(DiagLog::instance().start(tmp.path()));
        DiagLog::write("TEST", QStringLiteral("export me"));
        makeFile(tmp.path(), QStringLiteral("artilody-20250101-000000.log"), 5);
        QString error;
        const QString target = QDir(usb.path()).filePath(QStringLiteral("watt logs"));
        QCOMPARE(DiagLog::exportFiles(tmp.path(), target, &error), 2);   // flushes the current file first
        QVERIFY(error.isEmpty());
        QCOMPARE(QDir(target).entryList(QDir::Files).size(), 2);
        QVERIFY(readAll(QDir(target).filePath(QFileInfo(DiagLog::instance().currentFile()).fileName()))
                    .contains(QStringLiteral("export me")));
        // again: replaced, not failed
        QCOMPARE(DiagLog::exportFiles(tmp.path(), target, &error), 2);
        // not into the logs folder itself
        QCOMPARE(DiagLog::exportFiles(tmp.path(), tmp.path(), &error), -1);
        QVERIFY(!error.isEmpty());
    }

    void logManager()
    {
        QTemporaryDir tmp;
        qputenv("WATT_LOG_DIR", tmp.path().toUtf8());
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        LogManager logs(&db);
        QVERIFY(logs.enabled());   // default on
        QVERIFY(logs.available());
        QCOMPARE(logs.directory(), QDir::cleanPath(tmp.path()));
        QCOMPARE(logs.folderUrl(), QUrl::fromLocalFile(QDir::cleanPath(tmp.path())));
        QVERIFY2(logs.scpCommand().startsWith(QStringLiteral("scp '")) && logs.scpCommand().endsWith(QStringLiteral("/*.log' .")),
                 qPrintable(logs.scpCommand()));
        QVERIFY(!logs.scpCommand().contains(QLatin1Char(' ') + QStringLiteral("Trainer")));
        QSignalSpy changed(&logs, &LogManager::changed);
        logs.setEnabled(false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!DiagLog::instance().isActive());
        QCOMPARE(db.setting(QStringLiteral("diag/logEnabled")).toBool(), false);
        QVERIFY(!LogManager(&db).enabled());   // saved
        logs.setEnabled(true);
        QVERIFY(DiagLog::instance().isActive());
        QVERIFY(logs.currentFile().startsWith(QDir::cleanPath(tmp.path())));
        QVERIFY(logs.summary().contains(QStringLiteral("file")));
        QTemporaryDir usb;
        QVERIFY(logs.exportTo(QUrl::fromLocalFile(usb.path())).startsWith(QStringLiteral("Copied 1 log file")));
        QVERIFY(logs.exportTo(QString()).startsWith(QStringLiteral("Cannot write")));
    }
};

QTEST_GUILESS_MAIN(TestDiagLog)
#include "tst_diaglog.moc"
