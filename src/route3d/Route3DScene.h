#pragma once

#include "biome.h"
#include "ghost_rider.h"
#include "ghost_runner.h"
#include "countryside.h"
#include "terrain.h"
#include "vegetation.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QSize>
#include <QString>
#include <QVector3D>

#include <array>
#include <climits>
#include <functional>
#include <future>
#include <unordered_map>
#include <memory>
#include <vector>

class QOpenGLContext;

namespace watt {

// The runner figure model (runner.glb), null when there is none / it fails.
std::shared_ptr<const route3d::GlbModel> runnerModel();
// The same figure walking (walker.glb), null when there is none.
std::shared_ptr<const route3d::GlbModel> walkerModel();
std::shared_ptr<const route3d::GlbModel> riderModel();

// The 3D world of the Route3D prototype (0.7): terrain, road, houses, trees,
// sky and the built-in rider along a route3d::Route, drawn with OpenGL into
// the currently bound framebuffer (Route3DItem: a QQuickFramebufferObject).
//
// Runs on the render thread with a current context. Works on desktop OpenGL
// >= 3.1 (GLSL 1.40 / 3.30) and OpenGL ES 3.0 (Raspberry Pi): the shaders get
// the matching #version line, attribute locations are bound by name.
// Textures, trees and the rider are always on (the prototype's switches were
// for testing only); the camera height is fixed.
class Route3DScene : protected QOpenGLFunctions
{
public:
    static constexpr float kCameraHeightM = 1.7f;
    static constexpr float kNearPlaneM = .5f;
    static constexpr double kTaperM = 35.0;   // the run-out after the finish narrows to nothing over this

    Route3DScene();
    ~Route3DScene();

    // Compiles the shaders and creates the textures; false + error() when the
    // context cannot do it (e.g. OpenGL below 3.1 / ES below 3.0).
    bool initialize(QOpenGLContext *context);
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }

    // A new route (shared with the GUI thread, never modified). Null clears it.
    void setRoute(std::shared_ptr<const route3d::Route> route);

    // Inflatable arches: a green START arch where the ride starts (startDistance,
    // metres on the drawn road) and a red FINISH arch at the end of the route.
    // START is shown until you have ridden through it, then FINISH - so on a
    // loop (both at one place) you start under green and finish under red.
    void setStartDistance(double drawnMetres);
    // The GPX Ride's sprints (drawn metres): lines across the road, road text
    // and signs (see sprintMarks()); announce < 0: no "SPRINT IN 200 m".
    struct SprintMarks
    {
        double announce = -1, start = 0, finish = 0;
        bool operator==(const SprintMarks &o) const { return announce == o.announce && start == o.start && finish == o.finish; }
    };
    void setSprints(const std::vector<SprintMarks> &marks);
    // The GPX Ride's KOMs (drawn metres): a polka-dot arch at the foot, a red
    // one at the top.
    struct KomMarks
    {
        double start = 0, finish = 0;
        bool operator==(const KomMarks &o) const { return start == o.start && finish == o.finish; }
    };
    void setKoms(const std::vector<KomMarks> &marks);
    // A small white kilometre post on the right-hand verge at each given
    // place: drawn metres, the kilometre shown.
    void setKmPosts(const std::vector<std::array<double, 2>> &posts);
    // GPX Ride loop: a lap board just after the finish line showing the laps
    // finished (also after the turn of a there-and-back loop); < 0: none.
    void setLaps(int laps);
    // Draw a runner (Treadmill Run) instead of the cyclist. In render(),
    // crank is then the stride phase (one turn = a left and a right step).
    void setRunner(bool on);
    void setWalkRunKmh(double kmh) { m_runnerFigure.setTransitionKmh(kmh); }

    // Draws one frame into the bound framebuffer. distance: metres along the
    // route3d::Route; speedKmh: the rider's speed (lean in bends); crank: the
    // pedal angle (radians, from the real cadence); timeSec: animation clock
    // (clouds, wind in the trees).
    void render(const QSize &pixelSize, double distance, double speedKmh, double crank, float timeSec);

    // Releases the GL objects (context current).
    void release();

