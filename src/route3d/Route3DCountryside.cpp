// Route3DScene: the countryside's geometry (countryside.h places it): farm
// animals, fences and field gates, hay bales, tractors, wind turbines,
// benches, reeds, jetties and rowing boats. Low poly and flat
// coloured like the rest of the world (no textures); a few dozen to ~200
// triangles each. Writes with the short put() (m_target).
#include "Route3DScene.h"

#include "world_noise.h"

#include <algorithm>
#include <cmath>
#include <functional>

using namespace route3d;

namespace watt {

namespace {
const QVector3D kUp(0, 1, 0);

QVector3D mixColor(QVector3D a, QVector3D b, float t)
{
    return a + (b - a) * t;
}
} // namespace

void Route3DScene::box(QVector3D c, QVector3D hx, QVector3D hy, QVector3D hz, QVector3D col, int material, float blendLo,
                       float blendHi)
{
    const QVector3D ax[3] = { hx, hy, hz };
    for (int a = 0; a < 3; ++a) {
        const QVector3D A = ax[a], B = ax[(a + 1) % 3], C = ax[(a + 2) % 3];
        const QVector3D n = A.normalized();
        for (float s : { -1.f, 1.f }) {
            const QVector3D f = c + A * s;
            const QVector3D p[4] = { f - B - C, f + B - C, f + B + C, f - B + C };
            float bl[4];
            for (int k = 0; k < 4; ++k) {
                // which end of hy the corner lies at
                const float t = QVector3D::dotProduct(p[k] - c, hy) >= 0 ? 1.f : 0.f;
                bl[k] = blendLo + (blendHi - blendLo) * t;
            }
            const QVector3D nn = n * s;
            const QVector3D shade = col * (nn.y() > .5f ? 1.04f : nn.y() < -.5f ? .7f : 1.f);
            for (int k : { 0, 1, 2, 0, 2, 3 })
                put(p[k], nn, shade, material, bl[k]);
        }
    }
}

void Route3DScene::limb(QVector3D a, QVector3D b, float half, QVector3D col, int material, float blendA, float blendB)
{
    const QVector3D d = b - a;
    if (d.lengthSquared() < 1e-8f)
        return;
    QVector3D x = QVector3D::crossProduct(d, kUp);
    if (x.lengthSquared() < 1e-8f)
        x = QVector3D(1, 0, 0);
    x.normalize();
    const QVector3D z = QVector3D::crossProduct(x, d).normalized();
    box((a + b) * .5f, x * half, d * .5f, z * half, col, material, blendA, blendB);
}

void Route3DScene::cylinder(QVector3D a, QVector3D b, float r, int sides, QVector3D side, QVector3D cap)
{
    const QVector3D axis = (b - a).normalized();
    QVector3D u = QVector3D::crossProduct(axis, kUp);
    if (u.lengthSquared() < 1e-6f)
        u = QVector3D(1, 0, 0);
    u.normalize();
    const QVector3D w = QVector3D::crossProduct(axis, u);
    for (int i = 0; i < sides; ++i) {
        const float a0 = float(6.2831853 * i / sides), a1 = float(6.2831853 * (i + 1) / sides);
        const QVector3D n0 = u * std::cos(a0) + w * std::sin(a0), n1 = u * std::cos(a1) + w * std::sin(a1);
        put(a + n0 * r, n0, side, Solid, 0);
        put(b + n0 * r, n0, side, Solid, 0);
        put(b + n1 * r, n1, side, Solid, 0);
        put(a + n0 * r, n0, side, Solid, 0);
        put(b + n1 * r, n1, side, Solid, 0);
        put(a + n1 * r, n1, side, Solid, 0);
        for (int e = 0; e < 2; ++e) {   // flat caps
            const QVector3D c = e ? b : a, n = e ? axis : -axis;
            put(c, n, cap, Solid, 0);
            put(c + n0 * r, n, cap, Solid, 0);
            put(c + n1 * r, n, cap, Solid, 0);
        }
    }
}

void Route3DScene::ellipsoid(QVector3D c, QVector3D ax, QVector3D ay, QVector3D az, int rings, int sides,
                             const std::function<QVector3D(QVector3D)> &color)
{
    const float lx = ax.lengthSquared(), ly = ay.lengthSquared(), lz = az.lengthSquared();
    if (lx < 1e-10f || ly < 1e-10f || lz < 1e-10f)
        return;
    struct V { QVector3D p, n, c; };
    std::vector<V> pts;
    pts.reserve(size_t((rings + 1) * (sides + 1)));
    for (int i = 0; i <= rings; ++i)
        for (int k = 0; k <= sides; ++k) {
            const float t = float(3.14159265 * i / rings), f = float(6.2831853 * k / sides);
            const QVector3D d(std::sin(t) * std::cos(f), std::cos(t), std::sin(t) * std::sin(f));
            const QVector3D n = (ax * (d.x() / lx) + ay * (d.y() / ly) + az * (d.z() / lz)).normalized();
            pts.push_back({ c + ax * d.x() + ay * d.y() + az * d.z(), n, color(d) });
        }
    auto P = [&](int i, int k) -> const V & { return pts[size_t(i * (sides + 1) + k)]; };
    for (int i = 0; i < rings; ++i)
        for (int k = 0; k < sides; ++k) {
            const V &a = P(i, k), &b = P(i, k + 1), &cc = P(i + 1, k + 1), &d = P(i + 1, k);
            if (i > 0) {
                put(a.p, a.n, a.c, Solid, 0);
                put(b.p, b.n, b.c, Solid, 0);
                put(cc.p, cc.n, cc.c, Solid, 0);
            }
            if (i + 1 < rings) {
                put(a.p, a.n, a.c, Solid, 0);
                put(cc.p, cc.n, cc.c, Solid, 0);
                put(d.p, d.n, d.c, Solid, 0);
            }
        }
}

void Route3DScene::taper(QVector3D a, QVector3D b, float r0, float r1, int sides, QVector3D c0, QVector3D c1)
{
    const QVector3D axis = (b - a).normalized();
    QVector3D u = QVector3D::crossProduct(axis, kUp);
    if (u.lengthSquared() < 1e-6f)
        u = QVector3D(1, 0, 0);
    u.normalize();
    const QVector3D w = QVector3D::crossProduct(axis, u);
    for (int i = 0; i < sides; ++i) {
        const float a0 = float(6.2831853 * i / sides), a1 = float(6.2831853 * (i + 1) / sides);
        const QVector3D n0 = u * std::cos(a0) + w * std::sin(a0), n1 = u * std::cos(a1) + w * std::sin(a1);
        put(a + n0 * r0, n0, c0, Solid, 0);
        put(b + n0 * r1, n0, c1, Solid, 0);
        put(b + n1 * r1, n1, c1, Solid, 0);
        put(a + n0 * r0, n0, c0, Solid, 0);
        put(b + n1 * r1, n1, c1, Solid, 0);
        put(a + n1 * r0, n1, c0, Solid, 0);
    }
}

// Cows (black-and-white Holstein, red Danish), sheep and horses: rounded
// bodies and heads (low-poly ellipsoids), tapered legs, the head down when
// grazing. Far ones are coarser, without ears, horns, tail and udder, with
// one thin 3-sided leg each; beyond kAnimalLegsM no legs (under a pixel).
void Route3DScene::animal(const AnimalSpec &s, bool far, bool legless)
{
    const QVector3D f(std::cos(float(s.heading)), 0, std::sin(float(s.heading)));
    const QVector3D r = QVector3D::crossProduct(kUp, f);
    const QVector3D base(float(s.x), float(s.y), float(s.z));
    const uint32_t seed = s.seed;
    const float size = .92f + .16f * rnd(seed, 1);
    auto P = [&](float x, float y, float z) { return base + (f * x + kUp * y + r * z) * size; };
    const int rings = far ? 4 : 6, sides = far ? 6 : 9, legSides = far ? 3 : 6;
    auto solid = [](QVector3D c) { return [c](QVector3D) { return c; }; };
    // an ellipsoid along dir (half length), round across (half widths h up, w sideways)
    auto blob = [&](QVector3D c, QVector3D dir, float len, float h, float w, const std::function<QVector3D(QVector3D)> &col,
                    int rg = -1, int sd = -1) {
        dir.normalize();
        QVector3D up = kUp - dir * QVector3D::dotProduct(kUp, dir);
        up = up.lengthSquared() > 1e-6f ? up.normalized() : f;
        const QVector3D side = QVector3D::crossProduct(dir, up);
        ellipsoid(c, dir * (len * size), up * (h * size), side * (w * size), rg > 0 ? rg : rings, sd > 0 ? sd : sides, col);
    };
    auto leg = [&](float x, float z, float top, float r0, float r1, QVector3D col, QVector3D hoof) {
        if (legless)
            return;
        const QVector3D a = P(x, top, z), knee = P(x, .10f, z), g = P(x, -.03f, z);
        if (far) {   // one piece
            taper(a, g, r0 * size, r1 * size, legSides, col, hoof);
            return;
        }
        taper(a, knee, r0 * size, r1 * size, legSides, col, col * .92f);
        taper(knee, g, r1 * size, r1 * 1.15f * size, legSides, hoof, hoof);
    };
    if (s.kind == Sheep) {
        const QVector3D wool = QVector3D(.86f, .84f, .77f) * (.92f + .1f * rnd(seed, 2)), dark(.17f, .15f, .14f);
        // woolly: a little light and shade over the fleece
        auto fleece = [&](QVector3D d) {
            return wool * float(.90 + .16 * WorldNoise::value(d.x() * 3.1 + seed % 53, d.z() * 3.1 + d.y() * 2.3, seed));
        };
        blob(P(0, .66f, 0), f, .56f, .34f, .33f, fleece);
        for (float x : { -.34f, .34f })
            for (float z : { -.15f, .15f })
                leg(x, z, .45f, .055f, .04f, dark, dark * .8f);
        const QVector3D neck = P(.45f, .74f, 0);
        const QVector3D head = s.grazing ? P(.70f, .34f, 0) : P(.72f, .82f, 0);
        blob((neck + head) * .5f, head - neck, .2f, .17f, .16f, fleece, far ? 3 : 4, far ? 5 : 7);
        const QVector3D dir = s.grazing ? (f * .5f - kUp).normalized() : (f - kUp * .35f).normalized();
        if (far)
            return;
        blob(head + dir * (.12f * size), dir, .17f, .10f, .085f, solid(dark), 4, 7);
        if (!far)
            for (float z : { -1.f, 1.f })
                blob(head + r * (z * .12f * size) + kUp * (.05f * size), r * z, .08f, .025f, .04f, solid(dark), 3, 5);
        return;
    }
    if (s.kind == Horse) {
        static const QVector3D coats[] = { { .40f, .25f, .14f }, { .55f, .30f, .15f }, { .10f, .09f, .09f }, { .66f, .64f, .61f } };
        const QVector3D coat = coats[int(rnd(seed, 3) * 4) % 4], mane(.08f, .07f, .06f), hoof(.12f, .11f, .10f);
        blob(P(0, 1.30f, 0), f, .68f, .34f, .29f, solid(coat));
        blob(P(.38f, 1.30f, 0), f, .36f, .38f, .31f, solid(coat));    // chest
        blob(P(-.40f, 1.34f, 0), f, .36f, .37f, .32f, solid(coat));   // hindquarters
        for (float x : { -.48f, .46f })
            for (float z : { -.17f, .17f })
                leg(x, z, 1.15f, .085f, .05f, coat * .95f, hoof);
        const QVector3D neck0 = P(.56f, 1.45f, 0);
        const QVector3D neck1 = s.grazing ? P(.98f, .80f, 0) : P(.90f, 1.98f, 0);
        taper(neck0, neck1, .22f * size, .13f * size, sides, coat, coat);
        const QVector3D dir = s.grazing ? (f * .35f - kUp).normalized() : (f - kUp * .55f).normalized();
        blob(neck1 + dir * (.24f * size), dir, .32f, .12f, .10f, solid(coat));
        if (!far) {
            blob(neck1 + dir * (.50f * size), dir, .10f, .09f, .085f, solid(coat * .55f), 4, 7);
            taper(neck0 + kUp * (.17f * size), neck1 + kUp * (.11f * size), .06f * size, .04f * size, 4, mane, mane);
            taper(P(-.74f, 1.48f, 0), P(-.88f, .78f, 0), .08f * size, .035f * size, 5, mane, mane);
            for (float z : { -.06f, .06f })
                taper(neck1 + r * (z * size), neck1 + (r * z + kUp * .16f - f * .05f) * size, .03f * size, .005f * size, 4,
                      coat, coat);
        }
        return;
    }
    // cows
    const bool holstein = s.kind == Holstein;
    const QVector3D white(.92f, .91f, .88f), black(.10f, .10f, .10f), red(.47f, .17f, .10f), hoof(.12f, .11f, .10f);
    // Holstein: soft black patches on white (per-vertex colour, so the edges blend)
    auto patches = [&, holstein](uint32_t salt) {
        return [=](QVector3D d) {
            if (!holstein)
                return red;
            const double n = WorldNoise::value(d.x() * 2.6 + d.y() * 1.4 + (seed + salt) % 97, d.z() * 2.6 - d.y() * 1.2,
                                               seed + salt);
            const float t = float(WorldNoise::smoothstep(.40, .60, n));
            return mixColor(white, black, t);
        };
    };
    blob(P(0, 1.00f, 0), f, .80f, .42f, .38f, patches(0));
    blob(P(.40f, 1.06f, 0), f, .38f, .40f, .36f, patches(5), far ? 3 : 5, sides);   // shoulders
    for (float x : { -.54f, .55f })
        for (float z : { -.22f, .22f })
            leg(x, z, .85f, .11f, .07f, holstein ? white : red * .9f, hoof);
    const QVector3D neck0 = P(.74f, 1.15f, 0);
    const QVector3D head = s.grazing ? P(1.04f, .45f, 0) : P(1.10f, 1.18f, 0);
    taper(neck0, head, .24f * size, .17f * size, sides, holstein ? white : red, holstein ? black : red);
    const QVector3D dir = s.grazing ? (f * .4f - kUp).normalized() : (f - kUp * .3f).normalized();
    blob(head + dir * (.15f * size), dir, .30f, .17f, .15f, patches(11));
    if (far)
        return;
    blob(head + dir * (.42f * size), dir, .09f, .11f, .13f, solid(QVector3D(.78f, .55f, .52f)), 4, 7);
    for (float z : { -1.f, 1.f }) {
        blob(head + r * (z * .20f * size) + kUp * (.06f * size), r * z, .10f, .035f, .06f, patches(17), 3, 5);   // ears
        taper(head + (r * (z * .08f) + kUp * .14f) * size, head + (r * (z * .17f) + kUp * .25f) * size, .03f * size,
              .01f * size, 4, QVector3D(.85f, .82f, .70f), QVector3D(.85f, .82f, .70f));   // horns
    }
    taper(P(-.78f, 1.30f, 0), P(-.84f, .55f, 0), .04f * size, .025f * size, 4, holstein ? white : red, holstein ? white : red);
    blob(P(-.84f, .50f, 0), kUp, .08f, .04f, .04f, solid(holstein ? black : red * .6f), 3, 5);   // tail tuft
    blob(P(-.27f, .58f, 0), f, .16f, .10f, .15f, solid(QVector3D(.86f, .62f, .60f)), 3, 6);       // udder
}

void Route3DScene::bale(const BaleSpec &s)
{
    const QVector3D f(std::cos(float(s.heading)), 0, std::sin(float(s.heading)));
    const QVector3D r = QVector3D::crossProduct(kUp, f);
    const QVector3D base(float(s.x), float(s.y), float(s.z));
    const float tone = .92f + .14f * rnd(s.seed, 1);
    if (s.kind == SquareStack) {
        const QVector3D straw = QVector3D(.80f, .69f, .40f) * tone;
        const int count = 2 + int(rnd(s.seed, 2) * 3);
        for (int k = 0; k < count; ++k) {
            const int level = k / 2, slot = k % 2;
            const QVector3D c = base + kUp * (.18f + .36f * level) + r * (slot ? .25f : -.25f) * (level ? 0.f : 1.f)
                                + f * (level ? .1f : 0.f);
            box(c, f * .52f, kUp * .18f, r * .23f, straw * (.94f + .1f * rnd(s.seed, 3 + k)));
        }
        return;
    }
    static const QVector3D wraps[] = { { .90f, .91f, .88f }, { .30f, .42f, .25f }, { .10f, .10f, .10f } };
    const bool straw = s.kind == RoundStraw;
    const QVector3D side = straw ? QVector3D(.78f, .66f, .36f) * tone : wraps[s.color % 3];
    const QVector3D cap = straw ? QVector3D(.68f, .56f, .29f) * tone : wraps[s.color % 3] * .9f;
    const QVector3D c = base + kUp * .72f;
    cylinder(c - f * .6f, c + f * .6f, .75f, 10, side, cap);
}

// A farm tractor: big rear wheels, small front wheels, bonnet, glass cab.
void Route3DScene::tractor(const TractorSpec &s, bool far)
{
    static const QVector3D paints[] = { { .75f, .12f, .08f }, { .12f, .45f, .15f }, { .10f, .28f, .65f }, { .85f, .62f, .10f } };
    const QVector3D paint = paints[s.color % 4];
    const QVector3D f(std::cos(float(s.heading)), 0, std::sin(float(s.heading)));
    const QVector3D r = QVector3D::crossProduct(kUp, f);
    const QVector3D base(float(s.x), float(s.y), float(s.z));
    auto P = [&](float x, float y, float z) { return base + f * x + kUp * y + r * z; };
    const QVector3D tyre(.09f, .09f, .09f), rim = s.color == 1 ? QVector3D(.85f, .70f, .10f) : QVector3D(.80f, .78f, .74f);
    const QVector3D dark(.16f, .16f, .17f), glass(.24f, .31f, .36f);
    const int sides = far ? 8 : 12;
    for (float z : { -1.f, 1.f }) {
        cylinder(P(-.55f, .80f, z * .62f), P(-.55f, .80f, z * 1.08f), .80f, sides, tyre, rim);
        cylinder(P(1.30f, .48f, z * .55f), P(1.30f, .48f, z * .85f), .48f, sides, tyre, rim);
        box(P(-.55f, 1.62f, z * .85f), f * .72f, kUp * .05f, r * .26f, paint);   // mudguards
    }
    box(P(.65f, 1.05f, 0), f * .95f, kUp * .32f, r * .38f, paint);    // bonnet
    box(P(.55f, .62f, 0), f * .75f, kUp * .18f, r * .30f, dark);      // engine, axle
    box(P(1.62f, 1.0f, 0), f * .07f, kUp * .26f, r * .32f, dark);     // grille
    box(P(-.55f, 1.95f, 0), f * .55f, kUp * .55f, r * .55f, glass);   // cab
    box(P(-.55f, 2.54f, 0), f * .64f, kUp * .06f, r * .64f, s.color == 3 ? dark : paint);
    if (!far) {
        for (float x : { -1.08f, -.02f })
            for (float z : { -.53f, .53f })
                box(P(x, 1.95f, z), f * .04f, kUp * .56f, r * .04f, dark);   // cab pillars
        limb(P(1.0f, 1.35f, .24f), P(1.0f, 2.35f, .24f), .05f, dark);       // exhaust
    }
}

// A wind turbine: white tapered tower, nacelle, hub; the blades turn in the
// vertex shader (Rotor). Rotors face the prevailing west-south-west wind.
void Route3DScene::turbine(const TurbineSpec &s, bool far)
{
    const QVector3D white(.90f, .91f, .92f);
    const QVector3D foot(float(s.x), float(s.y), float(s.z));
    const float hub = float(s.hub), rotor = float(s.rotor);
    trunk(foot, hub - 1.2f, 2.1f, 1.25f, far ? 6 : 10, white);
    const QVector3D axis = QVector3D(-.93f, 0, .37f).normalized();
    const QVector3D side = QVector3D::crossProduct(kUp, axis);
    const QVector3D top = foot + kUp * hub;
    box(top - axis * 1.0f, axis * 3.4f, kUp * 1.4f, side * 1.3f, white * .97f);   // nacelle
    const QVector3D centre = top + axis * 2.9f;
    box(centre, axis * .9f, kUp * .9f, side * .9f, white);                         // hub
    const float phase = float(s.phase);
    for (int k = 0; k < 3; ++k) {
        const float a = float(6.2831853 * k / 3);
        const QVector3D u = kUp * std::cos(a) + side * std::sin(a);
        const QVector3D w = QVector3D::crossProduct(axis, u);
        const QVector3D c = centre + axis * .35f;
        const QVector3D rootL = c + u * .9f - w * .55f, rootR = c + u * .9f + w * .55f;
        const QVector3D midL = c + u * (rotor * .25f) - w * 1.05f, midR = c + u * (rotor * .25f) + w * .55f;
        const QVector3D tip = c + u * rotor;
        const QVector3D tris[9] = { rootL, rootR, midR, rootL, midR, midL, midL, midR, tip };
        for (const QVector3D &p : tris)   // one face: the fragment shader lights the side in view
            put(p, axis, centre, Rotor, phase);
    }
}

void Route3DScene::bench(const BenchSpec &s)
{
    const QVector3D f(float(s.fx), 0, float(s.fz));   // the way it faces
    const QVector3D r = QVector3D::crossProduct(kUp, f);
    const QVector3D base(float(s.x), float(s.y), float(s.z));
    auto P = [&](float x, float y, float z) { return base + f * x + kUp * y + r * z; };
    const QVector3D wood = QVector3D(.52f, .34f, .19f) * (.9f + .2f * rnd(s.seed, 1)), metal(.18f, .19f, .20f);
    for (float x : { -.13f, 0.f, .13f })
        box(P(x, .46f, 0), f * .055f, kUp * .022f, r * .90f, wood);
    for (float y : { .66f, .84f })
        box(P(-.24f, y, 0), f * .02f, kUp * .055f, r * .90f, wood);
    for (float z : { -.72f, .72f }) {
        limb(P(.17f, -.05f, z), P(.17f, .45f, z), .03f, metal);
        limb(P(-.22f, -.05f, z), P(-.24f, .92f, z), .03f, metal);
        limb(P(-.24f, .44f, z), P(.19f, .44f, z), .025f, metal);
    }
}

void Route3DScene::fenceSegment(QVector3D a, QVector3D b, QVector3D out, int type, bool gate)
{
    const QVector3D galv(.74f, .76f, .77f), wire(.30f, .30f, .28f), tape(.92f, .92f, .88f);
    const QVector3D post = type == 1 ? QVector3D(.88f, .88f, .84f) : QVector3D(.43f, .37f, .29f);
    if (gate) {
        limb(a - kUp * .2f, a + kUp * 1.25f, .08f, post * .9f);
        limb(b - kUp * .2f, b + kUp * 1.25f, .08f, post * .9f);
        const QVector3D a1 = a + (b - a).normalized() * .12f, b1 = b - (b - a).normalized() * .12f;
        for (float y : { .25f, .5f, .75f, 1.0f })
            limb(a1 + kUp * y, b1 + kUp * y, .025f, galv);
        limb(a1 + kUp * .25f, b1 + kUp * 1.0f, .02f, galv);
        return;
    }
    if (type == 1) {   // electric fence: thin white posts, white tape
        limb(a - kUp * .1f, a + kUp * 1.0f, .025f, post);
        for (float y : { .5f, .92f }) {
            const QVector3D p = a + kUp * y, q = b + kUp * y, h = kUp * .02f;
            face(p - h, q - h, q + h, p + h, out, tape);
            face(p - h, q - h, q + h, p + h, -out, tape * .8f);
        }
        return;
    }
    limb(a - kUp * .2f, a + kUp * 1.15f, .06f, post);
    if (type == 0) {   // wooden rails
        for (float y : { .52f, .98f })
            limb(a + kUp * y, b + kUp * y, .04f, QVector3D(.49f, .41f, .31f));
    } else {           // wire
        for (float y : { .38f, .72f, 1.05f })
            limb(a + kUp * y, b + kUp * y, .012f, wire);
    }
}

void Route3DScene::reeds(const ReedClump &c)
{
    const QVector3D base(float(c.x), float(c.y), float(c.z));
    const int blades = 6 + int(rnd(c.seed, 1) * 4);
    for (int k = 0; k < blades; ++k) {
        const float a = rnd(c.seed, 10 + k) * 6.283f, d = rnd(c.seed, 30 + k) * .4f;
        const QVector3D foot = base + QVector3D(std::cos(a) * d, -.05f, std::sin(a) * d);
        const float h = float(c.height) * (.70f + .3f * rnd(c.seed, 50 + k));
        const QVector3D lean = QVector3D(std::cos(a), 0, std::sin(a)) * (h * .12f);
        const QVector3D tip = foot + kUp * h + lean;
        const QVector3D w = QVector3D(-std::sin(a), 0, std::cos(a)) * .035f;
        const QVector3D green = mixColor(QVector3D(.33f, .44f, .17f), QVector3D(.58f, .54f, .30f), rnd(c.seed, 70 + k));
        tri(foot - w, foot + w, tip, green, Foliage, 0, 0, 1);
        if (k % 3 == 0)   // a bulrush head
            limb(tip - kUp * .28f - lean * .1f, tip - kUp * .05f, .03f, QVector3D(.32f, .19f, .09f), Foliage, .8f, 1);
    }
}

void Route3DScene::jetty(const JettySpec &j)
{
    const QVector3D a(float(j.x0), float(j.deck), float(j.z0)), b(float(j.x1), float(j.deck), float(j.z1));
    QVector3D f = b - a;
    const float length = f.length();
    if (length < 1)
        return;
    f /= length;
    const QVector3D r = QVector3D::crossProduct(kUp, f);
    const float half = float(j.width) * .5f;
    const int boards = std::max(1, int(length / 1.0f));
    for (int k = 0; k < boards; ++k) {
        const float t = (k + .5f) * length / boards;
        const QVector3D wood = QVector3D(.55f, .43f, .29f) * (.86f + .22f * rnd(j.seed, 10 + k));
        box(a + f * t, f * (length / boards * .5f - .03f), kUp * .05f, r * half, wood);
    }
    const QVector3D postWood(.30f, .24f, .18f);
    for (float t = .4f; t < length; t += 2.f)
        for (float z : { -half + .08f, half - .08f }) {
            const QVector3D top = a + f * t + r * z;
            const float ground = float(m_terrain.surface(top.x(), top.z())) - .3f;
            limb(top - kUp * .05f, QVector3D(top.x(), std::min(ground, top.y() - .4f), top.z()), .07f, postWood);
        }
    if (j.boat)
        boat(QVector3D(float(j.bx), float(j.by), float(j.bz)), float(j.bheading), j.boatColor, j.seed);
}

// A small rowing boat floating at the waterline: pointed bow, transom
// stern, wooden floor and two thwarts.
void Route3DScene::boat(QVector3D c, float heading, int color, uint32_t seed)
{
    static const QVector3D hulls[] = { { .90f, .90f, .87f }, { .70f, .14f, .10f }, { .16f, .38f, .22f }, { .16f, .30f, .55f } };
    const QVector3D hull = hulls[color % 4], wood(.58f, .43f, .27f);
    const QVector3D f(std::cos(heading), 0, std::sin(heading)), r = QVector3D::crossProduct(kUp, f);
    constexpr int stations = 7;
    const float xs[stations] = { -1.7f, -1.2f, -.6f, 0.f, .6f, 1.25f, 1.8f };
    const float ws[stations] = { .46f, .58f, .65f, .66f, .60f, .40f, 0.f };
    const float top = .32f, bottom = -.16f;
    auto P = [&](float x, float y, float z) { return c + f * x + kUp * y + r * z; };
    const QVector3D mid = P(0, .05f, 0);
    auto panel = [&](QVector3D p0, QVector3D p1, QVector3D p2, QVector3D p3, QVector3D col) {
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0);
        if (n.lengthSquared() < 1e-10f)
            n = QVector3D::crossProduct(p2 - p0, p3 - p0);
        n.normalize();
        if (QVector3D::dotProduct(n, (p0 + p1 + p2 + p3) * .25f - mid) < 0)
            n = -n;
        face(p0, p1, p2, p3, n, col);
    };
    for (int i = 0; i + 1 < stations; ++i)
        for (float side : { -1.f, 1.f }) {
            const QVector3D t0 = P(xs[i], top, side * ws[i]), t1 = P(xs[i + 1], top, side * ws[i + 1]);
            const QVector3D b0 = P(xs[i], bottom, side * ws[i] * .55f), b1 = P(xs[i + 1], bottom, side * ws[i + 1] * .55f);
            panel(t0, t1, b1, b0, hull);
            if (side > 0)
                panel(b0, b1, P(xs[i + 1], bottom, -ws[i + 1] * .55f), P(xs[i], bottom, -ws[i] * .55f), hull * .6f);
        }
    panel(P(xs[0], top, -ws[0]), P(xs[0], top, ws[0]), P(xs[0], bottom, ws[0] * .55f), P(xs[0], bottom, -ws[0] * .55f), hull * .9f);
    face(P(-1.6f, -.02f, -.42f), P(1.3f, -.02f, -.30f), P(1.3f, -.02f, .30f), P(-1.6f, -.02f, .42f), kUp, wood * .85f);
    for (float x : { -.55f, .55f })
        box(P(x, top - .10f, 0), f * .12f, kUp * .025f, r * (x < 0 ? .62f : .58f), wood);
    (void)seed;
}

} // namespace watt
