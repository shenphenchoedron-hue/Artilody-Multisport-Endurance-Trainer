// The 3D view's landscapes (biome.h): known places land in the expected
// biome, the zone edges, a route's summary (across the date line), the
// mountains' snow and tree lines, and the Nordic countryside unchanged by the
// biomes (the same tree kinds, the same profile as the old literals).
#include "biome.h"
#include "vegetation.h"

#include <QtTest>

using route3d::Biome;
Q_DECLARE_METATYPE(route3d::Biome)

class TestBiome : public QObject
{
    Q_OBJECT
private slots:
    void places_data()
    {
        QTest::addColumn<double>("lat");
        QTest::addColumn<double>("lon");
        QTest::addColumn<Biome>("expected");
        auto row = [](const char *name, double lat, double lon, Biome b) { QTest::newRow(name) << lat << lon << b; };
        // icy
        row("Nuuk", 64.18, -51.72, Biome::Icy);
        row("Ilulissat", 69.22, -51.10, Biome::Icy);
        row("Tasiilaq", 65.61, -37.64, Biome::Icy);
        row("Tromso", 69.65, 18.96, Biome::Icy);
        row("Fairbanks", 64.84, -147.72, Biome::Icy);
        row("Yellowknife", 62.45, -114.37, Biome::Icy);
        row("Yakutsk", 62.03, 129.73, Biome::Icy);
        row("McMurdo", -77.85, 166.67, Biome::Icy);
        row("Anadyr", 64.73, 177.51, Biome::Icy);
        row("Chukotka east", 65.0, -175.0, Biome::Icy);
        // Nordic temperate
        row("Copenhagen", 55.68, 12.57, Biome::Nordic);
        row("Trondheim", 63.43, 10.39, Biome::Nordic);
        row("Reykjavik", 64.15, -21.94, Biome::Nordic);
        row("Malaga", 36.72, -4.42, Biome::Nordic);
        row("Almeria", 36.84, -2.46, Biome::Nordic);
        row("Valletta", 35.90, 14.51, Biome::Nordic);
        row("Heraklion", 35.34, 25.13, Biome::Nordic);
        row("San Francisco", 37.77, -122.42, Biome::Nordic);
        row("Los Angeles", 34.05, -118.24, Biome::Nordic);
        row("San Diego", 32.72, -117.16, Biome::Nordic);
        row("Tel Aviv", 32.08, 34.78, Biome::Nordic);
        row("Beirut", 33.89, 35.50, Biome::Nordic);
        // desert
        row("Tangier", 35.77, -5.80, Biome::Desert);
        row("Rabat", 34.02, -6.84, Biome::Desert);
        row("Marrakech", 31.63, -8.01, Biome::Desert);
        row("Agadir", 30.43, -9.60, Biome::Desert);
        row("Algiers", 36.75, 3.06, Biome::Desert);
        row("Tunis", 36.81, 10.18, Biome::Desert);
        row("Cairo", 30.04, 31.24, Biome::Desert);
        row("Dubai", 25.20, 55.27, Biome::Desert);
        row("Karachi", 24.86, 67.01, Biome::Desert);
        row("Phoenix", 33.45, -112.07, Biome::Desert);
        row("Las Vegas", 36.17, -115.14, Biome::Desert);
        row("Eilat", 29.56, 34.95, Biome::Desert);
        row("Riyadh", 24.71, 46.68, Biome::Desert);
        row("Lima", -12.05, -77.04, Biome::Desert);
        row("Antofagasta", -23.65, -70.40, Biome::Desert);
        row("Swakopmund", -22.68, 14.53, Biome::Desert);
        // Australia, the tropics
        row("Sydney", -33.87, 151.21, Biome::Australian);
        row("Darwin", -12.46, 130.84, Biome::Australian);
        row("Bangkok", 13.76, 100.50, Biome::Tropical);
        row("Singapore", 1.35, 103.82, Biome::Tropical);
        row("Pucallpa", -8.38, -74.55, Biome::Tropical);
        row("Panama", 8.98, -79.52, Biome::Tropical);
    }
    void places()
    {
        QFETCH(double, lat);
        QFETCH(double, lon);
        QFETCH(Biome, expected);
        QCOMPARE(int(route3d::Biomes::classify(lat, lon, 50, 5)), int(expected));
    }
    void edges()
    {
        using route3d::Biomes::classify;
        QCOMPARE(int(classify(66.0, 15.0, 50, 5)), int(Biome::Nordic));     // the polar circle itself: not yet
        QCOMPARE(int(classify(66.01, 15.0, 50, 5)), int(Biome::Icy));
        QCOMPARE(int(classify(-60.0, 0.0, 50, 5)), int(Biome::Nordic));
        QCOMPARE(int(classify(-60.01, 0.0, 50, 5)), int(Biome::Icy));
        QCOMPARE(int(classify(55.68, 12.57, 1000, 25)), int(Biome::Nordic));   // the mountain limits are exclusive
        QCOMPARE(int(classify(55.68, 12.57, 1000.1, 5)), int(Biome::Mountain));
        QCOMPARE(int(classify(55.68, 12.57, 50, 25.1)), int(Biome::Mountain));
    }
    void names()
    {
        for (int i = 0; i <= int(Biome::Mountain); ++i) {
            Biome b = Biome::Nordic;
            QVERIFY(route3d::Biomes::fromName(route3d::Biomes::name(Biome(i)), &b));
            QCOMPARE(int(b), i);
        }
        Biome b = Biome::Desert;
        QVERIFY(!route3d::Biomes::fromName("jungle", &b));
        QVERIFY(!route3d::Biomes::fromName("Nordic", &b));   // case-sensitive
        QVERIFY(!route3d::Biomes::fromName(nullptr, &b));
        QCOMPARE(int(b), int(Biome::Desert));                // untouched
    }
    void summary()
    {
        struct P { double lat, lon, elevation, distance; };
        // across the date line: the middle is at 180, not at 0
        const P dateLine[] = { { 64.0, 179.8, 10, 0 }, { 64.2, -179.8, 30, 2000 }, { 64.4, -179.6, 20, 4000 } };
        auto s = route3d::Biomes::summarize(dateLine, 3);
        QVERIFY(std::abs(s.lat - 64.2) < 1e-9);
        QVERIFY(std::abs(std::remainder(s.lon - (-179.8667), 360.0)) < 1e-3);
        QVERIFY(std::abs(s.elevation - 20) < 1e-9);
        QVERIFY(std::abs(s.climbPerKm - 5) < 1e-9);   // 20 m up over 4 km
        const P shortRoute[] = { { 55, 12, 0, 0 }, { 55, 12, 50, 900 } };
        QCOMPARE(route3d::Biomes::summarize(shortRoute, 2).climbPerKm, 0.0);   // up to 1 km: no climb rate
        QCOMPARE(route3d::Biomes::summarize(shortRoute, 0).lat, 0.0);
    }
    void mountainLines()
    {
        using route3d::Biomes::profile;
        // a high plateau (Denver): no snow near the road (nothing reaches 2300 m),
        // the trees grow there
        auto plateau = profile(Biome::Mountain, 1550, 1600, 1650);
        QVERIFY(plateau.snowLine >= 1e4f);
        QCOMPARE(plateau.treeLine, 2180.0);
        // an alpine climb to 2600 m: snow from 2300 m, the trees end below
        auto alps = profile(Biome::Mountain, 800, 1500, 2600);
        QCOMPARE(alps.snowLine, 2300.f);
        QCOMPARE(alps.treeLine, 2180.0);
        // higher still: the snow line follows the road up
        auto high = profile(Biome::Mountain, 3000, 3600, 4400);
        QVERIFY(high.snowLine > 3150.f);
        QVERIFY(high.treeLine >= 3200.0);
    }
    void nordicTreesUnchanged()
    {
        // the tree kinds the Nordic world always had (vegetation.h before the
        // biomes): forest .62 spruce / broadleaf, open .40 bush / .80 broadleaf / spruce
        const auto nordic = route3d::Biomes::profile(Biome::Nordic);
        int wrong = 0;
        for (uint32_t k = 0; k < (1u << 24); ++k) {
            const double roll = double(k) / double(1u << 24);
            const int forest = roll < .62 ? route3d::Spruce : route3d::Broadleaf;
            const int open = roll < .40 ? route3d::Bush : (roll < .80 ? route3d::Broadleaf : route3d::Spruce);
            wrong += route3d::Vegetation::pick(nordic.forestMix, roll, false) != forest;
            wrong += route3d::Vegetation::pick(nordic.openMix, roll, true) != open;
        }
        QCOMPARE(wrong, 0);
    }
    void nordicProfileUnchanged()
    {
        // the literals the shaders, the terrain and the trees had before the biomes
        const auto p = route3d::Biomes::profile(Biome::Nordic);
        auto same = [](route3d::Rgb c, float r, float g, float b) { return c.r == r && c.g == g && c.b == b; };
        QVERIFY(same(p.zenith, .27f, .47f, .77f));
        QVERIFY(same(p.horizon, .73f, .81f, .88f));
        QVERIFY(same(p.sunLight, 1.f, .94f, .82f));
        QVERIFY(same(p.ambientSky, .52f, .57f, .63f));
        QVERIFY(same(p.ambientGround, .30f, .29f, .23f));
        QVERIFY(same(p.waterShallow, .19f, .33f, .30f));
        QVERIFY(same(p.waterDeep, .05f, .15f, .23f));
        QVERIFY(same(p.grassTint, 1, 1, 1) && same(p.leafTint, 1, 1, 1));
        QCOMPARE(p.treeOpen, .035);
        QCOMPARE(p.treeForest, .80);
        QCOMPARE(p.treeScale, 1.0);
        QCOMPARE(p.hillScale, 1.0);
        QCOMPARE(p.lakeChance, .6);
        QCOMPARE(p.sandCover, 0.f);
        QCOMPARE(p.snowCover, 0.f);
        QVERIFY(p.snowLine >= 1e4f && p.treeLine >= 1e4);
        QCOMPARE(p.buildings, 1.f);
        QVERIFY(p.animals && p.bales && p.tractors && p.turbines && p.fences);
    }
    void mountainsWin()
    {
        // high or hilly: mountains, wherever they are
        QCOMPARE(int(route3d::Biomes::classify(55.68, 12.57, 1500, 5)), int(Biome::Mountain));
        QCOMPARE(int(route3d::Biomes::classify(31.63, -8.01, 200, 40)), int(Biome::Mountain));
    }
};

QTEST_GUILESS_MAIN(TestBiome)
#include "tst_biome.moc"
