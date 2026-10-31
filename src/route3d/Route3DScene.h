#pragma once

#include "ghost_rider.h"
#include "ghost_runner.h"
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

private:
    enum Material { Solid = 0, Asphalt = 1, Grass = 2, Gravel = 3, Paint = 4, Verge = 5, Shadow = 6, Distant = 7, Foliage = 8,
                    Banner = 9 };   // Banner: colour r,g = texture coordinates in the START / FINISH banner
    struct Vertex { float x, y, z, r, g, b, nx, ny, nz, material, blend; };

    static QVector3D v(const route3d::Point &p) { return QVector3D(float(p.x), float(p.y), float(p.z)); }
    static float rnd(uint32_t seed, int k);
    void put(std::vector<Vertex> &out, QVector3D p, QVector3D n, QVector3D c, int m, float b);
    void put(QVector3D p, QVector3D n, QVector3D c, int m, float b) { put(m_build, p, n, c, m, b); }
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
    void face(QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n, QVector3D col);
    void building(const route3d::Building &b, bool far);
    void shadowDisc(std::vector<Vertex> &out, QVector3D c, QVector3D fwd, float rx, float rz, float strength,
                    const std::function<float(QVector3D)> &height);
    void buildRing();
    // one inflatable arch across the road at drawn distance d (finish: red)
    void arch(std::vector<Vertex> &out, double d, bool finish);
    void drawGates(double distance);
    double startGate(double length) const;   // drawn metres of the START arch
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
    std::array<std::unique_ptr<QOpenGLTexture>, 4> m_textures;
    std::unique_ptr<QOpenGLTexture> m_bannerTexture;   // START (top half) / FINISH (bottom half)
    QOpenGLBuffer m_gateBuffer { QOpenGLBuffer::VertexBuffer };
    std::vector<Vertex> m_gateVertices;
    int m_startGateCount = 0;     // first vertices: the START arch, then the FINISH arch
    double m_startDistance = 0;
    bool m_gatesDirty = true;
    QOpenGLBuffer m_buffer { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_ghostBuffer { QOpenGLBuffer::VertexBuffer };
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
    // Low afternoon sun from the south-west: long, readable shading on hills.
    const QVector3D m_sun = QVector3D(-.55f, .50f, .67f).normalized();
};

} // namespace watt