    // The trees of the world as geometry (no GL: the Rowing page's 3D view
    // plants the same trees): position, normal, colour, sway (0 at the foot ..
    // 1 at the top: how much the wind moves it).
    struct MeshVertex { QVector3D position, normal, color; float sway; };
    void treeMesh(const route3d::TreeSpec &t, bool far, std::vector<MeshVertex> &out);

private:
    enum Material { Solid = 0, Asphalt = 1, Grass = 2, Gravel = 3, Paint = 4, Verge = 5, Shadow = 6, Distant = 7, Foliage = 8,
                    Banner = 9,     // Banner: colour r,g = texture coordinates in the START / FINISH banner
                    Water = 10,     // lakes and the sea; blend = water depth (m). Grass: blend = sand (0..1)
                    Polka = 11,     // white with red dots: colour r,g = surface position in dot periods
                    Rotor = 12,     // wind turbine blades, turned in the vertex shader: colour = hub,
                                    // normal = rotor axis, blend = phase (one face, lit from both sides)
                    BirdBody = 13 }; // birds: less haze (they fly beyond the landscape); normals point up
    struct Vertex { float x, y, z, r, g, b, nx, ny, nz, material, blend; };

    static QVector3D v(const route3d::Point &p) { return QVector3D(float(p.x), float(p.y), float(p.z)); }
    static float rnd(uint32_t seed, int k);
    void put(std::vector<Vertex> &out, QVector3D p, QVector3D n, QVector3D c, int m, float b);
    // (the short form writes to m_target: the tile, or the gate buffer while buildGates() runs)
    void put(QVector3D p, QVector3D n, QVector3D c, int m, float b) { put(*m_target, p, n, c, m, b); }
    void tri(QVector3D a, QVector3D b, QVector3D c, QVector3D color, int material = Solid, float ba = 0, float bb = 0, float bc = 0);
    void quad(QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D color, int material = Solid, float ba = 0,
              float bb = 0, float bc = 0, float bd = 0);
    QVector3D side(double d) const;
    QVector3D groundTint(double x, double z) const;
    void trunk(QVector3D base, float height, float r0, float r1, int sides, QVector3D color);
    void spruce(const route3d::TreeSpec &t, bool far);
    void blob(QVector3D center, QVector3D crownCenter, float r, QVector3D green, float squash, uint32_t seed, int rings,
              int sides, float baseY, float height);
    void broadleaf(const route3d::TreeSpec &t, bool far);
    void bush(const route3d::TreeSpec &t);
    // the warm biomes' trees (biome.h)
    void palm(const route3d::TreeSpec &t, bool far);
    void cactus(const route3d::TreeSpec &t, bool far);
    void eucalyptus(const route3d::TreeSpec &t, bool far);
    void acacia(const route3d::TreeSpec &t, bool far);
    void tree(const route3d::TreeSpec &t, bool far);   // any kind
    static QVector3D rgb(const route3d::Rgb &c) { return QVector3D(c.r, c.g, c.b); }
    // The birds around the rider at drawn distance d (eye: the camera) at
    // this time (Route3DBirds.cpp): built every frame (up to a few thousand
    // vertices), drawn after the landscape.
    void birds(double distance, const QVector3D &eye, float time, std::vector<Vertex> &out);
    // Two trees near the road a little bird flies between (Route3DBirds.cpp):
    // a, b inside their crowns (where it rests, hidden), the top of its path.
    struct Perch { QVector3D a, b; float peak; uint32_t seed; };
    static void perches(const std::vector<route3d::TreeSpec> &trees, const route3d::Terrain &terrain, double cx, double cz,
                        const route3d::BiomeProfile &profile, std::vector<Perch> &out);
    void applyProfile();   // m_profile -> sun, terrain settings, ring (setRoute)
    void setBiomeUniforms(QOpenGLShaderProgram &program, bool sky);
    void flowers(const route3d::FlowerClump &f);
    void rock(const route3d::RockSpec &s, bool far);
    void face(QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n, QVector3D col);
    // countryside (Route3DCountryside.cpp)
    // a box: centre, half axes (orthogonal); blend lo at -hy, hi at +hy (Foliage sway)
    void box(QVector3D c, QVector3D hx, QVector3D hy, QVector3D hz, QVector3D col, int material = Solid, float blendLo = 0,
             float blendHi = 0);
    // a square bar from a to b (half thickness)
    void limb(QVector3D a, QVector3D b, float half, QVector3D col, int material = Solid, float blendA = 0, float blendB = 0);
    void cylinder(QVector3D a, QVector3D b, float r, int sides, QVector3D side, QVector3D cap);
    // a low-poly ellipsoid (smooth normals): centre, half axes; colour per unit direction
    void ellipsoid(QVector3D c, QVector3D ax, QVector3D ay, QVector3D az, int rings, int sides,
                   const std::function<QVector3D(QVector3D)> &color);
    // an open tapered tube from a (radius r0, colour c0) to b (r1, c1), smooth normals
    void taper(QVector3D a, QVector3D b, float r0, float r1, int sides, QVector3D c0, QVector3D c1);
    void animal(const route3d::AnimalSpec &a, bool far, bool legless);
    void bale(const route3d::BaleSpec &b);
    void tractor(const route3d::TractorSpec &t, bool far);
    void turbine(const route3d::TurbineSpec &t, bool far);
    void bench(const route3d::BenchSpec &b);
    // one fence panel from post a to post b (ground points); out: away from the road
    // type 0 wooden rails, 1 electric tape, 2 wire; gate: a steel field gate instead
    void fenceSegment(QVector3D a, QVector3D b, QVector3D out, int type, bool gate);
    void reeds(const route3d::ReedClump &r);
    void jetty(const route3d::JettySpec &j);
    void boat(QVector3D c, float heading, int color, uint32_t seed);
    void building(const route3d::Building &b, bool far);
    void shadowDisc(std::vector<Vertex> &out, QVector3D c, QVector3D fwd, float rx, float rz, float strength,
                    const std::function<float(QVector3D)> &height);
    void buildRing();
    // one inflatable arch across the road at drawn distance d (finish: red)
    enum ArchStyle { StartArch, FinishArch, KomStartArch, KomFinishArch };
    void arch(std::vector<Vertex> &out, double d, ArchStyle style);
    void buildGates();   // arches + display bike -> m_gateBuffer (no tile job running)
    void drawGates(double distance);
    double startGate(double length) const;   // drawn metres of the START arch
    // The display bike on its post beside the road before the START arch
    // (trail-bike.glb, route3d::StaticModel): placed in setRoute() /
    // setStartDistance(), drawn with the arches; tiles keep its spot clear.
    struct DisplaySpot {
        bool valid = false;
        QVector3D pos;            // the foot of the post, on the verge
        QVector3D x, z;           // the model's x / z axes in the world (y is up)
        QVector3D roadSide;       // unit vector from the road towards the spot
        float roadY = 0, outerY = 0, lateral = 0;   // the verge: road edge / outer edge heights
        float groundAt(QVector3D q) const;          // the verge's height under q
    };
    void placeDisplayBike();
    void displayBike(std::vector<Vertex> &out);
    void sprintMarks(std::vector<Vertex> &out, const SprintMarks &sprint);
    void kmPosts();                                   // -> m_target (buildGates)
    void lapBoards();                                 // -> m_target (buildGates)
    void finishTunnel(double finishArch);              // barriers + flags past the FINISH arch (-> m_target)
    float vergeHeight(double d, float lateral) const;  // the ground beside the road (gravel, verge, terrain)
    // The landscape around the camera: rebuilt when the camera enters another
    // 32 m tile. The first tile of a route is built in the frame; later ones
    // on a worker thread (the old tile is drawn meanwhile) and only uploaded
    // in a frame, so riding never stalls the window.
    void scene(double distance);
    void buildTile(int tileX, int tileZ);   // -> m_build / m_buildDecals (any thread)
    void uploadTile();                       // m_build -> GPU (render thread)
    void waitForTile();
    void indexRoute();
    void attributes();
    QByteArray glslHeader() const;

