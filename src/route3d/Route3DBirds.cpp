// The birds of the 3D view: small birds in loose flocks wheeling over the
// fields beside the road, a big bird of prey circling above it (the sea eagle
// over the Nordic countryside), little birds flying from tree to tree near
// it. Which birds and how many: the biome (biome.h). Placed by stretch of
// road and by tree (deterministic: the same road has the same birds), moved
// by the clock, built on the CPU every frame (up to a few thousand vertices).
// They never just vanish in view: they shrink away in the distance and rest
// hidden in the crowns.
#include "Route3DScene.h"
#include "world_noise.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace watt {

using namespace route3d;

namespace {

constexpr double kTwoPi = 6.283185307179586;

struct Look {
    QVector3D body, wing, tip, tail, head;
    float span;      // wingspan (m)
    float chord;     // wing depth / span
    float tipWidth;  // the wing tip's depth / chord (pointed .2 .. broad, fingered .9)
    float bodyLen;   // body length / span
    float tailWidth; // tail half width / body length
    float flapHz, flapAmp;   // wing beats per second, amplitude (rad)
    float glide;     // 0 .. 1: share of the time gliding
    float dihedral;  // wings held up (rad) while gliding
    float speed;     // m/s
    int flock;       // the most birds in a flock
};

const Look &look(int kind)
{
    static const Look looks[] = {
        // Gull: white, grey wings with black tips, long pointed wings
        { { .92f, .93f, .94f }, { .66f, .69f, .73f }, { .10f, .10f, .11f }, { .94f, .94f, .95f }, { .95f, .95f, .96f },
          1.30f, .17f, .25f, .40f, .30f, 2.6f, .65f, .45f, .06f, 11.f, 5 },
        // Crow / raven
        { { .07f, .07f, .08f }, { .08f, .08f, .09f }, { .06f, .06f, .07f }, { .07f, .07f, .08f }, { .07f, .07f, .08f },
          1.00f, .22f, .70f, .46f, .30f, 3.0f, .60f, .20f, .04f, 10.f, 4 },
        // Starling: small, dark, fast, in a bigger, tighter flock
        { { .10f, .10f, .12f }, { .13f, .12f, .14f }, { .10f, .10f, .12f }, { .10f, .10f, .12f }, { .10f, .10f, .12f },
          .40f, .22f, .25f, .50f, .25f, 5.0f, .60f, .25f, .03f, 15.f, 12 },
        // White-tailed sea eagle: broad brown "barn door" wings, pale head, white wedge tail
        { { .30f, .22f, .15f }, { .27f, .20f, .14f }, { .10f, .09f, .08f }, { .95f, .94f, .90f }, { .78f, .72f, .60f },
          2.30f, .25f, .90f, .40f, .38f, 1.4f, .45f, .93f, .07f, 11.f, 1 },
        // Black kite
        { { .32f, .24f, .17f }, { .28f, .21f, .16f }, { .12f, .10f, .09f }, { .36f, .27f, .19f }, { .52f, .46f, .40f },
          1.50f, .20f, .55f, .42f, .40f, 2.0f, .50f, .80f, .05f, 10.f, 1 },
        // Vulture: very broad, fingered wings in a shallow V, pale head
        { { .24f, .19f, .15f }, { .22f, .18f, .14f }, { .10f, .09f, .08f }, { .20f, .16f, .13f }, { .85f, .78f, .70f },
          2.60f, .27f, .95f, .36f, .30f, 1.2f, .35f, .95f, .14f, 10.f, 1 },
        // Cockatoo: white, noisy flocks
        { { .95f, .95f, .92f }, { .94f, .94f, .90f }, { .96f, .92f, .70f }, { .94f, .94f, .90f }, { .96f, .96f, .94f },
          .95f, .22f, .55f, .46f, .30f, 3.5f, .60f, .20f, .04f, 12.f, 8 },
        // Galah: pink body and head, grey wings
        { { .86f, .52f, .58f }, { .62f, .63f, .66f }, { .55f, .56f, .60f }, { .62f, .63f, .66f }, { .92f, .72f, .76f },
          .70f, .22f, .45f, .46f, .30f, 4.0f, .60f, .20f, .04f, 13.f, 10 },
        // Parrot: green, blue tips, red head
        { { .22f, .62f, .25f }, { .18f, .55f, .22f }, { .10f, .25f, .60f }, { .20f, .58f, .22f }, { .80f, .20f, .15f },
          .70f, .20f, .35f, .55f, .20f, 4.0f, .60f, .15f, .03f, 14.f, 6 },
        // Egret: white, slow beats
        { { .96f, .96f, .95f }, { .95f, .95f, .94f }, { .95f, .95f, .94f }, { .95f, .95f, .94f }, { .96f, .96f, .95f },
          1.00f, .22f, .55f, .55f, .20f, 2.0f, .55f, .30f, .04f, 9.f, 5 },
        // Wedge-tailed eagle: dark, broad wings, long wedge tail
        { { .16f, .12f, .09f }, { .15f, .11f, .08f }, { .08f, .07f, .06f }, { .15f, .11f, .08f }, { .38f, .28f, .18f },
          2.30f, .24f, .90f, .42f, .42f, 1.3f, .45f, .92f, .09f, 11.f, 1 },
        // Golden eagle: brown, golden head
        { { .28f, .20f, .12f }, { .25f, .18f, .11f }, { .10f, .08f, .06f }, { .25f, .18f, .12f }, { .62f, .47f, .26f },
          2.10f, .23f, .85f, .40f, .35f, 1.4f, .45f, .90f, .09f, 11.f, 1 },
        // Alpine chough: black, yellow bill
        { { .07f, .07f, .08f }, { .08f, .08f, .09f }, { .06f, .06f, .07f }, { .07f, .07f, .08f }, { .80f, .65f, .10f },
          .80f, .22f, .70f, .45f, .28f, 3.2f, .60f, .35f, .05f, 11.f, 6 },
        // Chaffinch: pinkish brown, blue-grey head, quick bounding flight
        { { .62f, .42f, .36f }, { .30f, .27f, .22f }, { .12f, .12f, .12f }, { .25f, .24f, .22f }, { .45f, .48f, .55f },
          .30f, .24f, .50f, .55f, .25f, 5.f, .65f, .30f, .05f, 9.f, 1 },
        // Magpie: black, white wing tips, long tail
        { { .06f, .06f, .08f }, { .10f, .12f, .18f }, { .90f, .90f, .90f }, { .08f, .10f, .16f }, { .05f, .05f, .06f },
          .55f, .25f, .70f, .80f, .15f, 4.f, .60f, .30f, .04f, 8.f, 1 },
        // House sparrow
        { { .58f, .48f, .36f }, { .45f, .33f, .22f }, { .20f, .16f, .12f }, { .35f, .27f, .19f }, { .40f, .36f, .34f },
          .30f, .24f, .50f, .55f, .25f, 5.f, .65f, .25f, .05f, 9.f, 1 },
        // Rainbow lorikeet: orange breast, green wings, blue head
        { { .95f, .45f, .12f }, { .15f, .60f, .20f }, { .10f, .30f, .70f }, { .20f, .62f, .22f }, { .15f, .25f, .80f },
          .42f, .20f, .35f, .60f, .18f, 5.f, .60f, .10f, .03f, 12.f, 1 },
        // Snow bunting: white, black wing tips
        { { .94f, .94f, .92f }, { .88f, .88f, .86f }, { .08f, .08f, .09f }, { .85f, .85f, .83f }, { .92f, .90f, .85f },
          .34f, .24f, .50f, .52f, .25f, 5.f, .65f, .30f, .05f, 9.f, 1 },
        // Bee-eater: turquoise, green wings, yellow head, pointed wings
        { { .20f, .60f, .62f }, { .40f, .55f, .20f }, { .15f, .30f, .45f }, { .25f, .55f, .40f }, { .90f, .75f, .15f },
          .40f, .20f, .25f, .62f, .15f, 4.5f, .60f, .45f, .04f, 10.f, 1 },
    };
    static_assert(std::size(looks) == Bird::BeeEater + 1, "one look per Bird::Kind, in order");
    return looks[std::clamp(kind, 0, int(std::size(looks)) - 1)];
}

double smooth(double a, double b, double x)
{
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

constexpr float kPi = 3.14159265f;

// Where a little bird rests in a tree, hidden by the leaves: the thick of the
// crown, worked out from the same seed values as the tree's mesh
// (Route3DScene::spruce, broadleaf, palm, eucalyptus, acacia, bush).
QVector3D restingPlace(const TreeSpec &t)
{
    const QVector3D base(float(t.x), float(t.y), float(t.z));
    const float h = float(t.height), R = float(t.radius);
    auto rnd = [&](int k) { return float(WorldNoise::random(int(t.seed & 0x7fffffff), k, 91)); };   // (Route3DScene::rnd)
    switch (t.kind) {
    case Palm: {   // in the frond bases (the crown leans with the trunk's bend)
        const float lean = rnd(1) * 6.283f, bend = h * (.06f + .12f * rnd(2));
        return base + QVector3D(std::cos(lean) * bend, h - .25f, std::sin(lean) * bend);
    }
    case Eucalyptus: {   // the first limb's clump of leaves (drawn near and far)
        const float lean = rnd(4) * 6.283f, spin = rnd(6) * 6.283f;
        const QVector3D fork = base + QVector3D(std::cos(lean) * h * .05f, h * .48f, std::sin(lean) * h * .05f);
        const float a = spin + (rnd(10) - .5f) * .7f;
        const QVector3D end = fork + QVector3D(std::cos(a), 0, std::sin(a)) * (R * (.45f + .35f * rnd(20)))
                              + QVector3D(0, h * (.25f + .2f * rnd(30)), 0);
        return end + QVector3D(0, R * (.38f + .14f * rnd(40)) * .2f, 0);
    }
    case Acacia: return base + QVector3D(0, h * .82f, 0);
    case Spruce: return base + QVector3D(0, h * .55f, 0);
    case Bush: return base + QVector3D(0, h * .55f, 0);
    default: return base + QVector3D(0, h * .63f, 0);   // broadleaf: the crown's centre
    }
}

// a 0..1 number from a seed and a key
float unit(uint32_t seed, uint32_t k)
{
    uint32_t h = seed * 0x9E3779B1u ^ (k + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return float(h >> 8) / 16777216.f;
}

} // namespace

float Route3DScene::BirdGround::at(float x, float z) const
{
    if (n < 2)
        return -1e9f;
    const float gx = x / step - x0, gz = z / step - z0;
    if (!(gx >= 0 && gz >= 0 && gx <= n - 1 && gz <= n - 1))
        return -1e9f;
    const int i = std::min(int(gx), n - 2), j = std::min(int(gz), n - 2);
    const float fx = gx - i, fz = gz - j;
    auto H = [&](int a, int b) { return h[size_t(a * n + b)]; };
    return (H(i, j) * (1 - fz) + H(i, j + 1) * fz) * (1 - fx) + (H(i + 1, j) * (1 - fz) + H(i + 1, j + 1) * fz) * fx;
}

// Pairs of trees near the road for the little birds: from some trees (the
// profile's share) to another tree 10 .. 45 m away, often across the road.
// They rest inside the crowns (hidden by the leaves: never just gone), so
// no cacti. Deterministic (by tree), so the next tile keeps the same pairs.
void Route3DScene::perches(const std::vector<TreeSpec> &trees, const Terrain &terrain, double cx, double cz,
                           const BiomeProfile &profile, std::vector<Perch> &out)
{
    if (profile.perchChance <= 0 || (profile.perchBirds[0] < 0 && profile.perchBirds[1] < 0))
        return;
    struct Top { QVector3D p; float top; uint32_t seed; bool start; };
    std::vector<Top> tops;
    for (const auto &t : trees) {
        const double dc = std::hypot(t.x - cx, t.z - cz);
        if (dc > 250)
            continue;
        if (terrain.nearest(t.x, t.z).distance > 55)
            continue;
        if (t.kind == Cactus)
            continue;
        tops.push_back({ restingPlace(t), float(t.y + t.height), t.seed, dc < 200 });
    }
    for (const auto &from : tops) {
        if (!from.start || unit(from.seed, 1) > profile.perchChance)
            continue;
        const float want = 14 + 24 * unit(from.seed, 2);
        const Top *best = nullptr;
        float bestErr = 1e9f;
        for (const auto &to : tops) {
            const float d = std::hypot(to.p.x() - from.p.x(), to.p.z() - from.p.z());
            if (d < 10 || d > 45 || std::abs(to.p.y() - from.p.y()) > d * .5f)
                continue;
            const float err = std::abs(d - want);
            if (err < bestErr) {
                bestErr = err;
                best = &to;
            }
        }
        if (best)
            out.push_back({ from.p, best->p, std::max(from.top, best->top) + 1.5f, from.seed });
    }
}

void Route3DScene::birds(double distance, const QVector3D &eye, float timeF, std::vector<Vertex> &out)
{
    if (!m_route || m_route->points.size() < 2)
        return;
    const Route &route = *m_route;
    const BiomeProfile &profile = m_profile;
    const double time = timeF;
    const QVector3D kUpAxis(0, 1, 0);
    // drawn larger than life: life-size, a bird 50 m off is a speck of a few pixels
    constexpr float kSeen = 1.6f;

    // the highest ground around (x, z) where the current tile knows it (else very low)
    auto groundAt = [&](float x, float z) { return m_birdGround.at(x, z); };

    // the road at drawn distance d: position and the unit vector to its left
    auto road = [&](double d, QVector3D &left) {
        d = std::clamp(d, 0.0, route.length());
        const auto p = route.at(d), p0 = route.at(std::max(0.0, d - 6)), p1 = route.at(std::min(route.length(), d + 6));
        QVector3D f(float(p1.x - p0.x), 0, float(p1.z - p0.z));
        if (f.lengthSquared() < 1e-6f)
            f = QVector3D(0, 0, -1);
        left = QVector3D::crossProduct(kUpAxis, f.normalized());
        return QVector3D(float(p.x), float(p.y), float(p.z));
    };

    auto tri = [&](const QVector3D &a, const QVector3D &b, const QVector3D &c, const QVector3D &ca, const QVector3D &cb,
                   const QVector3D &cc) {
        QVector3D n = QVector3D::crossProduct(b - a, c - a);
        if (n.lengthSquared() < 1e-12f)
            return;
        n.normalize();
        if (n.y() < 0)
            n = -n;
        // (lit like from above whichever side you see: from below a bird
        // against the bright sky would otherwise be a black speck)
        n = (n + QVector3D(0, .6f, 0)).normalized();
        put(out, a, n, ca, BirdBody, 0);
        put(out, b, n, cb, BirdBody, 0);
        put(out, c, n, cc, BirdBody, 0);
    };

    // One bird at pos flying along forward (horizontal unit vector), banked
    // by bank (rad, > 0: the right wing down), the wings at angle flap (rad,
    // > 0: up). Far birds are simpler. Never just gone: between fadeFrom and
    // fadeTo (metres from the camera) a bird shrinks away to nothing (it
    // is a speck in the haze there), only beyond is it left out.
    auto bird = [&](const Look &look, const QVector3D &pos, const QVector3D &forward, float bank, float flap,
                    float fadeFrom = 600.f, float fadeTo = 800.f) {
        const float dist = (pos - eye).length();
        if (dist >= fadeTo)
            return;
        Look L = look;
        L.span *= 1.f - float(smooth(fadeFrom, fadeTo, dist));
        const bool far = dist > 220.f;
        const QVector3D f = forward;
        QVector3D r = QVector3D::crossProduct(f, kUpAxis).normalized();
        QVector3D u = kUpAxis;
        {   // bank: roll r and u about f
            const float c = std::cos(bank), s = std::sin(bank);
            const QVector3D r2 = r * c - u * s, u2 = u * c + r * s;
            r = r2;
            u = u2;
        }
        auto P = [&](float fw, float rt, float up) { return pos + f * fw + r * rt + u * up; };
        const float len = L.span * L.bodyLen, rad = L.span * .045f, chord = L.span * L.chord;
        // body: a slim double cone (nose -> waist ring -> tail root)
        const QVector3D nose = P(len * .52f, 0, 0), root = P(-len * .30f, 0, 0);
        const QVector3D ring[4] = { P(len * .05f, rad, 0), P(len * .05f, 0, rad), P(len * .05f, -rad, 0), P(len * .05f, 0, -rad * .8f) };
        for (int k = 0; k < 4; ++k) {
            const QVector3D &a = ring[k], &b = ring[(k + 1) % 4];
            tri(nose, a, b, L.head, L.body, L.body);
            if (!far)
                tri(root, b, a, L.body, L.body, L.body);
        }
        // tail: a fan behind the root
        const float tw = len * L.tailWidth;
        tri(root, P(-len * .62f, tw, 0), P(-len * .62f, -tw, 0), L.tail, L.tail, L.tail);
        // wings: an inner part (raised by flap) and an outer part (raised more: the beat bends the wing)
        const float half = L.span * .5f, inner = half * .45f, outer = half * .55f - rad;
        const float a1 = flap, a2 = flap * 1.45f;
        for (float side : { -1.f, 1.f }) {
            const QVector3D d1 = r * (side * std::cos(a1)) + u * std::sin(a1);
            const QVector3D d2 = r * (side * std::cos(a2)) + u * std::sin(a2);
            const QVector3D rf = P(chord * .55f, side * rad, 0), rb = P(-chord * .45f, side * rad, 0);
            const QVector3D ef = rf + d1 * inner, eb = rb + d1 * inner - f * (chord * .06f);
            const float tipDepth = chord * L.tipWidth;
            const QVector3D tf = ef + d2 * outer - f * (chord * (.55f - .45f * L.tipWidth) * .6f);
            const QVector3D tb = tf - f * tipDepth;
            const QVector3D mid = L.wing * .55f + L.tip * .45f;
            tri(rf, ef, eb, L.wing, L.wing, L.wing);
            tri(rf, eb, rb, L.wing, L.wing, L.wing);
            tri(ef, tf, tb, L.wing, L.tip, L.tip);
            if (!far)
                tri(ef, tb, eb, L.wing, mid, L.wing);
            else
                tri(ef, tb, eb, L.wing, L.tip, L.wing);
        }
    };

    // the wings now: flapping for a while, then gliding (seed: per bird)
    auto wings = [&](const Look &L, double seed) {
        const double gate = std::sin(time * (.18 + .12 * std::fmod(seed * 7.3, 1.0)) + seed * kTwoPi) - (2 * L.glide - 1);
        const double flapping = smooth(-.35, .35, gate);
        const double beat = std::sin(time * L.flapHz * kTwoPi + seed * 13.1);
        return float(L.dihedral + flapping * L.flapAmp * beat);
    };

    // Flocks: maybe one every 250 m of road, wheeling in a wandering loop
    // over the fields beside it, low enough to see.
    if (profile.flockChance > 0) {
        constexpr double kCell = 250;
        constexpr double kKeep = 700, kWindow = 900;   // road metres: full size, shrunk away
        for (int c = int(std::floor((distance - kWindow) / kCell)) - 1; c <= int(std::floor((distance + kWindow) / kCell)); ++c) {
            auto r = [&](uint32_t k) { return WorldNoise::random(c, 77, 4100 + k); };
            if (r(0) > profile.flockChance)
                continue;
            const int kind = profile.flockBirds[std::min(2, int(r(1) * 3))];
            if (kind < 0)
                continue;
            const double d = (c + r(2)) * kCell;   // its stretch of road (none beyond the route's ends)
            const float edge = 1.f - float(smooth(kKeep, kWindow, std::abs(d - distance)));
            if (d < 0 || d > route.length() || edge <= 0)
                continue;
            Look L = look(kind);
            L.span *= kSeen * edge;   // (a flock coming into the window grows, leaving it shrinks: never pops)
            QVector3D left;
            const QVector3D at = road(d, left);
            const float lateral = float((r(3) < .5 ? -1 : 1) * (4 + 26 * r(4)));
            QVector3D centre = at + left * lateral;
            // wandering slowly about
            centre += QVector3D(float(20 * std::sin(time * .012 + r(4) * kTwoPi)), 0, float(20 * std::cos(time * .010 + r(5) * kTwoPi)));
            if (std::hypot(centre.x() - eye.x(), centre.z() - eye.z()) > 800 + 100)
                continue;   // (all its birds beyond fadeTo)
            // above the road, more over the fields (they rise away from it)
            const float base = at.y() + float(5 + 9 * r(6)) + std::abs(lateral) * .12f;
            const double cx = centre.x(), cz = centre.z();
            const double R1 = 40 + 35 * r(7), R2 = R1 * (.6 + .35 * r(8)), Rm = .5 * (R1 + R2);
            const double turn = r(9) * kTwoPi, dir = r(10) < .5 ? -1 : 1;
            const double speed = L.speed * .75;   // cruising
            const double w = speed / Rm * dir;
            const int n = 2 + int(r(11) * (L.flock - 1));
            const float bank = float(dir * std::atan(speed * speed / (Rm * 9.81)) * .7);
            const double ct = std::cos(turn), st = std::sin(turn);
            for (int b = 0; b < n; ++b) {
                const double rb = WorldNoise::random(c * 31 + b, 78, 4200), rs = WorldNoise::random(c, 79 + b, 4300);
                const double rv = WorldNoise::random(c + b, 80 - b, 4400);
                const double gap = L.span * (2.5 + 1.5 * rb) * b;   // behind the leader
                const double a = w * time + r(12) * kTwoPi - dir * gap / Rm;
                const double ex = R1 * std::cos(a), ez = R2 * std::sin(a);
                const double vx = -R1 * std::sin(a) * dir, vz = R2 * std::cos(a) * dir;
                QVector3D fwd(float(vx * ct - vz * st), 0, float(vx * st + vz * ct));
                if (fwd.lengthSquared() < 1e-8f)
                    continue;
                fwd.normalize();
                const QVector3D right = QVector3D::crossProduct(fwd, kUpAxis);
                const float side = float((rs - .5) * L.span * 4 * (b > 0));
                const float up = float((rv - .5) * L.span * 3 * (b > 0) + std::sin(time * .25 + rv * 9) * L.span * .4);
                QVector3D pos = QVector3D(float(cx + ex * ct - ez * st), base, float(cz + ex * st + ez * ct)) + right * side;
                pos.setY(std::max(pos.y(), groundAt(pos.x(), pos.z()) + 10.f) + up);   // over the hills, not through them
                bird(L, pos, fwd, bank, wings(L, rb * 17 + b));
            }
        }
    }

    // Birds of prey: maybe one every 700 m of road, circling in a thermal
    // above it (beside the road, in view ahead), drifting slowly.
    if (profile.raptor >= 0 && profile.raptorChance > 0) {
        constexpr double kCell = 700;
        constexpr double kKeep = 850, kWindow = 1100;
        for (int c = int(std::floor((distance - kWindow) / kCell)) - 1; c <= int(std::floor((distance + kWindow) / kCell)); ++c) {
            auto r = [&](uint32_t k) { return WorldNoise::random(c, 91, 5100 + k); };
            if (r(0) > profile.raptorChance)
                continue;
            const double d = (c + .1 + .8 * r(1)) * kCell;
            const float edge = 1.f - float(smooth(kKeep, kWindow, std::abs(d - distance)));
            if (d < 0 || d > route.length() || edge <= 0)
                continue;
            Look L = look(profile.raptor);
            L.span *= kSeen * edge;
            QVector3D left;
            const QVector3D at = road(d, left);
            const float lateral = float((r(2) < .5 ? -1 : 1) * (5 + 40 * r(3)));
            const double cx = at.x() + left.x() * lateral + 35 * std::sin(time * .01 + r(4) * kTwoPi);
            const double cz = at.z() + left.z() * lateral + 35 * std::cos(time * .008 + r(5) * kTwoPi);
            if (std::hypot(cx - eye.x(), cz - eye.z()) > 800 + 70)
                continue;
            const double R = 35 + 25 * r(6), dir = r(7) < .5 ? -1 : 1;
            const double a = L.speed / R * dir * time + r(8) * kTwoPi;
            const float height = at.y() + std::abs(lateral) * .1f + float(18 + 16 * r(9) + 5 * std::sin(time * .04 + r(10) * 6));
            QVector3D pos(float(cx + R * std::cos(a)), height, float(cz + R * std::sin(a)));
            pos.setY(std::max(pos.y(), groundAt(pos.x(), pos.z()) + 25.f));
            const QVector3D fwd = QVector3D(float(-std::sin(a) * dir), 0, float(std::cos(a) * dir)).normalized();
            const float bank = float(dir * std::atan(L.speed * L.speed / (R * 9.81)) * .7);
            bird(L, pos, fwd, bank, wings(L, r(11) * 11));
        }
    }

    // Little birds flying between the trees near the road: up out of one
    // crown, a smooth arc over the treetops (speeding up, slowing down),
    // down into the other crown, a long rest there (hidden in the leaves),
    // back again. Sometimes two together.
    for (const Perch &p : m_perches) {
        const QVector3D mid = (p.a + p.b) * .5f;
        if ((mid - eye).length() > 150.f + .5f * (p.b - p.a).length())
            continue;
        const int kind = profile.perchBirds[unit(p.seed, 3) < .5f ? 0 : 1];
        if (kind < 0)
            continue;
        Look L = look(kind);
        L.span *= kSeen;
        const QVector3D span = p.b - p.a;
        const float len = std::hypot(span.x(), span.z());
        const double speed = std::min(double(L.speed), 7.0);
        const double flight = 1.4 * len / speed, rest = 3.5 + 4.0 * unit(p.seed, 4);   // (1.4: eased)
        const double cycle = 2 * (flight + rest);
        const int n = unit(p.seed, 5) < .3f ? 2 : 1;
        for (int b = 0; b < n; ++b) {
            double t = std::fmod(time + unit(p.seed, 6) * cycle - b * .7, cycle);
            if (t < 0)
                t += cycle;
            bool back = false;
            if (t >= flight + rest) {
                t -= flight + rest;
                back = true;
            }
            if (t >= flight)
                continue;   // resting in the crown
            const float u = float(t / flight);
            const float e = u * u * (3 - 2 * u);   // eased
            const QVector3D from = back ? p.b : p.a, to = back ? p.a : p.b;
            QVector3D pos = from + (to - from) * e;
            // up out of the crown, over the treetops, down into the other crown
            const float lift = std::sqrt(std::max(0.f, std::sin(kPi * std::clamp(e, 0.f, 1.f))));
            pos.setY(pos.y() + (p.peak - pos.y()) * lift);
            QVector3D fwd(to.x() - from.x(), 0, to.z() - from.z());
            fwd.normalize();
            pos += QVector3D::crossProduct(fwd, kUpAxis) * (.8f * b);
            // flapping out of the tree, gliding in with the wings spread
            const float beat = float(std::sin(time * L.flapHz * kTwoPi + b * 1.7));
            const float flapping = 1.f - float(smooth(.6, .8, u));
            bird(L, pos, fwd, 0, L.dihedral + .1f + flapping * L.flapAmp * beat, 110.f, 150.f);
        }
    }
}

} // namespace watt
