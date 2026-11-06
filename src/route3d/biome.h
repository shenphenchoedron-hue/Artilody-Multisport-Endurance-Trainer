#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>

// Geometry only: no Qt Widgets / GL.
namespace route3d {

// The landscape a route is drawn in, picked once per route from where it is
// on the earth (rough lat/lon zones) and how much it climbs. Not precise: it
// only gives the rides some variation.
enum class Biome { Nordic = 0, Tropical, Australian, Desert, Icy, Mountain };

constexpr int kTreeKinds = 7;   // route3d::TreeKind (vegetation.h)

struct Rgb { float r, g, b; };

// The birds (Route3DScene::birds): small ones in loose flocks over the
// fields, a large bird of prey circling above the road, little ones flitting
// from tree to tree beside it.
namespace Bird {
enum Kind { None = -1, Gull = 0, Crow, Starling, SeaEagle, Kite, Vulture, Cockatoo, Galah, Parrot, Egret, WedgeTail,
            GoldenEagle, Chough, Finch, Magpie, Sparrow, Lorikeet, Bunting, BeeEater };
}

// Everything that differs between the biomes: sky and light (shaders),
// ground (shaders), relief and lakes (Terrain), vegetation and what stands
// in the countryside (Route3DScene::buildTile).
struct BiomeProfile {
    Biome biome = Biome::Nordic;
    // sky and light
    Rgb zenith { .27f, .47f, .77f }, horizon { .73f, .81f, .88f }, sunGlow { 1.f, .86f, .64f };
    Rgb sunLight { 1.f, .94f, .82f }, ambientSky { .52f, .57f, .63f }, ambientGround { .30f, .29f, .23f };
    float sun[3] { -.55f, .50f, .67f };   // towards the sun (x east, y up, z south); normalised by the user
    float cloudShift = 0;                 // > 0: fewer clouds
    // ground
    Rgb grassTint { 1, 1, 1 };            // multiplies the grass
    Rgb sandTint { 1, 1, 1 };             // multiplies the sand (beaches, desert ground)
    float sandCover = 0;                  // 0..1: how much of the open ground is sand
    float snowCover = 0;                  // 0..1: snow everywhere (steep slopes stay rock)
    float snowLine = 1e5f;                // metres (route elevation): snow above
    float treeSnow = 0;                   // snow on the trees' upper faces
    bool iceWater = false;                // lakes and the sea frozen
    Rgb waterShallow { .19f, .33f, .30f }, waterDeep { .05f, .15f, .23f };
    // the distant hills around the horizon
    Rgb ringFar { .42f, .52f, .60f }, ringNear { .27f, .38f, .24f };
    float ringScale = 1;                  // their height
    bool ringSnow = false;                // snow-capped
    // relief and water
    double hillScale = 1;                 // the hills beside the road
    double lakeChance = .6;               // per stretch of road (Terrain::tryLake)
    // vegetation: probability per 10 m cell in the open and in forest, and
    // the mix of tree kinds there (weights per route3d::TreeKind)
    double treeOpen = .035, treeForest = .80;
    float forestMix[kTreeKinds] { .62f, .38f, 0, 0, 0, 0, 0 };
    float openMix[kTreeKinds] { .20f, .40f, .40f, 0, 0, 0, 0 };
    double treeScale = 1;
    double treeLine = 1e5;                // metres: no trees above (they thin out below it)
    Rgb leafTint { 1, 1, 1 };             // broadleaf trees and bushes
    double flowers = 1, rocks = 1;        // density factors
    float buildings = 1;                  // share of the houses kept
    bool animals = true, bales = true, tractors = true, turbines = true, fences = true;
    // birds (Route3DBirds.cpp): the flocks' kinds (one picked per flock) and
    // how often there is one, the bird of prey and how often
    int flockBirds[3] { Bird::Gull, Bird::Crow, Bird::Starling };
    float flockChance = .55f;      // per 250 m of road
    int raptor = Bird::SeaEagle;   // the white-tailed sea eagle
    float raptorChance = .55f;     // per 700 m of road
    // the little birds flying between the trees near the road (one of the
    // two picked per bird) and the share of those trees with one
    int perchBirds[2] { Bird::Finch, Bird::Magpie };
    float perchChance = .30f;
    Rgb walls[4] { { .84f, .83f, .79f }, { .86f, .72f, .42f }, { .58f, .23f, .17f }, { .14f, .13f, .12f } };
    Rgb roofs[5] { { .52f, .22f, .15f }, { .12f, .12f, .14f }, { .42f, .35f, .24f }, { .46f, .46f, .44f }, { .30f, .32f, .35f } };
};

namespace Biomes {

// The rough zones (degrees): lat0 < lat1, lon0 < lon1.
struct Box { double lat0, lat1, lon0, lon1; };
inline bool inside(const Box &b, double lat, double lon)
{
    return lat >= b.lat0 && lat <= b.lat1 && lon >= b.lon0 && lon <= b.lon1;
}

// Icy: everything beyond these latitudes, and the boxes below.
constexpr double kArcticLat = 66, kAntarcticLat = -60;
constexpr Box kIcy[] = {
    { 59, 84, -75, -30 },     // southern Greenland (Nuuk, Tasiilaq; Iceland stays out)
    { 62, 84, -170, -52 },    // northern Canada and Alaska (Yukon, Nunavut, Fairbanks)
    { 62, 84, 60, 180 },      // Siberia, the Russian far east
    { 62, 84, -180, -170 },   // Chukotka and the Bering coast east of the date line
};
constexpr Box kAustralia { -44, -10, 112, 154 };
constexpr Box kDeserts[] = {
    // the Sahara and North Africa, in three boxes that keep southern Spain,
    // Sicily, Malta and Crete out
    { 15, 35.95, -18, -1 },   // Morocco, Western Sahara, Mauritania
    { 15, 37.5, -1, 12 },     // Algeria, Tunisia
    { 15, 33, 12, 35 },       // Libya, Egypt, Sudan, Chad
    { 12, 36.5, 34, 63 },     // Arabia, the Middle East, Iran
    { 23, 31, 63, 75 },       // Pakistan, the Thar desert
    { 36, 48, 55, 110 },      // Central Asia and the Gobi
    { 22, 38, -117, -103 },   // south-west USA and northern Mexico (not the Californian coast)
    { -14, -3.5, -82, -76.5 },   // the Peruvian coast (not the Amazon behind the Andes)
    { -30, -14, -76.5, -68 },    // southern Peru and the Atacama
    { -30, -17, 11, 25 },     // Kalahari and Namib
    { -52, -38, -72, -63 },   // Patagonia
};

// Green strips inside the desert boxes (checked first): the Levant's coast
// and hills (Beirut, Haifa, Tel Aviv, Jerusalem).
constexpr Box kNotDesert[] = {
    { 31, 36.5, 34, 36 },
};

// lat/lon: the route's middle; meanElevation (m) and climbPerKm (m of
// ascent per km) from its profile. First match wins.
inline Biome classify(double lat, double lon, double meanElevation, double climbPerKm)
{
    if (meanElevation > 1000 || climbPerKm > 25)
        return Biome::Mountain;
    if (lat > kArcticLat || lat < kAntarcticLat)
        return Biome::Icy;
    for (const Box &b : kIcy)
        if (inside(b, lat, lon))
            return Biome::Icy;
    if (inside(kAustralia, lat, lon))
        return Biome::Australian;
    const bool green = std::any_of(std::begin(kNotDesert), std::end(kNotDesert), [&](const Box &b) { return inside(b, lat, lon); });
    for (const Box &b : kDeserts)
        if (!green && inside(b, lat, lon))
            return Biome::Desert;
    if (std::abs(lat) <= 23.5)
        return Biome::Tropical;
    return Biome::Nordic;
}

// What classify() needs from a route: its middle (mean position; the mean
// longitude taken across the date line), mean elevation and metres climbed
// per km (0 for routes up to 1 km). Point: lat, lon (degrees), elevation,
// distance (m) members.
struct Summary { double lat = 0, lon = 0, elevation = 0, climbPerKm = 0; };
template <class Point>
inline Summary summarize(const Point *pts, size_t n)
{
    Summary s;
    if (!pts || n == 0)
        return s;
    const double lon0 = pts[0].lon;
    double lon = 0, climb = 0;
    for (size_t i = 0; i < n; ++i) {
        s.lat += pts[i].lat;
        lon += std::remainder(pts[i].lon - lon0, 360.0);
        s.elevation += pts[i].elevation;
        if (i > 0)
            climb += std::max(0.0, double(pts[i].elevation - pts[i - 1].elevation));
    }
    s.lat /= double(n);
    s.lon = std::remainder(lon0 + lon / double(n), 360.0);
    s.elevation /= double(n);
    const double km = (pts[n - 1].distance - pts[0].distance) / 1000.0;
    s.climbPerKm = km > 1.0 ? climb / km : 0.0;
    return s;
}

inline const char *name(Biome b)
{
    switch (b) {
    case Biome::Tropical: return "tropical";
    case Biome::Australian: return "australian";
    case Biome::Desert: return "desert";
    case Biome::Icy: return "icy";
    case Biome::Mountain: return "mountain";
    default: return "nordic";
    }
}

// "nordic", "desert", ... (case-sensitive); false if unknown.
inline bool fromName(const char *s, Biome *out)
{
    for (int i = 0; i <= int(Biome::Mountain); ++i)
        if (s && std::strcmp(s, name(Biome(i))) == 0) {
            *out = Biome(i);
            return true;
        }
    return false;
}

// The profile of a biome; the road's elevation range (m) places the snow
// line and tree line of the mountains.
inline BiomeProfile profile(Biome b, double minElevation = 0, double meanElevation = 0, double maxElevation = 0)
{
    BiomeProfile p;   // the defaults: the Nordic temperate countryside
    p.biome = b;
    auto set = [](float (&w)[kTreeKinds], std::initializer_list<float> v) {
        std::fill(std::begin(w), std::end(w), 0.f);
        std::copy(v.begin(), v.end(), std::begin(w));
    };
    switch (b) {
    case Biome::Nordic:
        break;
    case Biome::Tropical:
        p.zenith = { .20f, .48f, .82f };
        p.horizon = { .78f, .86f, .90f };
        p.sunGlow = { 1.f, .92f, .74f };
        p.sunLight = { 1.f, .97f, .88f };
        p.ambientSky = { .52f, .60f, .64f };
        p.ambientGround = { .27f, .31f, .20f };
        p.sun[0] = -.35f; p.sun[1] = .85f; p.sun[2] = .40f;
        p.cloudShift = -.04f;
        p.grassTint = { .74f, .94f, .66f };
        p.sandTint = { 1.08f, 1.04f, .96f };
        p.waterShallow = { .16f, .52f, .50f };
        p.waterDeep = { .03f, .26f, .38f };
        p.ringFar = { .38f, .52f, .58f };
        p.ringNear = { .18f, .36f, .18f };
        p.ringScale = 1.2f;
        p.treeOpen = .055;
        p.treeForest = .85;
        //               Spruce Broadl Bush  Palm  Cactus Eucal Acacia
        set(p.forestMix, { 0,   .55f, .15f, .30f, 0,     0,    0 });
        set(p.openMix,   { 0,   .25f, .35f, .40f, 0,     0,    0 });
        p.leafTint = { .80f, 1.10f, .80f };
        p.flowers = 1.3;
        p.animals = false; p.bales = false; p.tractors = false; p.turbines = false; p.fences = false;
        p.flockBirds[0] = Bird::Parrot; p.flockBirds[1] = Bird::Egret; p.flockBirds[2] = Bird::Parrot;
        p.flockChance = .50f;
        p.raptor = Bird::Kite;
        p.raptorChance = .40f;
        p.perchBirds[0] = Bird::BeeEater; p.perchBirds[1] = Bird::Parrot;
        p.walls[0] = { .93f, .88f, .70f }; p.walls[1] = { .55f, .80f, .74f };
        p.walls[2] = { .92f, .62f, .48f }; p.walls[3] = { .90f, .90f, .86f };
        p.roofs[0] = { .62f, .28f, .17f }; p.roofs[1] = { .55f, .24f, .15f }; p.roofs[2] = { .48f, .40f, .26f };
        p.roofs[3] = { .55f, .55f, .52f }; p.roofs[4] = { .30f, .45f, .40f };
        break;
    case Biome::Australian:
        p.zenith = { .22f, .45f, .80f };
        p.horizon = { .80f, .82f, .82f };
        p.sunGlow = { 1.f, .84f, .58f };
        p.sunLight = { 1.f, .93f, .78f };
        p.ambientSky = { .55f, .57f, .60f };
        p.ambientGround = { .36f, .28f, .20f };
        p.sun[0] = -.50f; p.sun[1] = .65f; p.sun[2] = -.57f;   // the southern hemisphere: the sun in the north
        p.cloudShift = .10f;
        p.grassTint = { 1.24f, 1.00f, .58f };
        p.sandTint = { 1.00f, .66f, .50f };    // red-ochre earth
        p.sandCover = .38f;
        p.waterShallow = { .30f, .36f, .28f };
        p.waterDeep = { .10f, .18f, .20f };
        p.ringFar = { .55f, .52f, .56f };
        p.ringNear = { .45f, .38f, .26f };
        p.lakeChance = .25;
        p.treeOpen = .03;
        p.treeForest = .40;
        set(p.forestMix, { 0,   0,    .15f, 0,    0,     .70f, .15f });
        set(p.openMix,   { 0,   0,    .45f, 0,    0,     .35f, .20f });
        p.leafTint = { 1.10f, 1.00f, .80f };
        p.flowers = .25;
        p.rocks = 1.6;
        p.buildings = .5f;
        p.animals = false; p.bales = false; p.turbines = false;
        p.flockBirds[0] = Bird::Cockatoo; p.flockBirds[1] = Bird::Galah; p.flockBirds[2] = Bird::Galah;
        p.flockChance = .50f;
        p.raptor = Bird::WedgeTail;
        p.raptorChance = .50f;
        p.perchBirds[0] = Bird::Lorikeet; p.perchBirds[1] = Bird::Magpie;   // (the Australian magpie looks alike)
        p.walls[0] = { .88f, .86f, .80f }; p.walls[1] = { .80f, .68f, .50f };
        p.walls[2] = { .62f, .36f, .24f }; p.walls[3] = { .70f, .72f, .70f };
        for (Rgb &r : p.roofs) r = { .60f, .62f, .62f };   // corrugated iron
        p.roofs[0] = { .55f, .30f, .22f }; p.roofs[4] = { .30f, .40f, .36f };
        break;
    case Biome::Desert:
        p.zenith = { .30f, .50f, .78f };
        p.horizon = { .88f, .84f, .74f };
        p.sunGlow = { 1.f, .90f, .66f };
        p.sunLight = { 1.f, .95f, .82f };
        p.ambientSky = { .58f, .58f, .58f };
        p.ambientGround = { .42f, .34f, .24f };
        p.sun[0] = -.45f; p.sun[1] = .75f; p.sun[2] = .48f;
        p.cloudShift = .20f;
        p.grassTint = { 1.30f, 1.02f, .58f };
        p.sandTint = { 1.10f, .96f, .78f };
        p.sandCover = .90f;
        p.waterShallow = { .20f, .45f, .40f };
        p.waterDeep = { .05f, .25f, .32f };
        p.ringFar = { .70f, .62f, .55f };
        p.ringNear = { .64f, .52f, .38f };
        p.ringScale = 1.3f;
        p.hillScale = 1.15;
        p.lakeChance = .05;   // a rare oasis
        p.treeOpen = .010;
        p.treeForest = .07;
        set(p.forestMix, { 0,   0,    .30f, .20f, .25f,  0,    .25f });
        set(p.openMix,   { 0,   0,    .55f, .05f, .30f,  0,    .10f });
        p.leafTint = { 1.25f, 1.00f, .70f };
        p.flowers = 0;
        p.rocks = 2.5;
        p.buildings = .25f;
        p.animals = false; p.bales = false; p.tractors = false; p.turbines = false; p.fences = false;
        p.flockBirds[0] = Bird::Crow; p.flockBirds[1] = Bird::Crow; p.flockBirds[2] = Bird::Crow;   // desert ravens
        p.flockChance = .08f;
        p.raptor = Bird::Vulture;
        p.raptorChance = .45f;
        p.perchBirds[0] = Bird::Sparrow; p.perchBirds[1] = Bird::Sparrow;
        p.perchChance = .45f;   // (few trees)
        p.walls[0] = { .82f, .70f, .52f }; p.walls[1] = { .88f, .80f, .64f };
        p.walls[2] = { .74f, .52f, .36f }; p.walls[3] = { .90f, .88f, .82f };
        for (Rgb &r : p.roofs) r = { .70f, .58f, .42f };   // earthen
        p.roofs[0] = { .62f, .36f, .24f };
        break;
    case Biome::Icy:
        p.zenith = { .34f, .50f, .72f };
        p.horizon = { .80f, .85f, .90f };
        p.sunGlow = { 1.f, .80f, .60f };
        p.sunLight = { 1.f, .90f, .78f };
        p.ambientSky = { .58f, .64f, .74f };
        p.ambientGround = { .40f, .42f, .46f };
        p.sun[0] = -.60f; p.sun[1] = .24f; p.sun[2] = .76f;   // a low polar sun
        p.cloudShift = -.03f;
        p.snowCover = 1;
        p.treeSnow = .9f;
        p.iceWater = true;
        p.ringFar = { .62f, .68f, .76f };
        p.ringNear = { .70f, .74f, .80f };
        p.ringSnow = true;
        p.ringScale = 1.2f;
        p.treeOpen = .008;
        p.treeForest = .30;
        set(p.forestMix, { .95f, .05f, 0,   0,    0,     0,    0 });
        set(p.openMix,   { .90f, .10f, 0,   0,    0,     0,    0 });
        p.treeScale = .65;
        p.flowers = 0;
        p.rocks = 1.5;
        p.buildings = .3f;
        p.animals = false; p.bales = false; p.tractors = false; p.turbines = false;
        p.flockBirds[0] = Bird::Gull; p.flockBirds[1] = Bird::Gull; p.flockBirds[2] = Bird::Crow;   // gulls and ravens
        p.flockChance = .15f;
        p.raptorChance = .25f;   // sea eagles (Greenland, Norway)
        p.perchBirds[0] = Bird::Bunting; p.perchBirds[1] = Bird::Bunting;   // snow buntings
        p.walls[0] = { .62f, .18f, .14f }; p.walls[1] = { .86f, .70f, .30f };
        p.walls[2] = { .20f, .30f, .45f }; p.walls[3] = { .85f, .85f, .82f };
        break;
    case Biome::Mountain: {
        p.zenith = { .20f, .42f, .78f };   // thin, deep-blue air
        p.horizon = { .70f, .79f, .88f };
        p.sun[0] = -.55f; p.sun[1] = .60f; p.sun[2] = .60f;
        p.grassTint = { .92f, 1.0f, .90f };
        p.ringFar = { .48f, .55f, .66f };
        p.ringNear = { .30f, .38f, .32f };
        p.ringScale = 1.8f;
        p.ringSnow = true;
        p.hillScale = 1.5;
        p.lakeChance = .35;
        p.treeOpen = .03;
        p.treeForest = .70;
        set(p.forestMix, { .85f, .15f, 0,   0,    0,     0,    0 });
        set(p.openMix,   { .45f, .20f, .35f, 0,   0,     0,    0 });
        p.rocks = 3;
        p.flowers = .8;
        p.bales = false; p.turbines = false; p.tractors = false;
        p.flockBirds[0] = Bird::Chough; p.flockBirds[1] = Bird::Chough; p.flockBirds[2] = Bird::Crow;
        p.flockChance = .45f;
        p.raptor = Bird::GoldenEagle;
        p.raptorChance = .55f;
        p.perchBirds[0] = Bird::Finch; p.perchBirds[1] = Bird::Chough;
        // snow on the peaks around, and on the road where it climbs highest
        const double top = std::max(meanElevation, maxElevation);
        // (never below alpine heights: a flat high plateau - Denver, Nairobi - gets no snow;
        // the trees never thin out at the road's own level)
        p.snowLine = float(std::max({ 2300.0, minElevation + 150, meanElevation + .35 * (top - meanElevation) + 50 }));
        p.treeLine = std::max(double(p.snowLine) - 120, minElevation + 200);
        // no ground near the road reaches it (the hills beside the road rise
        // at most ~120 m * hillScale above it): no snow work in the shader
        // (the distant peaks keep theirs: ringSnow)
        if (top + 200 * p.hillScale + 50 < p.snowLine)
            p.snowLine = 1e5f;
        p.roofs[0] = { .36f, .24f, .18f }; p.roofs[2] = { .40f, .36f, .32f };   // dark wood and slate
        p.walls[1] = { .58f, .42f, .28f };   // timber
        break;
    }
    }
    return p;
}

} // namespace Biomes
} // namespace route3d
