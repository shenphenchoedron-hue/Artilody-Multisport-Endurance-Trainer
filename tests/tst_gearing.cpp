// Gearing model (GearSetup / Gearing), road model and gear setup persistence.
#include "Database.h"
#include "Gearing.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QtTest>
#include <cmath>

using namespace watt;

namespace {
int cogIndex(const GearSetup &s, int teeth)
{
    return s.cogs.indexOf(teeth);
}
} // namespace

class TestGearing : public QObject
{
    Q_OBJECT
private slots:
    void defaults()
    {
        const GearSetup s = GearSetup::defaults();
        QCOMPARE(s.chainrings, QVector<int>({ 34, 50 }));
        QCOMPARE(s.cogs, QVector<int>({ 11, 12, 13, 14, 15, 17, 19, 21, 24, 27, 30, 34 }));
        QCOMPARE(s.circumferenceMm, 2105);
        QVERIFY(s.sequential);
        QCOMPARE(s.bikeWeightKg, 9.0);
        QCOMPARE(s.cda, 0.32);
        QCOMPARE(s.crr, 0.004);
        QVERIFY(s.isValid());
        // starts in the middle of the sequential list
        const Gearing g(s);
        QCOMPARE(g.gearIndex(), g.gearCount() / 2);
        QVERIFY(g.canShiftUp());
        QVERIFY(g.canShiftDown());
    }

    void ratioAndLabel()
    {
        GearSetup s = GearSetup::defaults();
        s.sequential = false;
        Gearing g(s);
        QVERIFY(g.setGear(1, cogIndex(g.setup(), 17)));
        QCOMPARE(g.frontTeeth(), 50);
        QCOMPARE(g.rearTeeth(), 17);
        QCOMPARE(g.label(), QStringLiteral("50×17"));
        QVERIFY(qAbs(g.ratio() - 50.0 / 17.0) < 1e-12);
        QVERIFY(!g.setGear(2, 0));
        QVERIFY(!g.setGear(0, 12));
        QVERIFY(!g.setGear(-1, 0));
    }

    void virtualSpeed()
    {
        // 90 rpm, 50x17, 2105 mm: 1.5 rev/s * 2.941 * 2.105 m = 9.287 m/s (33.4 km/h)
        QVERIFY(qAbs(Gearing::speedFor(90, 50.0 / 17.0, 2105) - 1.5 * 50.0 / 17.0 * 2.105) < 1e-9);
        QVERIFY(qAbs(Gearing::speedFor(90, 50.0 / 17.0, 2105) * 3.6 - 33.43) < 0.01);
        QCOMPARE(Gearing::speedFor(0, 3.0, 2105), 0.0);
        QCOMPARE(Gearing::speedFor(-5, 3.0, 2105), 0.0);
        GearSetup s = GearSetup::defaults();
        s.sequential = false;
        Gearing g(s);
        g.setGear(1, cogIndex(g.setup(), 17));
        QVERIFY(qAbs(g.virtualSpeed(90) - Gearing::speedFor(90, 50.0 / 17.0, 2105)) < 1e-12);
        // harder gear -> faster at the same cadence
        const double before = g.virtualSpeed(90);
        QVERIFY(g.shiftUp());
        QVERIFY(g.virtualSpeed(90) > before);
    }

    void derailleurShifting()
    {
        GearSetup s = GearSetup::defaults();
        s.sequential = false;
        Gearing g(s);
        QCOMPARE(g.gearCount(), 24);
        QVERIFY(g.setGear(1, 0));        // 50x11: hardest
        QVERIFY(!g.canShiftUp());
        QVERIFY(!g.shiftUp());
        QVERIFY(!g.canShiftFrontUp());
        QCOMPARE(g.gearIndex(), 23);
        QVERIFY(g.shiftDown());
        QCOMPARE(g.rearTeeth(), 12);
        QVERIFY(g.shiftFrontDown());
        QCOMPARE(g.frontTeeth(), 34);
        QCOMPARE(g.rearTeeth(), 12);     // rear stays when the front shifts
        QVERIFY(!g.shiftFrontDown());
        QVERIFY(g.setGear(0, 11));       // 34x34: easiest
        QVERIFY(!g.canShiftDown());
        QCOMPARE(g.gearIndex(), 0);
        QVERIFY(g.shiftFrontUp());
        QCOMPARE(g.label(), QStringLiteral("50×34"));
        // setGearIndex follows the ratio order
        QVERIFY(g.setGearIndex(23));
        QCOMPARE(g.label(), QStringLiteral("50×11"));
        QVERIFY(!g.setGearIndex(24));
    }

