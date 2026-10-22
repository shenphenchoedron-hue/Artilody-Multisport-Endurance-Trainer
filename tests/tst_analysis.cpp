#include "CsvExporter.h"
#include "RideAnalysis.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

class TestAnalysis : public QObject
{
    Q_OBJECT
private slots:
    void constantPower()
    {
        QVector<Sample> s;
        for (int t = 0; t < 3600; ++t)
            s.append({ t, 200, 200, 90, 140 });
        const RideStats st = analysis::analyze(s, 200);
        QCOMPARE(st.durationSec, 3600);
        QCOMPARE(st.avgPower, 200.0);
        QCOMPARE(st.maxPower, 200);
        QVERIFY(qAbs(st.normalizedPower - 200.0) < 0.01);
        QVERIFY(qAbs(st.intensityFactor - 1.0) < 0.001);
        QVERIFY(qAbs(st.tss - 100.0) < 0.1);   // 1 h at FTP = 100 TSS
        QCOMPARE(st.kilojoules, 720.0);
        QCOMPARE(st.avgHeartRate, 140.0);
        QCOMPARE(st.avgCadence, 90.0);
        QCOMPARE(st.zoneSeconds[4], 3600);       // 100% FTP => Z5 threshold
    }

    void variablePowerRaisesNp()
    {
        QVector<int> p;
        for (int i = 0; i < 1200; ++i)
            p.append((i / 60) % 2 ? 300 : 100);   // 1 min on / 1 min off
        const double np = analysis::normalizedPower(p);
        QVERIFY(np > 200.0);   // average is 200 W, NP must be higher
        QVERIFY(np < 300.0);
    }

    void ignoresZeroHrAndCadence()
    {
        QVector<Sample> s { { 0, 100, 0, 0, 0 }, { 1, 100, 0, 80, 120 }, { 2, 100, 0, 90, 0 } };
        const RideStats st = analysis::analyze(s, 250);
        QCOMPARE(st.avgHeartRate, 120.0);
        QCOMPARE(st.avgCadence, 85.0);
        QCOMPARE(st.maxHeartRate, 120);
    }

    void emptyRide()
    {
        const RideStats st = analysis::analyze({}, 250);
        QCOMPARE(st.durationSec, 0);
        QCOMPARE(st.avgPower, 0.0);
    }

    void bestAveragePower()
    {
        QVector<Sample> s;
        for (int t = 0; t < 300; ++t)
            s.append({ t, t >= 100 && t < 160 ? 400 : 100, 0, 90, 0 });
        QCOMPARE(analysis::bestAveragePower(s, 60), 400);
        QCOMPARE(analysis::bestAveragePower(s, 1), 400);
        QCOMPARE(analysis::bestAveragePower(s, 120), 250);    // 60 s @ 400 + 60 s @ 100
        QCOMPARE(analysis::bestAveragePower(s, 301), 0);      // window longer than the ride
        QCOMPARE(analysis::bestAveragePower(s, 0), 0);
        QCOMPARE(analysis::bestAveragePower({}, 60), 0);
        // negative readings count as 0
        QVector<Sample> neg(60, Sample { 0, -50, 0, 0, 0 });
        QCOMPARE(analysis::bestAveragePower(neg, 60), 0);
    }

    void rampFtpEstimate()
    {
        // 10 min ride with a 1-minute block at 340 W -> 0.75 * 340 = 255 W
        QVector<Sample> s;
        for (int t = 0; t < 600; ++t)
            s.append({ t, t >= 500 && t < 560 ? 340 : 150, 0, 90, 0 });
        QCOMPARE(analysis::estimateFtpFromRamp(s), 255);
        QCOMPARE(analysis::plausibleRampFtp(s), 255);

        // rounding: 0.75 * 333 = 249.75 -> 250
        QVector<Sample> r(60, Sample { 0, 333, 0, 0, 0 });
        QCOMPARE(analysis::estimateFtpFromRamp(r), 250);

        // too little data
        QVector<Sample> shortRide(59, Sample { 0, 400, 0, 0, 0 });
        QCOMPARE(analysis::estimateFtpFromRamp(shortRide), 0);
        QCOMPARE(analysis::estimateFtpFromRamp({}), 0);
        QVector<Sample> zero(600, Sample { 0, 0, 0, 0, 0 });
        QCOMPARE(analysis::estimateFtpFromRamp(zero), 0);

        // estimate exists but the ride is shorter than 5 min -> not plausible
        QCOMPARE(analysis::estimateFtpFromRamp(r), 250);
        QCOMPARE(analysis::plausibleRampFtp(r), 0);
        // outside the valid FTP range (50-600 W) -> not plausible
        QVector<Sample> weak(400, Sample { 0, 40, 0, 0, 0 });
        QCOMPARE(analysis::estimateFtpFromRamp(weak), 30);
        QCOMPARE(analysis::plausibleRampFtp(weak), 0);
        QVector<Sample> huge(400, Sample { 0, 900, 0, 0, 0 });
        QCOMPARE(analysis::plausibleRampFtp(huge), 0);
    }

    void csvExport()
    {
        RideRecord r;
        r.workoutName = QStringLiteral("Test, \"quoted\"");
        r.startedAt = QDateTime(QDate(2026, 10, 21), QTime(18, 0));
        r.ftp = 250;
        r.durationSec = 2;
        const QVector<Sample> s { { 0, 180, 190, 85, 130 }, { 3661, 200, 190, 86, 131 } };
        const QString csv = csv::samplesToCsv(r, s);
        QVERIFY(csv.contains(QStringLiteral("seconds,elapsed,power_w,target_w,cadence_rpm,heart_rate_bpm")));
        QVERIFY(csv.contains(QStringLiteral("0,00:00:00,180,190,85,130")));
        QVERIFY(csv.contains(QStringLiteral("3661,01:01:01,200,190,86,131")));
        QVERIFY(csv.contains(QStringLiteral("\"Test, \"\"quoted\"\"\"")));

        const QString summary = csv::ridesToCsv({ r });
        QCOMPARE(summary.count(QLatin1Char('\n')), 2);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.csv"));
        QString err;
        QVERIFY(csv::writeFile(path, csv, &err));
        QFile f(path);
        // writeFile() uses text mode (CRLF line endings on Windows), so read it back in text mode.
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(f.readAll()), csv);
    }
};

QTEST_GUILESS_MAIN(TestAnalysis)
#include "tst_analysis.moc"
