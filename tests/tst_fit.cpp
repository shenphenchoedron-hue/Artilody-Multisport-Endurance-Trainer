#include "FitExporter.h"

#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

namespace {

// Independent bitwise implementation of the FIT CRC-16 (reflected poly 0xA001),
// deliberately different from the nibble-table version in FitExporter.cpp.
quint16 refCrc(const QByteArray &d, int from, int len)
{
    quint16 crc = 0;
    for (int i = from; i < from + len; ++i) {
        crc ^= quint8(d[i]);
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? quint16((crc >> 1) ^ 0xA001) : quint16(crc >> 1);
    }
    return crc;
}

quint64 le(const QByteArray &d, int at, int n)
{
    quint64 v = 0;
    for (int i = n - 1; i >= 0; --i)
        v = (v << 8) | quint8(d[at + i]);
    return v;
}

struct Message
{
    quint16 global = 0;
    QMap<int, quint64> fields;   // field number -> raw value
};

struct FitFile
{
    bool ok = false;
    QString error;
    int headerSize = 0;
    int protocol = 0;
    int profile = 0;
    QVector<Message> messages;

    QVector<Message> of(quint16 global) const
    {
        QVector<Message> out;
        for (const Message &m : messages)
            if (m.global == global)
                out.append(m);
        return out;
    }
};

// Minimal FIT reader: header + CRCs, definition / normal data messages,
// little- and big-endian definitions. No compressed timestamps or developer data.
FitFile parse(const QByteArray &d)
{
    FitFile f;
    auto fail = [&](const QString &e) { f.error = e; return f; };
    if (d.size() < 14)
        return fail(QStringLiteral("too short"));
    f.headerSize = quint8(d[0]);
    if (f.headerSize != 12 && f.headerSize != 14)
        return fail(QStringLiteral("bad header size"));
    f.protocol = quint8(d[1]);
    f.profile = int(le(d, 2, 2));
    const qint64 dataSize = qint64(le(d, 4, 4));
    if (d.mid(8, 4) != QByteArray(".FIT"))
        return fail(QStringLiteral("no .FIT signature"));
    if (f.headerSize == 14) {
        const quint16 hcrc = quint16(le(d, 12, 2));
        if (hcrc != 0 && hcrc != refCrc(d, 0, 12))
            return fail(QStringLiteral("header CRC mismatch"));
    }
    if (d.size() != f.headerSize + dataSize + 2)
        return fail(QStringLiteral("size mismatch"));
    if (refCrc(d, 0, int(f.headerSize + dataSize)) != quint16(le(d, int(f.headerSize + dataSize), 2)))
        return fail(QStringLiteral("file CRC mismatch"));

    struct Def
    {
        bool valid = false;
        bool bigEndian = false;
        quint16 global = 0;
        QVector<QPair<int, int>> fields;   // number, size
    };
    Def defs[16];
    int pos = f.headerSize;
    const int end = int(f.headerSize + dataSize);
    while (pos < end) {
        const quint8 h = quint8(d[pos++]);
        if (h & 0x80)
            return fail(QStringLiteral("compressed timestamp header not expected"));
        const int local = h & 0x0F;
        if (h & 0x40) {
            if (h & 0x20)
                return fail(QStringLiteral("developer data not expected"));
            if (pos + 5 > end)
                return fail(QStringLiteral("truncated definition"));
            Def def;
            def.valid = true;
            def.bigEndian = quint8(d[pos + 1]) == 1;
            def.global = def.bigEndian ? quint16((quint8(d[pos + 2]) << 8) | quint8(d[pos + 3]))
                                       : quint16(le(d, pos + 2, 2));
            const int n = quint8(d[pos + 4]);
            pos += 5;
            if (pos + 3 * n > end)
                return fail(QStringLiteral("truncated field definitions"));
            for (int i = 0; i < n; ++i, pos += 3)
                def.fields.append({ quint8(d[pos]), quint8(d[pos + 1]) });
            defs[local] = def;
        } else {
            const Def &def = defs[local];
            if (!def.valid)
                return fail(QStringLiteral("data message without definition"));
            Message m;
            m.global = def.global;
            for (const auto &fd : def.fields) {
                if (pos + fd.second > end)
                    return fail(QStringLiteral("truncated data message"));
                quint64 v = 0;
                if (fd.second <= 8) {
                    if (def.bigEndian)
                        for (int i = 0; i < fd.second; ++i)
                            v = (v << 8) | quint8(d[pos + i]);
                    else
                        v = le(d, pos, fd.second);
                }
                m.fields.insert(fd.first, v);
                pos += fd.second;
            }
            f.messages.append(m);
        }
    }
    f.ok = true;
    return f;
}

RideRecord makeRide(int seconds)
{
    RideRecord r;
    r.id = 42;
    r.workoutName = QStringLiteral("Sweet Spot");
    r.startedAt = QDateTime::fromSecsSinceEpoch(1773473400).toUTC();   // 2026-03-14T07:30:00Z
    r.durationSec = seconds;
    r.ftp = 250;
    r.avgPower = 201.6;
    r.maxPower = 412;
    r.normalizedPower = 223.4;
    r.avgHeartRate = 141.2;
    r.maxHeartRate = 172;
    r.avgCadence = 88.6;
    r.kilojoules = 725.8;
    r.intensityFactor = 0.894;
    r.tss = 79.9;
    return r;
}

constexpr quint64 kStartFit = 1773473400 - 631065600;   // 2026-03-14T07:30:00Z in FIT epoch

} // namespace