    void sequentialList()
    {
        const GearSetup s = GearSetup::defaults();
        const QVector<Gear> seq = Gearing::buildSequence(s);
        QVERIFY(seq.size() >= 16);
        QVERIFY(seq.size() < 24);   // near-duplicates removed
        QCOMPARE(Gearing::labelFor(seq.first().frontTeeth, seq.first().rearTeeth), QStringLiteral("34×34"));
        QCOMPARE(Gearing::labelFor(seq.last().frontTeeth, seq.last().rearTeeth), QStringLiteral("50×11"));
        for (int i = 1; i < seq.size(); ++i)
            QVERIFY2(seq.at(i).ratio > seq.at(i - 1).ratio * (1.0 + Gearing::kDuplicateTolerance),
                     qPrintable(QStringLiteral("gear %1").arg(i)));
        // 34x13 (2.615) and 50x19 (2.632) are the same gear: only one stays
        int both = 0;
        for (const Gear &g : seq)
            if ((g.frontTeeth == 34 && g.rearTeeth == 13) || (g.frontTeeth == 50 && g.rearTeeth == 19))
                ++both;
        QCOMPARE(both, 1);

        Gearing g(s);
        QCOMPARE(g.gearCount(), seq.size());
        QVERIFY(g.setGearIndex(0));
        QVERIFY(!g.canShiftDown());
        int steps = 0;
        double last = g.ratio();
        while (g.shiftUp()) {
            QVERIFY(g.ratio() > last);
            last = g.ratio();
            ++steps;
        }
        QCOMPARE(steps, seq.size() - 1);
        QCOMPARE(g.label(), QStringLiteral("50×11"));
        // front shift = jump of kSequentialJump gears
        QVERIFY(g.shiftFrontDown());
        QCOMPARE(g.gearIndex(), seq.size() - 1 - Gearing::kSequentialJump);
        QVERIFY(g.setGearIndex(1));
        QVERIFY(g.shiftFrontDown());
        QCOMPARE(g.gearIndex(), 0);
    }

    void singleChainring()
    {
        GearSetup s;
        s.chainrings = { 42 };
        s.cogs = { 10, 11, 13, 15, 17, 19, 21, 24, 28, 32, 38, 44 };
        QVERIFY(s.isValid());
        Gearing g(s);
        QCOMPARE(g.gearCount(), 12);
        QVERIFY(!g.canShiftFrontUp() || g.setup().sequential);
        g.setSequential(false);
        QVERIFY(!g.canShiftFrontUp());
        QVERIFY(!g.canShiftFrontDown());
        QCOMPARE(g.gearCount(), 12);
        QVERIFY(g.setGear(0, 0));
        QCOMPARE(g.label(), QStringLiteral("42×10"));
    }

    void tripleAndUnsortedInput()
    {
        GearSetup s;
        s.chainrings = { 50, 30, 39, 39 };   // unsorted + duplicate
        s.cogs = { 25, 11, 12, 13, 14, 15, 17, 19, 21, 23 };
        const GearSetup n = s.normalized();
        QCOMPARE(n.chainrings, QVector<int>({ 30, 39, 50 }));
        QCOMPARE(n.cogs.first(), 11);
        QCOMPARE(n.cogs.last(), 25);
        Gearing g(s);
        QVERIFY(g.gearCount() > 10);
        QVERIFY(g.gearCount() < 30);
    }