    QOpenGLContext *m_context = nullptr;
    QOpenGLShaderProgram m_shader, m_skyShader;
    QOpenGLShaderProgram m_cutoutShader;   // m_shader with cut-out letters (discard): the gate buffer only
    std::array<std::unique_ptr<QOpenGLTexture>, 4> m_textures;
    std::unique_ptr<QOpenGLTexture> m_bannerTexture;   // START (top half) / FINISH (bottom half)
    QOpenGLBuffer m_gateBuffer { QOpenGLBuffer::VertexBuffer };
    std::vector<Vertex> m_gateVertices;
    int m_startGateCount = 0;     // first vertices: the START arch, then the FINISH arch
    int m_finishGateEnd = 0;      // then the display bike (always drawn) up to the end
    DisplaySpot m_bikeSpot;       // read by the tile job: changed only after waitForTile()
    std::vector<SprintMarks> m_sprints;   // drawn with the arches (m_gateBuffer)
    std::vector<KomMarks> m_koms;
    std::vector<std::array<double, 2>> m_kmPosts;   // drawn metres, km number
    int m_laps = -1;                         // laps on the lap board (-1: none)
    std::vector<route3d::BenchSpec> m_benches;   // per route (setRoute), read by the tile job
    std::vector<Vertex> *m_target = &m_build;
    double m_startDistance = 0;
    double m_builtStartGate = 0;  // drawn metres of the START arch in m_gateBuffer
    bool m_startDirty = false;    // m_startDistance changed: bike / gates follow in scene()
    bool m_gatesDirty = true;
    bool m_biomeUniformsDirty = true;   // m_profile -> the programs' uniforms (render)
    QOpenGLBuffer m_buffer { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_ghostBuffer { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_birdBuffer { QOpenGLBuffer::VertexBuffer };
    std::vector<Vertex> m_birdVertices;
    // the little birds' trees: written by the tile job, taken with its tile (uploadTile)
    std::vector<Perch> m_buildPerches, m_perches;
    // the ground under the flying birds: a coarse grid of the highest terrain
    // around each point (the tile's nodes; same hand-over as the perches)
    struct BirdGround {
        float x0 = 0, z0 = 0, step = 32;   // grid origin (in steps) and spacing (m)
        int n = 0;
        std::vector<float> h;              // n x n, [i * n + j]: i along x
        float at(float x, float z) const;  // bilinear; -1e9 outside
    };
    BirdGround m_buildBirdGround, m_birdGround;
    QOpenGLBuffer m_ringBuffer { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_ghostIndexBuffer { QOpenGLBuffer::IndexBuffer };
    std::vector<uint32_t> m_ghostIndices;
    size_t m_ghostIndexShadow = size_t(-1);
    const uint32_t *m_ghostIndexSource = nullptr;
    route3d::GhostRider m_ghost;
    route3d::GhostRunner m_runnerFigure;
    bool m_runner = false;
    bool m_runnerModelTried = false;
    bool m_riderModelTried = false;    // the .glb cyclist (riderModel()) was looked for   // the .glb runner (runnerModel()) was looked for
    QOpenGLVertexArrayObject m_vao;
    QOpenGLVertexArrayObject m_skyVao;
    std::vector<Vertex> m_build, m_buildDecals, m_ring;   // m_build*: written by the tile job only
    int m_meshCount = 0, m_decalCount = 0;                // on the GPU
    int m_bufferCapacity = 0;                             // bytes allocated in m_buffer
    std::future<void> m_tileJob;
    int m_jobX = INT_MAX, m_jobZ = INT_MAX;
    // route points per 256 m cell (the road near a tile without scanning the route)
    std::unordered_map<long long, std::vector<uint32_t>> m_routeCells;
    std::shared_ptr<const route3d::Route> m_route;
    route3d::Terrain m_terrain;
    int m_meshX = INT_MAX, m_meshZ = INT_MAX;
    bool m_ready = false;
    QString m_error;
    // The route's landscape (biome.h): read by the tile job, changed only in
    // setRoute() after waitForTile().
    route3d::BiomeProfile m_profile;
    bool m_ringDirty = false;   // the distant hills follow the biome (uploaded in render())
    // Nordic: a low afternoon sun from the south-west, long, readable shading
    // on hills (per biome: m_profile.sun).
    QVector3D m_sun = QVector3D(-.55f, .50f, .67f).normalized();
};

} // namespace watt