class TestFit : public QObject
{
    Q_OBJECT
private slots:
    void crcMatchesReference()
    {
        const QByteArray d("123456789");
        QCOMPARE(fit::crc16(d), refCrc(d, 0, d.size()));
        QCOMPARE(refCrc(d, 0, d.size()), quint16(0xBB3D));   // CRC-16/ARC check value
    }

    void fullRide()
    {
        QVector<Sample> s;
        for (int t = 0; t < 600; ++t)
            s.append({ t, 150 + t % 100, 200, t % 7 == 0 ? 0 : 90, t < 10 ? 0 : 140 });
        const RideRecord r = makeRide(600);
        const QByteArray data = fit::encodeActivity(r, s);
        const FitFile f = parse(data);
        QVERIFY2(f.ok, qPrintable(f.error));
        QCOMPARE(f.headerSize, 14);
        QCOMPARE(f.profile, 2140);
        QVERIFY(f.protocol == 0x10 || f.protocol == 0x20);

        // message order: file_id first, activity last
        QCOMPARE(f.messages.first().global, quint16(0));
        QCOMPARE(f.messages.last().global, quint16(34));

        const auto fileId = f.of(0);
        QCOMPARE(fileId.size(), 1);
        QCOMPARE(fileId[0].fields.value(0), quint64(4));     // type = activity
        QCOMPARE(fileId[0].fields.value(1), quint64(255));   // manufacturer = development
        QCOMPARE(fileId[0].fields.value(3), quint64(42));    // serial from ride id
        QCOMPARE(fileId[0].fields.value(4), kStartFit);

        const auto events = f.of(21);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].fields.value(1), quint64(0));   // start
        QCOMPARE(events[1].fields.value(1), quint64(4));   // stop_all
        QCOMPARE(events[1].fields.value(253), kStartFit + 600);

        const auto rec = f.of(20);
        QCOMPARE(rec.size(), 600);
        QCOMPARE(rec[0].fields.value(253), kStartFit);
        QCOMPARE(rec[599].fields.value(253), kStartFit + 599);
        QCOMPARE(rec[0].fields.value(3), quint64(0xFF));   // no HR -> invalid
        QCOMPARE(rec[0].fields.value(4), quint64(0));      // cadence 0 is valid
        QCOMPARE(rec[10].fields.value(3), quint64(140));
        QCOMPARE(rec[11].fields.value(7), quint64(161));
        QCOMPARE(rec[11].fields.value(4), quint64(90));

        const auto laps = f.of(19);
        QCOMPARE(laps.size(), 1);
        QCOMPARE(laps[0].fields.value(2), kStartFit);
        QCOMPARE(laps[0].fields.value(7), quint64(600000));
        QCOMPARE(laps[0].fields.value(19), quint64(202));
        QCOMPARE(laps[0].fields.value(20), quint64(412));
        QCOMPARE(laps[0].fields.value(41), quint64(725800));
        QCOMPARE(laps[0].fields.value(25), quint64(2));
        QCOMPARE(laps[0].fields.value(39), quint64(6));

        const auto ses = f.of(18);
        QCOMPARE(ses.size(), 1);
        const Message &m = ses[0];
        QCOMPARE(m.fields.value(5), quint64(2));     // sport cycling
        QCOMPARE(m.fields.value(6), quint64(6));     // indoor cycling
        QCOMPARE(m.fields.value(2), kStartFit);
        QCOMPARE(m.fields.value(253), kStartFit + 600);
        QCOMPARE(m.fields.value(8), quint64(600000));
        QCOMPARE(m.fields.value(20), quint64(202));  // avg power
        QCOMPARE(m.fields.value(21), quint64(412));  // max power
        QCOMPARE(m.fields.value(34), quint64(223));  // NP
        QCOMPARE(m.fields.value(35), quint64(799));  // TSS * 10
        QCOMPARE(m.fields.value(36), quint64(894));  // IF * 1000
        QCOMPARE(m.fields.value(45), quint64(250));  // FTP
        QCOMPARE(m.fields.value(16), quint64(141));
        QCOMPARE(m.fields.value(17), quint64(172));
        QCOMPARE(m.fields.value(18), quint64(89));
        QCOMPARE(m.fields.value(25), quint64(0));
        QCOMPARE(m.fields.value(26), quint64(1));
        QCOMPARE(m.fields.value(0), quint64(8));
        QCOMPARE(m.fields.value(1), quint64(1));

        const auto act = f.of(34);
        QCOMPARE(act.size(), 1);
        QCOMPARE(act[0].fields.value(1), quint64(1));
        QCOMPARE(act[0].fields.value(0), quint64(600000));
        QCOMPARE(act[0].fields.value(5), kStartFit + 600);   // UTC ride: local == utc
    }

    void emptySamples()
    {
        RideRecord r = makeRide(0);
        r.avgHeartRate = 0;
        r.maxHeartRate = 0;
        const FitFile f = parse(fit::encodeActivity(r, {}));
        QVERIFY2(f.ok, qPrintable(f.error));
        QCOMPARE(f.of(20).size(), 0);
        QCOMPARE(f.of(18).size(), 1);
        QCOMPARE(f.of(19).size(), 1);
        QCOMPARE(f.of(34).size(), 1);
        QCOMPARE(f.of(18)[0].fields.value(16), quint64(0xFF));   // no HR
        QCOMPARE(f.of(18)[0].fields.value(20), quint64(202));
    }

    void veryLongRide()
    {
        const int n = 70000;
        QVector<Sample> s;
        s.reserve(n);
        for (int t = 0; t < n; ++t)
            s.append({ t, 180, 0, 85, 0 });
        RideRecord r = makeRide(n);
        r.kilojoules = 12600;   // 180 W * 70000 s
        const QByteArray data = fit::encodeActivity(r, s);
        const FitFile f = parse(data);
        QVERIFY2(f.ok, qPrintable(f.error));
        const auto rec = f.of(20);
        QCOMPARE(rec.size(), n);
        QCOMPARE(rec.last().fields.value(253), kStartFit + n - 1);
        QCOMPARE(f.of(18)[0].fields.value(8), quint64(n) * 1000);
        QCOMPARE(f.of(18)[0].fields.value(48), quint64(12600000));
        QCOMPARE(f.of(18)[0].fields.value(11), quint64(12600));
    }

    void writeFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("ride.fit"));
        QVector<Sample> s { { 0, 100, 0, 80, 120 }, { 1, 110, 0, 81, 121 } };
        QString err;
        QVERIFY(fit::writeFile(path, makeRide(2), s, &err));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const FitFile f = parse(file.readAll());
        QVERIFY2(f.ok, qPrintable(f.error));
        QCOMPARE(f.of(20).size(), 2);

        QVERIFY(!fit::writeFile(dir.filePath(QStringLiteral("missing/dir/ride.fit")), makeRide(2), s, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFit)
#include "tst_fit.moc"