    void validation()
    {
        QString e;
        QVERIFY(GearSetup::validate({ 50, 34 }, { 11, 28 }, 2105, &e));
        QVERIFY(e.isEmpty());
        QVERIFY(!GearSetup::validate({}, { 11, 28 }, 2105, &e));
        QVERIFY(e.contains(QStringLiteral("chainring")));
        QVERIFY(!GearSetup::validate({ 53, 50, 39, 30 }, { 11, 28 }, 2105, &e));
        QVERIFY(!GearSetup::validate({ 50 }, {}, 2105, &e));
        QVERIFY(!GearSetup::validate({ 50 }, { 8, 28 }, 2105, &e));
        QVERIFY(e.contains(QStringLiteral("Cogs")));
        QVERIFY(!GearSetup::validate({ 80 }, { 11 }, 2105, &e));
        QVERIFY(!GearSetup::validate({ 50 }, { 11 }, 500, &e));
        QVERIFY(e.contains(QStringLiteral("circumference")));
        QVERIFY(!GearSetup::validate({ 50 }, QVector<int>(15, 11) << 12 << 13 << 14 << 15 << 16 << 17 << 18 << 19 << 20
                                                 << 21 << 22 << 23 << 24 << 25,
                                     2105, &e));
    }

    void presets()
    {
        static const QRegularExpression name(QStringLiteral("^(\\d+)-(\\d+) (\\d+)-speed"));
        const QVariantList cassettes = GearSetup::cassettePresets();
        QVERIFY(cassettes.size() >= 10);
        for (const QVariant &v : cassettes) {
            const QVariantMap m = v.toMap();
            QVector<int> cogs;
            for (const QVariant &c : m.value(QStringLiteral("values")).toList())
                cogs.append(c.toInt());
            const QString n = m.value(QStringLiteral("name")).toString();
            const auto match = name.match(n);
            QVERIFY2(match.hasMatch(), qPrintable(n));
            QCOMPARE(cogs.first(), match.captured(1).toInt());
            QCOMPARE(cogs.last(), match.captured(2).toInt());
            QCOMPARE(cogs.size(), match.captured(3).toInt());
            QVERIFY2(GearSetup::validate({ 50, 34 }, cogs, 2105), qPrintable(n));
            QCOMPARE(GearSetup::parseTeethList(m.value(QStringLiteral("text")).toString()), cogs);
            for (int i = 1; i < cogs.size(); ++i)
                QVERIFY(cogs.at(i) > cogs.at(i - 1));
        }
        for (const QVariant &v : GearSetup::chainringPresets()) {
            QVector<int> rings;
            for (const QVariant &c : v.toMap().value(QStringLiteral("values")).toList())
                rings.append(c.toInt());
            QVERIFY(GearSetup::validate(rings, { 11, 28 }, 2105));
        }
        for (const QVariant &v : GearSetup::wheelPresets()) {
            const int mm = v.toMap().value(QStringLiteral("mm")).toInt();
            QVERIFY(GearSetup::validate({ 50 }, { 11 }, mm));
        }
    }

    void parseTeeth()
    {
        QCOMPARE(GearSetup::parseTeethList(QStringLiteral("11,12,13")), QVector<int>({ 11, 12, 13 }));
        QCOMPARE(GearSetup::parseTeethList(QStringLiteral(" 11-12 - 13 ")), QVector<int>({ 11, 12, 13 }));
        QCOMPARE(GearSetup::parseTeethList(QStringLiteral("50/34")), QVector<int>({ 50, 34 }));
        QCOMPARE(GearSetup::parseTeethList(QStringLiteral("11; 12 x")), QVector<int>());
        QCOMPARE(GearSetup::parseTeethList(QString()), QVector<int>());
    }

    void jsonRoundTrip()
    {
        GearSetup s;
        s.chainrings = { 42 };
        s.cogs = { 10, 12, 14, 16, 18, 21, 24, 28, 32, 36, 42, 50 };
        s.circumferenceMm = 2298;
        s.sequential = false;
        s.frontIndex = 0;
        s.rearIndex = 7;
        s.bikeWeightKg = 12.5;
        s.cda = 0.4;
        s.crr = 0.008;
        const GearSetup back = GearSetup::fromJsonText(s.toJsonText());
        QVERIFY(back == s);
        QCOMPARE(GearSetup::fromJsonText(QString()), GearSetup::defaults());
        QCOMPARE(GearSetup::fromJsonText(QStringLiteral("garbage")), GearSetup::defaults());
        // invalid gearing falls back to the defaults, valid physics survive
        const GearSetup bad = GearSetup::fromJsonText(
            QStringLiteral("{\"chainrings\":[5],\"cogs\":[11],\"circumferenceMm\":2105,\"bikeWeightKg\":11}"));
        QCOMPARE(bad.chainrings, GearSetup::defaults().chainrings);
        QCOMPARE(bad.bikeWeightKg, 11.0);
        // out of range indices are replaced
        const GearSetup idx = GearSetup::fromJsonText(
            QStringLiteral("{\"chainrings\":[50,34],\"cogs\":[11,28],\"circumferenceMm\":2105,\"front\":7,\"rear\":9}"));
        QVERIFY(idx.frontIndex >= 0 && idx.frontIndex < 2);
        QVERIFY(idx.rearIndex >= 0 && idx.rearIndex < 2);
    }

