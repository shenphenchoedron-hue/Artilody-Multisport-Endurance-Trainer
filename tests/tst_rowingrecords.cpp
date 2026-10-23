#include "Database.h"
#include "ProfileManager.h"
#include "RowingRecords.h"

#include <QtTest>

using namespace watt;

class TestRowingRecords : public QObject
{
    Q_OBJECT
private slots:
    void interpolation()
    {
        const QVector<TracePoint> trace { { 0, 0 }, { 1, 4 }, { 2, 9 }, { 4, 19 } };
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 0), 0.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, -3), 0.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 0.5), 2.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 1), 4.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 1.5), 6.5);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 3), 14.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 4), 19.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(trace, 100), 19.0);   // finished
        QCOMPARE(RowingRecords::ghostDistanceAt({}, 5), 0.0);
        // trace without a [0,0] start interpolates from the origin
        QCOMPARE(RowingRecords::ghostDistanceAt({ { 2, 10 } }, 1), 5.0);
        QCOMPARE(RowingRecords::pace500(2000, 420), 105.0);
    }

    void traceEncoding()
    {
        const QVector<TracePoint> trace { { 0, 0 }, { 1.04, 4.26 }, { 2, 9.5 } };
        const QVector<TracePoint> back = Database::traceFromText(Database::traceToText(trace));
        QCOMPARE(back.size(), 3);
        QCOMPARE(back.at(1).t, 1.0);
        QCOMPARE(back.at(1).d, 4.3);
        QCOMPARE(back.at(2).d, 9.5);
        const QVariantList v = RowingRecords::traceToVariant(trace);
        QCOMPARE(RowingRecords::traceFromVariant(v), trace);
        QVariantMap m { { QStringLiteral("t"), 3.0 }, { QStringLiteral("d"), 12.0 } };
        QCOMPARE(RowingRecords::traceFromVariant({ m }).first().d, 12.0);
    }

    void bestRecords()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        RowingRecords rec(&db, &profiles);
        QSignalSpy spy(&rec, &RowingRecords::newRecord);

        QVERIFY(rec.bestFor(2000).isEmpty());
        QCOMPARE(rec.ghostDistance(2000, 10), 0.0);

        QVector<TracePoint> slow;
        for (int t = 0; t <= 480; ++t)
            slow.append({ double(t), std::min(2000.0, t * 2000.0 / 480.0) });
        QVERIFY(rec.saveIfBest(2000, 480.0, slow));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(2).toDouble(), 0.0);
        QCOMPARE(rec.ghostDistance(2000, 240), 1000.0);
        QVERIFY(!rec.saveIfBest(2000, 490.0, slow));   // slower
        QVERIFY(!rec.saveIfBest(2000, 480.0, slow));   // equal
        QVariantList fast;
        for (int t = 0; t <= 400; ++t)
            fast.append(QVariant(QVariantList { t, t * 5.0 }));
        RideRecord ride;
        ride.profileId = profiles.profileId();
        ride.sport = QStringLiteral("rowing");
        ride.distanceM = 2000;
        ride.startedAt = QDateTime::currentDateTime();
        const qint64 rideId = db.saveRide(ride, {});
        QVERIFY(rideId > 0);
        QVERIFY(rec.saveIfBest(2000, 400.0, fast, rideId));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(2).toDouble(), 480.0);
        QCOMPARE(rec.ghostDistance(2000, 240), 1200.0);   // cache refreshed

        const QVariantMap best = rec.bestFor(2000);
        QCOMPARE(best.value(QStringLiteral("timeSec")).toDouble(), 400.0);
        QCOMPARE(best.value(QStringLiteral("rideId")).toLongLong(), rideId);
        QCOMPARE(best.value(QStringLiteral("pace500Sec")).toDouble(), 100.0);
        QCOMPARE(best.value(QStringLiteral("trace")).toList().size(), 401);

        QVERIFY(rec.saveIfBest(1234, 300.0, QVector<TracePoint> { { 0, 0 }, { 300, 1234 } }));
        QVERIFY(rec.saveIfBest(500, 95.0, QVector<TracePoint> {}, 999999));   // unknown ride -> stored without
        QCOMPARE(rec.bestFor(500).value(QStringLiteral("rideId")).toLongLong(), qint64(-1));
        QVERIFY(!rec.saveIfBest(0, 95.0, QVector<TracePoint> {}));
        QVERIFY(!rec.saveIfBest(500, -1, QVector<TracePoint> {}));

        const QVariantList all = rec.allBests();
        QCOMPARE(all.size(), 7);   // 6 standard + 1 custom
        QList<int> distances;
        for (const QVariant &v : all)
            distances.append(v.toMap().value(QStringLiteral("distanceM")).toInt());
        QCOMPARE(distances, (QList<int> { 500, 1000, 1234, 2000, 5000, 6000, 10000 }));
        QVariantMap m500 = all.at(0).toMap();
        QVERIFY(m500.value(QStringLiteral("hasRecord")).toBool());
        QCOMPARE(m500.value(QStringLiteral("timeSec")).toDouble(), 95.0);
        QVERIFY(!all.at(1).toMap().value(QStringLiteral("hasRecord")).toBool());
        QVERIFY(!all.at(2).toMap().value(QStringLiteral("standard")).toBool());
        QCOMPARE(all.at(3).toMap().value(QStringLiteral("timeSec")).toDouble(), 400.0);

        // per profile
        const qint64 first = profiles.profileId();
        profiles.createProfile(QStringLiteral("Other"));
        QVERIFY(rec.bestFor(2000).isEmpty());
        QCOMPARE(rec.ghostDistance(2000, 240), 0.0);
        QVERIFY(profiles.selectProfile(first));
        QCOMPARE(rec.ghostDistance(2000, 240), 1200.0);
    }
};

QTEST_GUILESS_MAIN(TestRowingRecords)
#include "tst_rowingrecords.moc"