    void setSetupKeepsNearestRatio()
    {
        GearSetup s = GearSetup::defaults();
        s.sequential = false;
        Gearing g(s);
        g.setGear(1, cogIndex(g.setup(), 17));   // 2.94
        GearSetup oneBy;
        oneBy.chainrings = { 40 };
        oneBy.cogs = { 10, 12, 14, 16, 18, 21, 24, 28, 32, 36, 42 };
        oneBy.sequential = false;
        oneBy.frontIndex = oneBy.rearIndex = -1;   // "keep the current gear"
        g.setSetup(oneBy);
        QCOMPARE(g.label(), QStringLiteral("40×14"));   // 2.857 is nearest to 2.94
    }

    void roadModel()
    {
        road::Model m;   // 79 kg, CdA 0.32, Crr 0.004, flat
        QCOMPARE(road::powerForSpeed(m, 0), 0.0);
        // 33.4 km/h on the flat: (3.10 N + 16.9 N) * 9.29 m/s / 0.97 = ~191 W
        const double v = 1.5 * 50.0 / 17.0 * 2.105;
        const double p = road::powerForSpeed(m, v);
        QVERIFY2(p > 185 && p < 198, qPrintable(QString::number(p)));
        // monotonic in speed
        double prev = 0;
        for (double s = 1; s < 20; s += 0.5) {
            const double w = road::powerForSpeed(m, s);
            QVERIFY(w > prev);
            prev = w;
        }
        // inverse
        QVERIFY(qAbs(road::speedForPower(m, p) - v) < 1e-3);
        QCOMPARE(road::speedForPower(m, 0), 0.0);
        // climbing costs more, heavier costs more uphill
        road::Model hill = m;
        hill.gradePercent = 5;
        QVERIFY(road::powerForSpeed(hill, 4) > road::powerForSpeed(m, 4) + 100);
        road::Model heavy = hill;
        heavy.massKg = 100;
        QVERIFY(road::powerForSpeed(heavy, 4) > road::powerForSpeed(hill, 4));
        // coasting: slows down on the flat, speeds up steeply downhill
        QVERIFY(road::coastDeceleration(m, 10) > 0);
        road::Model down = m;
        down.gradePercent = -8;
        QVERIFY(road::coastDeceleration(down, 5) < 0);
        QVERIFY(road::speedForPower(down, 0) > 10);   // terminal coasting speed
    }

    void persistence()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        const qint64 id = db.profiles().first().id;
        QVERIFY(db.gearSetupJson(id).isEmpty());
        GearSetup s;
        s.chainrings = { 44 };
        s.cogs = { 11, 13, 15, 17, 19, 22, 25, 28, 32, 36, 42 };
        s.frontIndex = 0;
        s.rearIndex = 4;
        QVERIFY(db.setGearSetupJson(id, s.toJsonText()));
        QCOMPARE(GearSetup::fromJsonText(db.gearSetupJson(id)), s.normalized());
        QVERIFY(!db.setGearSetupJson(9999, s.toJsonText()));
        // saving the profile does not touch the gear setup
        Profile p = *db.profile(id);
        p.ftp = 300;
        QCOMPARE(db.saveProfile(p), id);
        QCOMPARE(GearSetup::fromJsonText(db.gearSetupJson(id)).chainrings, QVector<int>({ 44 }));
    }
};

QTEST_GUILESS_MAIN(TestGearing)
#include "tst_gearing.moc"
