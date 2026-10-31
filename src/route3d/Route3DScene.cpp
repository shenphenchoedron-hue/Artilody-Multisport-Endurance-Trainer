#include "Route3DScene.h"

#include <QFile>
#include <map>

#include "materials.h"
#include "settlement.h"

#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>

using namespace route3d;

namespace watt {

namespace {

// Shared GLSL: sky colour and the matching horizon haze, so fogged terrain
// melts into the sky instead of into a flat colour.
const char *const kSkyCommon = R"(
uniform vec3 sunDir;
const vec3 zenith=vec3(.27,.47,.77);
const vec3 horizon=vec3(.73,.81,.88);
vec3 skyColor(vec3 d){
    float h=max(d.y,0.);
    vec3 c=mix(horizon,zenith,pow(h,.5));
    float s=max(dot(d,sunDir),0.);
    c+=vec3(1.,.86,.64)*(pow(s,6.)*.20+pow(s,48.)*.25);
    c+=vec3(1.,.97,.90)*smoothstep(.9994,.9998,s)*1.5;
    return c;
}
vec3 hazeColor(vec3 d){d.y=0.;return skyColor(normalize(d+vec3(0.,1e-4,0.)));}
)";

// (attribute locations are bound by name: GLSL 1.40 has no layout qualifiers)
const char *const kVertexShader = R"(
in vec3 position;
in vec3 color;
in vec3 normal;
in float material;
in float blend;
uniform mat4 mvp;uniform vec3 offset;uniform float time;
out vec3 col;out vec3 world;out vec3 norm;flat out int mat;out float edge;
void main(){
    vec3 p=position+offset;mat=int(material+.5);
    if(mat==8){ // gentle wind sway, stronger towards the crown top
        float w=blend*blend;
        p.x+=sin(time*1.3+p.x*.07+p.z*.05)*.12*w;
        p.z+=cos(time*1.05+p.x*.05-p.z*.04)*.08*w;
    }
    gl_Position=mvp*vec4(p,1);col=color;world=p;norm=normal;edge=blend;
}
)";

// Textures are sampled in world XZ relative to uvOrigin (a multiple of 5 km,
// where every texture scale below repeats): long routes keep sharp surfaces
// far from the start instead of losing float precision.
const char *const kFragmentShaderHead = R"(
in vec3 col;in vec3 world;in vec3 norm;flat in int mat;in float edge;
uniform vec3 eye;uniform vec2 uvOrigin;
uniform sampler2D asphaltMap;uniform sampler2D grassMap;uniform sampler2D gravelMap;uniform sampler2D noiseMap;
uniform sampler2D bannerMap;
uniform float opacity;
out vec4 frag;
)";

const char *const kFragmentShaderBody = R"(
vec3 grass(vec2 p){
    vec3 a=texture(grassMap,p*.42).rgb;
    vec3 b=texture(grassMap,mat2(.8,-.6,.6,.8)*p*.13+.37).rgb; // rotated second scale hides tiling
    vec3 m=texture(noiseMap,p*.006).rgb;                          // 40 m and 10 m patches
    vec3 g=mix(a,b,.45)*(.80+m.g*.40);
    return mix(g,g*vec3(1.12,1.05,.74),smoothstep(.55,.80,m.r)*.5);
}
void main(){
    vec3 view=world-eye;float dist=length(view);
    float fog=max(1.-exp(-dist*dist/(290.*290.)),smoothstep(335.,395.,dist));
    if(mat==6){frag=vec4(.03,.05,.07,smoothstep(0.,.38,edge)*.52*(1.-fog));return;}
    vec2 uv=world.xz-uvOrigin;
    vec3 haze=hazeColor(view);
    vec3 base=col;
    if(mat==1){
        base=texture(asphaltMap,uv*.8).rgb*1.06;
        float lat=abs(edge);
        float tracks=smoothstep(.10,.24,lat)*(1.-smoothstep(.26,.40,lat))+smoothstep(.60,.74,lat)*(1.-smoothstep(.76,.90,lat));
        base*=1.-.09*tracks;
        base*=1.+.12*smoothstep(.88,1.,lat); // dusty edges
        base*=.90+.20*texture(noiseMap,uv*.018).b; // repair patches
    }
    if(mat==2)base=grass(uv)*col;
    if(mat==3)base=texture(gravelMap,uv*.75).rgb;
    if(mat==4){float grain=texture(asphaltMap,uv*.8).r;base=col*(.9+grain*.35);}
    if(mat==9)base=texture(bannerMap,col.rg).rgb;
    if(mat==5){
        vec3 g=grass(uv);
        vec3 stone=texture(gravelMap,uv*.75).rgb;
        float variation=texture(grassMap,uv*1.5).g-.46;
        float mixValue=smoothstep(.12,.95,clamp(edge+variation*.32*sin(edge*3.14159),0.,1.));
        base=mix(stone,g,mixValue);
    }
    vec3 n=normalize(norm);
    float ndl=dot(n,sunDir);
    // Foliage uses wrapped diffuse: light scatters through leaves and needles.
    float diffuse=mat==8?clamp((ndl+.45)/1.45,0.,1.):max(ndl,0.);
    vec3 ambient=mix(vec3(.30,.29,.23),vec3(.52,.57,.63),n.y*.5+.5);
    vec3 lit=base*(ambient*.95+vec3(1.,.94,.82)*diffuse*.92);
    if(mat==1||mat==4){
        float spec=pow(max(dot(reflect(-sunDir,n),-view/dist),0.),20.);
        lit+=vec3(1.,.95,.85)*spec*.06;
    }
    if(mat==7){frag=vec4(mix(haze,lit,edge),1.);return;}
    frag=vec4(mix(lit,haze,fog),opacity);
}
)";

const char *const kSkyVertexShader = R"(
uniform mat4 invViewProj;uniform vec3 eye;
out vec3 ray;
void main(){
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2))*2.-1.;
    gl_Position=vec4(p,.9999,1.);
    vec4 w=invViewProj*vec4(p,1.,1.);ray=w.xyz/w.w-eye;
}
)";

const char *const kSkyFragmentShaderHead = R"(
in vec3 ray;uniform sampler2D noiseMap;uniform float time;
out vec4 frag;
)";

const char *const kSkyFragmentShaderBody = R"(
void main(){
    vec3 d=normalize(ray);vec3 c=skyColor(d);
    if(d.y>0.){
        vec2 uv=d.xz/(d.y+.10)*.10+vec2(time*.0020,time*.0007);
        vec4 a=texture(noiseMap,uv),b=texture(noiseMap,uv*2.7+.31);
        float density=a.r*.62+b.g*.38;
        float cover=smoothstep(.50,.76,density)*smoothstep(.0,.22,d.y);
        vec3 cloud=mix(vec3(.74,.78,.84),vec3(1.,.985,.955),clamp(.35+b.b*.9-cover*.3,0.,1.));
        c=mix(c,cloud,cover*.9);
    }
    frag=vec4(c,1.);
}
)";

constexpr double kTextureRepeatM = 5000.0;   // every texture scale above repeats here

// START / FINISH arches (inflatable, like at a race)
const QVector3D kStartGreen(.10f, .62f, .24f);
const QVector3D kFinishRed(.80f, .12f, .10f);
constexpr float kArchHalfSpan = 4.4f;   // tube centre beside the road (gravel ends at 3.6 m)
constexpr float kArchLegHeight = 3.0f;  // straight legs, then a half circle
constexpr float kArchTube = .48f;       // inflated tube radius
constexpr float kBannerHalfWidth = 2.6f, kBannerHeight = 1.3f;   // 4:1 like the texture halves

// White bold "START" on green (top half) and "FINISH" on red (bottom half).
QImage bannerImage()
{
    QImage img(1024, 512, QImage::Format_RGBA8888);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    auto half = [&](int y, QVector3D c, const QString &text) {
        const QRect r(0, y, 1024, 256);
        p.fillRect(r, QColor::fromRgbF(c.x(), c.y(), c.z()));
        p.setPen(QPen(QColor(255, 255, 255, 230), 10));
        p.drawRect(r.adjusted(10, 10, -10, -10));
        QFont f = QGuiApplication::font();
        f.setPixelSize(170);
        f.setWeight(QFont::Black);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 14);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, text);
    };
    half(0, kStartGreen * 1.1f, QStringLiteral("START"));
    half(256, kFinishRed * 1.1f, QStringLiteral("FINISH"));
    p.end();
    return img;
}

} // namespace

Route3DScene::Route3DScene() = default;

Route3DScene::~Route3DScene()
{
    waitForTile();
}

void Route3DScene::waitForTile()
{
    if (!m_tileJob.valid())
        return;
    try {
        m_tileJob.get();
    } catch (...) {
    }
    m_jobX = m_jobZ = INT_MAX;
}

namespace {
constexpr double kRouteCellM = 256.0;
long long routeCellKey(int cx, int cz)
{
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned int>(cz);
}
} // namespace

void Route3DScene::indexRoute()
{
    m_routeCells.clear();
    if (!m_route)
        return;
    const auto &pts = m_route->points;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const int cx = int(std::floor(pts[i].x / kRouteCellM)), cz = int(std::floor(pts[i].z / kRouteCellM));
        m_routeCells[routeCellKey(cx, cz)].push_back(uint32_t(i));
    }
}

float Route3DScene::rnd(uint32_t seed, int k)
{
    return float(WorldNoise::random(int(seed & 0x7fffffff), k, 91));
}

QByteArray Route3DScene::glslHeader() const
{
    if (m_context->isOpenGLES())
        return QByteArrayLiteral("#version 300 es\nprecision highp float;\nprecision highp int;\n");
    const QSurfaceFormat f = m_context->format();
    if (f.majorVersion() > 3 || (f.majorVersion() == 3 && f.minorVersion() >= 3))
        return QByteArrayLiteral("#version 330 core\n");
    return QByteArrayLiteral("#version 140\n");
}

bool Route3DScene::initialize(QOpenGLContext *context)
{
    m_context = context;
    m_ready = false;
    if (!context) {
        m_error = QStringLiteral("No OpenGL context.");
        return false;
    }
    initializeOpenGLFunctions();
    const QSurfaceFormat f = context->format();
    const bool es = context->isOpenGLES();
    const bool versionOk = es ? f.majorVersion() >= 3
                              : (f.majorVersion() > 3 || (f.majorVersion() == 3 && f.minorVersion() >= 1));
    if (!versionOk) {
        m_error = QStringLiteral("The 3D view needs OpenGL 3.1 or OpenGL ES 3.0 (this computer offers %1 %2.%3).")
                      .arg(es ? QStringLiteral("OpenGL ES") : QStringLiteral("OpenGL"))
                      .arg(f.majorVersion())
                      .arg(f.minorVersion());
        return false;
    }
    const QByteArray head = glslHeader();
    auto bindAttributes = [](QOpenGLShaderProgram &p) {
        p.bindAttributeLocation("position", 0);
        p.bindAttributeLocation("color", 1);
        p.bindAttributeLocation("normal", 2);
        p.bindAttributeLocation("material", 3);
        p.bindAttributeLocation("blend", 4);
    };
    const QByteArray fs = head + kFragmentShaderHead + kSkyCommon + kFragmentShaderBody;
    if (!m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex, head + kVertexShader)
        || !m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)) {
        m_error = QStringLiteral("3D shaders could not be compiled: ") + m_shader.log();
        return false;
    }
    bindAttributes(m_shader);
    if (!m_shader.link()) {
        m_error = QStringLiteral("3D shaders could not be linked: ") + m_shader.log();
        return false;
    }
    const QByteArray skyFs = head + kSkyFragmentShaderHead + kSkyCommon + kSkyFragmentShaderBody;
    if (!m_skyShader.addShaderFromSourceCode(QOpenGLShader::Vertex, head + kSkyVertexShader)
        || !m_skyShader.addShaderFromSourceCode(QOpenGLShader::Fragment, skyFs) || !m_skyShader.link()) {
        m_error = QStringLiteral("Sky shaders could not be built: ") + m_skyShader.log();
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        auto &t = m_textures[size_t(i)];
        t = std::make_unique<QOpenGLTexture>(Materials::create(i), QOpenGLTexture::GenerateMipMaps);
        t->setWrapMode(QOpenGLTexture::Repeat);
        t->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        t->setMagnificationFilter(QOpenGLTexture::Linear);
        if (i < 3 && context->hasExtension(QByteArrayLiteral("GL_EXT_texture_filter_anisotropic"))) {
            GLfloat limit = 1;
            glGetFloatv(0x84FF, &limit);   // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
            t->setMaximumAnisotropy(std::min(8.f, limit));
        }
        if (!t->isCreated()) {
            m_error = QStringLiteral("The surface textures could not be created.");
            return false;
        }
    }
    m_bannerTexture = std::make_unique<QOpenGLTexture>(bannerImage(), QOpenGLTexture::GenerateMipMaps);
    m_bannerTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_bannerTexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    m_bannerTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_gateBuffer.create();
    m_gateBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_gatesDirty = true;
    m_vao.create();
    m_skyVao.create();
    m_buffer.create();
    m_buffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_ghostBuffer.create();
    m_ghostBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_ghostIndexBuffer.create();
    m_ghostIndexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    buildRing();
    m_ringBuffer.create();
    m_ringBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_ringBuffer.bind();
    m_ringBuffer.allocate(m_ring.data(), int(m_ring.size() * sizeof(Vertex)));
    m_ringBuffer.release();
    m_error.clear();
    m_ready = true;
    return true;
}

void Route3DScene::release()
{
    waitForTile();
    for (auto &t : m_textures)
        t.reset();
    m_bannerTexture.reset();
    m_gateBuffer.destroy();
    m_buffer.destroy();
    m_ghostBuffer.destroy();
    m_ghostIndexBuffer.destroy();
    m_ringBuffer.destroy();
    m_vao.destroy();
    m_skyVao.destroy();
    m_shader.removeAllShaders();
    m_skyShader.removeAllShaders();
    m_ready = false;
}

void Route3DScene::setRoute(std::shared_ptr<const Route> route)
{
    waitForTile();   // the job reads the route and the terrain
    m_route = std::move(route);
    if (m_route)
        m_terrain.reset(*m_route);   // keeps a pointer: m_route owns the route
    else
        m_terrain = Terrain();
    indexRoute();
    m_meshX = m_meshZ = INT_MAX;
    m_build.clear();
    m_buildDecals.clear();
    m_meshCount = m_decalCount = 0;
    m_gatesDirty = true;
    m_ghostIndices.clear();
    m_ghostIndexShadow = size_t(-1);
    m_ghostIndexSource = nullptr;
}

namespace {
std::shared_ptr<const route3d::GlbModel> loadModel(const char *env, const QString &resource, const char *what,
                                                   route3d::GlbModel::Outfit outfit = route3d::GlbModel::Outfit::Runner)
{
    const QString override = qEnvironmentVariable(env);
    // <env>=procedural: the built-in figure
    if (override == QLatin1String("procedural"))
        return {};
    const QString path = override.isEmpty() ? resource : override;
    if (!QFile::exists(path))
        return {};
    auto m = std::make_shared<route3d::GlbModel>();
    QString error;
    if (!m->loadFile(path, &error, outfit)) {
        qWarning("Route3D: %s model %s not used: %s", what, qPrintable(path), qPrintable(error));
        return {};
    }
    return m;
}
} // namespace

// The runner figure: the skinned, animated runner.glb (Qt resource
// :/models/runner.glb, or WATT_RUNNER_GLB=<file>) when it loads, else the
// procedural runner. Loaded once per process.
std::shared_ptr<const route3d::GlbModel> runnerModel()
{
    static const std::shared_ptr<const route3d::GlbModel> model =
        loadModel("WATT_RUNNER_GLB", QStringLiteral(":/models/runner.glb"), "runner");
    return model;
}

// The same figure walking (walker.glb: :/models/walker.glb, or
// WATT_WALKER_GLB=<file>); optional, used at walking speeds.
std::shared_ptr<const route3d::GlbModel> walkerModel()
{
    static const std::shared_ptr<const route3d::GlbModel> model =
        loadModel("WATT_WALKER_GLB", QStringLiteral(":/models/walker.glb"), "walker");
    return model;
}

// The cyclist: the skinned rider.glb (:/models/rider.glb, or
// WATT_RIDER_GLB=<file>, =procedural for the built-in rider) posed on the bike
// (GlbModel::poseOnBike); else the procedural rider. Loaded once per process.
std::shared_ptr<const route3d::GlbModel> riderModel()
{
    static const std::shared_ptr<const route3d::GlbModel> model = [] {
        auto m = loadModel("WATT_RIDER_GLB", QStringLiteral(":/models/rider.glb"), "rider",
                           route3d::GlbModel::Outfit::Cyclist);
        if (m && !m->canRide()) {
            qWarning("Route3D: rider.glb lacks the joints needed on the bike (Mixamo skeleton) - not used");
            m.reset();
        }
        return m;
    }();
    return model;
}

void Route3DScene::setRunner(bool on)
{
    m_runner = on;
    if (on && !m_runnerModelTried) {
        m_runnerModelTried = true;
        m_runnerFigure.setModel(runnerModel());
        if (auto walk = walkerModel(); walk && !m_runnerFigure.setWalkModel(walk))
            qWarning("Route3D: walker.glb is not the same figure (mesh) as runner.glb - not used");
    }
}

void Route3DScene::setStartDistance(double drawnMetres)
{
    if (drawnMetres == m_startDistance)
        return;
    m_startDistance = drawnMetres;
    m_gatesDirty = true;
}

// An inflatable arch: a fat tube up one side, over the road in a half circle
// and down the other, in light/dark segments like the sewn panels of a real
// one, with a printed banner on both faces (readable from either side).
void Route3DScene::arch(std::vector<Vertex> &out, double d, bool finish)
{
    const Route &route = *m_route;
    d = std::clamp(d, 0.0, route.length());
    QVector3D f = v(route.at(std::min(route.length(), d + 3))) - v(route.at(std::max(0.0, d - 3)));
    f.setY(0);
    if (f.lengthSquared() < 1e-6f)
        f = QVector3D(0, 0, -1);
    f.normalize();
    const QVector3D s(-f.z(), 0, f.x()), up(0, 1, 0);   // s: the rider's right
    const QVector3D c = v(route.at(d));
    const float road = c.y();
    const QVector3D color = finish ? kFinishRed : kStartGreen;
    // centre line in the arch plane: (lateral, height); legs reach into the ground
    auto groundAt = [&](float lateral) {
        const QVector3D q = c + s * lateral;
        return float(m_terrain.surface(q.x(), q.z())) - .3f;
    };
    std::vector<QVector2D> path;
    const float top = road + kArchLegHeight;
    for (int i = 0; i <= 4; ++i)
        path.emplace_back(-kArchHalfSpan, groundAt(-kArchHalfSpan) + (top - groundAt(-kArchHalfSpan)) * i / 4.f);
    for (int i = 1; i < 24; ++i) {
        const float a = float(3.14159265 * i / 24);
        path.emplace_back(-kArchHalfSpan * std::cos(a), top + kArchHalfSpan * std::sin(a));
    }
    for (int i = 0; i <= 4; ++i)
        path.emplace_back(kArchHalfSpan, top + (groundAt(kArchHalfSpan) - top) * i / 4.f);
    auto at = [&](const QVector2D &p) { return QVector3D(c.x(), 0, c.z()) + s * p.x() + up * p.y(); };
    constexpr int sides = 14;
    float along = 0;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const QVector3D a = at(path[i]), b = at(path[i + 1]);
        const QVector3D t = (b - a).normalized();
        // ring frame: f (along the road) and the in-plane normal
        const QVector3D n = QVector3D::crossProduct(f, t).normalized();
        along += (b - a).length();
        const bool seam = int(along / 1.3f) % 2 == 0;
        const QVector3D col = color * (seam ? 1.0f : .86f);
        // slight bulge between seams: inflated panels
        for (int k = 0; k < sides; ++k) {
            const float a0 = float(6.2831853 * k / sides), a1 = float(6.2831853 * (k + 1) / sides);
            const QVector3D r0 = n * std::cos(a0) + f * std::sin(a0), r1 = n * std::cos(a1) + f * std::sin(a1);
            const QVector3D p00 = a + r0 * kArchTube, p01 = a + r1 * kArchTube, p10 = b + r0 * kArchTube,
                            p11 = b + r1 * kArchTube;
            put(out, p00, r0, col, Solid, 0);
            put(out, p10, r0, col, Solid, 0);
            put(out, p11, r1, col, Solid, 0);
            put(out, p00, r0, col, Solid, 0);
            put(out, p11, r1, col, Solid, 0);
            put(out, p01, r1, col, Solid, 0);
        }
    }
    // banner on the front (towards the approaching rider) and on the back
    const float v0 = finish ? .5f : 0.f, v1 = v0 + .5f;
    const float by1 = top + kArchHalfSpan - .25f, by0 = by1 - kBannerHeight;
    for (float side : { -1.f, 1.f }) {   // -1: front (faces -f)
        const QVector3D off = f * (side * (kArchTube + .03f));
        const QVector3D n = f * side;
        auto P = [&](float lateral, float y) { return QVector3D(c.x(), y, c.z()) + s * lateral + off; };
        // front: left edge (-s) = u 0; back: seen from behind, left edge is +s
        const float uL = side < 0 ? 0.f : 1.f, uR = 1.f - uL;
        const QVector3D tl(uL, v0, 0), tr(uR, v0, 0), bl(uL, v1, 0), br(uR, v1, 0);
        put(out, P(-kBannerHalfWidth, by1), n, tl, Banner, 0);
        put(out, P(kBannerHalfWidth, by1), n, tr, Banner, 0);
        put(out, P(kBannerHalfWidth, by0), n, br, Banner, 0);
        put(out, P(-kBannerHalfWidth, by1), n, tl, Banner, 0);
        put(out, P(kBannerHalfWidth, by0), n, br, Banner, 0);
        put(out, P(-kBannerHalfWidth, by0), n, bl, Banner, 0);
    }
}

double Route3DScene::startGate(double length) const
{
    return std::min(length, m_startDistance + 25.0);
}

void Route3DScene::drawGates(double distance)
{
    if (!m_route)
        return;
    const double length = m_route->length();
    if (m_gatesDirty) {
        m_gateVertices.clear();
        // START ahead of the start point (seen whole before you ride through
        // it), FINISH just before the end (you finish under it)
        arch(m_gateVertices, startGate(length), false);
        m_startGateCount = int(m_gateVertices.size());
        arch(m_gateVertices, std::max(0.0, m_route->finish() - 1.0), true);
        m_gateBuffer.bind();
        m_gateBuffer.allocate(m_gateVertices.data(), int(m_gateVertices.size() * sizeof(Vertex)));
        m_gatesDirty = false;
    }
    // START until you have ridden through it, then FINISH (a loop has both at one place)
    const bool finishHalf = distance > startGate(length) + 2.0;
    m_gateBuffer.bind();
    attributes();
    if (finishHalf)
        glDrawArrays(GL_TRIANGLES, m_startGateCount, int(m_gateVertices.size()) - m_startGateCount);
    else
        glDrawArrays(GL_TRIANGLES, 0, m_startGateCount);
}

// --- geometry (unchanged from the prototype) -----------------------------------------------

void Route3DScene::put(std::vector<Vertex> &out, QVector3D p, QVector3D n, QVector3D c, int m, float b)
{
    out.push_back({ p.x(), p.y(), p.z(), c.x(), c.y(), c.z(), n.x(), n.y(), n.z(), float(m), b });
}

void Route3DScene::tri(QVector3D a, QVector3D b, QVector3D c, QVector3D color, int material, float ba, float bb, float bc)
{
    auto normal = QVector3D::crossProduct(b - a, c - a).normalized();
    if (normal.y() < 0)
        normal = -normal;
    put(a, normal, color, material, ba);
    put(b, normal, color, material, bb);
    put(c, normal, color, material, bc);
}

void Route3DScene::quad(QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D color, int material, float ba,
                        float bb, float bc, float bd)
{
    tri(a, b, c, color, material, ba, bb, bc);
    tri(a, c, d, color, material, ba, bc, bd);
}

QVector3D Route3DScene::side(double d) const
{
    auto delta = v(m_route->at(d + 4)) - v(m_route->at(d - 4));
    delta.setY(0);
    delta.normalize();
    return QVector3D(-delta.z(), 0, delta.x());
}

// Large-scale ground colour: dry meadow patches, lush hollows, forest floor.
QVector3D Route3DScene::groundTint(double x, double z) const
{
    float dry = float(WorldNoise::smoothstep(.56, .74, WorldNoise::fbm(x / 150, z / 150, 3, 21))) * .55f;
    float lush = float(WorldNoise::fbm(x / 45, z / 45, 2, 33) - .5) * .28f;
    float forest = float(Vegetation::forestDensity(x, z)) * .7f;
    QVector3D c = QVector3D(1, 1, 1) * (1 - dry) + QVector3D(1.14f, 1.06f, .68f) * dry;
    c *= 1 + lush;
    return c * (1 - forest) + QVector3D(.60f, .55f, .43f) * forest;
}

// Tapered trunk, open at both ends (ends are hidden in ground/crown).
void Route3DScene::trunk(QVector3D base, float height, float r0, float r1, int sides, QVector3D color)
{
    for (int i = 0; i < sides; i++) {
        float a = float(i * 6.283185307 / sides), b = float((i + 1) * 6.283185307 / sides);
        QVector3D na(std::cos(a), 0, std::sin(a)), nb(std::cos(b), 0, std::sin(b)), top(0, height, 0);
        put(base + na * r0, na, color * .75f, Solid, 0);
        put(base + nb * r0, nb, color * .75f, Solid, 0);
        put(base + top + nb * r1, nb, color, Solid, 0);
        put(base + na * r0, na, color * .75f, Solid, 0);
        put(base + top + nb * r1, nb, color, Solid, 0);
        put(base + top + na * r1, na, color, Solid, 0);
    }
}

void Route3DScene::spruce(const TreeSpec &t, bool far)
{
    QVector3D base(float(t.x), float(t.y), float(t.z));
    float h = float(t.height);
    trunk(base, h * .3f, h * .022f, h * .014f, far ? 4 : 6, QVector3D(.30f, .23f, .17f));
    QVector3D green = QVector3D(.11f + .04f * rnd(t.seed, 1), .24f + .06f * rnd(t.seed, 2), .13f + .03f * rnd(t.seed, 3));
    int tiers = far ? 3 : 5, sides = far ? 6 : 10;
    float spin = rnd(t.seed, 4) * 6.28f;
    for (int k = 0; k < tiers; k++) {
        float y0 = h * (.12f + .66f * k / tiers), y1 = k == tiers - 1 ? h : std::min(h, y0 + h * 1.5f / tiers);
        float R = float(t.radius) * (1 - .72f * k / tiers) * (.88f + .24f * rnd(t.seed, 10 + k));
        QVector3D apex = base + QVector3D(0, y1, 0), inner = base + QVector3D(0, y0 + (y1 - y0) * .22f, 0);
        for (int i = 0; i < sides; i++) {
            float a = spin + k * .6f + float(i * 6.283185307 / sides), b = spin + k * .6f + float((i + 1) * 6.283185307 / sides);
            QVector3D ra(std::cos(a), 0, std::sin(a)), rb(std::cos(b), 0, std::sin(b));
            // Cone side normals, softened towards "up" so the crown reads as a soft mass.
            auto coneN = [&](QVector3D r) { return (r * (y1 - y0) + QVector3D(0, R * 1.6f, 0)).normalized(); };
            // Alternating branch tips and gaps give a ragged, star-shaped whorl
            // instead of a smooth cone, at no extra triangle cost. Tips droop.
            float la = i % 2 ? .62f : 1.f, lb = (i + 1) % 2 ? .62f : 1.f;
            QVector3D pa = base + ra * R * la + QVector3D(0, y0 - (la - .62f) * R * .35f, 0),
                      pb = base + rb * R * lb + QVector3D(0, y0 - (lb - .62f) * R * .35f, 0);
            float wa = y0 / h, wb = y1 / h;
            put(pa, coneN(ra), green * (.60f + .18f * la), Foliage, wa);
            put(pb, coneN(rb), green * (.60f + .18f * lb), Foliage, wa);
            put(apex, QVector3D(0, 1, 0), green * 1.18f, Foliage, wb);
            if (!far) {   // dark, slightly concave underside of each whorl
                put(pb, -coneN(rb), green * .42f, Foliage, wa);
                put(pa, -coneN(ra), green * .42f, Foliage, wa);
                put(inner, QVector3D(0, -1, 0), green * .32f, Foliage, wa);
            }
        }
    }
}

// Irregular, noise-displaced crown blob with soft normals and darker underside.
void Route3DScene::blob(QVector3D center, QVector3D crownCenter, float r, QVector3D green, float squash, uint32_t seed,
                        int rings, int sides, float baseY, float height)
{
    auto dir = [&](int i, int s) {
        double a = 3.141592654 * i / rings, t = 6.283185307 * (s % sides) / sides;
        return QVector3D(float(std::sin(a) * std::cos(t)), float(std::cos(a)), float(std::sin(a) * std::sin(t)));
    };
    auto point = [&](int i, int s) {
        auto d = dir(i, s);
        double n = WorldNoise::value(d.x() * 1.9 + d.y() * .7 + seed % 97, d.z() * 1.9 - d.y() * 1.1, seed);
        auto p = d * r * float(.82 + .36 * n);
        p.setY(p.y() * squash);
        return center + p;
    };
    auto vert = [&](int i, int s) {
        auto p = point(i, s);
        auto d = dir(i, s);
        auto n = (d * .55f + (p - crownCenter).normalized() * .45f).normalized();
        float ao = .55f + .55f * (d.y() * .5f + .5f);
        put(p, n, green * ao, Foliage, (p.y() - baseY) / height);
    };
    for (int i = 0; i < rings; i++)
        for (int s = 0; s < sides; s++) {
            if (i > 0) {
                vert(i, s);
                vert(i, s + 1);
                vert(i + 1, s + 1);
            }
            if (i < rings - 1) {
                vert(i, s);
                vert(i + 1, s + 1);
                vert(i + 1, s);
            }
        }
}

void Route3DScene::broadleaf(const TreeSpec &t, bool far)
{
    QVector3D base(float(t.x), float(t.y), float(t.z));
    float h = float(t.height), R = float(t.radius);
    trunk(base, h * .58f, h * .032f, h * .018f, far ? 4 : 6, QVector3D(.33f, .27f, .21f));
    float tone = rnd(t.seed, 5);
    QVector3D green = QVector3D(.20f + .10f * tone, .32f + .08f * tone, .11f + .04f * rnd(t.seed, 6));
    QVector3D crown = base + QVector3D(0, h * .63f, 0);
    if (far) {
        blob(crown, crown, R, green, .85f, t.seed, 4, 6, base.y(), h);
        return;
    }
    blob(crown, crown, R * .82f, green, .85f, t.seed, 5, 8, base.y(), h);
    for (int k = 0; k < 2; k++) {
        float a = rnd(t.seed, 20 + k) * 6.28f;
        QVector3D off(std::cos(a) * R * .45f, h * (.08f - .16f * rnd(t.seed, 30 + k)), std::sin(a) * R * .45f);
        blob(crown + off, crown, R * (.55f + .15f * rnd(t.seed, 40 + k)), green * (.92f + .16f * rnd(t.seed, 50 + k)), .85f,
             t.seed + uint32_t(k) + 1, 5, 8, base.y(), h);
    }
}

void Route3DScene::bush(const TreeSpec &t)
{
    QVector3D base(float(t.x), float(t.y), float(t.z));
    float h = float(t.height);
    QVector3D green = QVector3D(.21f + .08f * rnd(t.seed, 7), .31f + .06f * rnd(t.seed, 8), .12f);
    QVector3D c = base + QVector3D(0, h * .45f, 0);
    blob(c, c, float(t.radius), green, .75f, t.seed, 4, 7, base.y(), h * 2.5f);
}

// Flat quad with an explicit normal (tri() forces normals upwards: wrong for walls).
void Route3DScene::face(QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n, QVector3D col)
{
    put(a, n, col, Solid, 0);
    put(b, n, col, Solid, 0);
    put(c, n, col, Solid, 0);
    put(a, n, col, Solid, 0);
    put(c, n, col, Solid, 0);
    put(d, n, col, Solid, 0);
}

// Danish country building: plinth, rendered/painted walls, gable roof with
// overhang, chimney, framed windows and door. ~60-170 triangles; distant
// buildings drop windows and chimney.
void Route3DScene::building(const Building &b, bool far)
{
    QVector3D c(float(b.x), 0, float(b.z)), u(float(b.ux()), 0, float(b.uz())), f(float(b.fx), 0, float(b.fz)), up(0, 1, 0);
    float L = float(b.length * .5), W = float(b.width * .5), y0 = float(b.ground) - .4f, fl = float(b.floor),
          y1 = fl + float(b.wall), rise = W * float(b.pitch);
    auto P = [&](float a, float s, float y) { return c + u * a + f * s + QVector3D(0, y, 0); };
    static const QVector3D wallColors[] = { { .84f, .83f, .79f }, { .86f, .72f, .42f }, { .58f, .23f, .17f }, { .14f, .13f, .12f } };
    static const QVector3D roofColors[] = { { .52f, .22f, .15f }, { .12f, .12f, .14f }, { .42f, .35f, .24f },
                                            { .46f, .46f, .44f }, { .30f, .32f, .35f } };
    float tint = .92f + .16f * rnd(b.seed, 1);
    QVector3D wall = wallColors[b.walls & 3] * tint, roof = roofColors[std::clamp(b.roof, 0, 4)] * (.9f + .2f * rnd(b.seed, 2)),
              plinth(.40f, .39f, .37f);
    float base = fl + .35f;
    // Walls: long sides (normal +-f) and gable ends (normal +-u).
    for (float s : { -1.f, 1.f }) {
        face(P(-L, s * W, y0), P(L, s * W, y0), P(L, s * W, base), P(-L, s * W, base), f * s, plinth);
        face(P(-L, s * W, base), P(L, s * W, base), P(L, s * W, y1), P(-L, s * W, y1), f * s, wall);
        face(P(s * L, -W, y0), P(s * L, W, y0), P(s * L, W, base), P(s * L, -W, base), u * s, plinth);
        face(P(s * L, -W, base), P(s * L, W, base), P(s * L, W, y1), P(s * L, -W, y1), u * s, wall);
        put(P(s * L, -W, y1), u * s, wall, Solid, 0);   // gable
        put(P(s * L, W, y1), u * s, wall, Solid, 0);
        put(P(s * L, 0, y1 + rise), u * s, wall * .97f, Solid, 0);
    }
    // Roof slab: top, underside, eave fascia and verge edges.
    float oh = b.roof == 2 ? .65f : .45f, og = .35f, k = float(b.pitch), thick = b.roof == 2 ? .45f : .16f;
    float ye = y1 - oh * k, yr = y1 + rise + thick * .3f, A = L + og;
    for (float s : { -1.f, 1.f }) {
        QVector3D n = (up + f * (s * k)).normalized();
        face(P(-A, s * (W + oh), ye), P(A, s * (W + oh), ye), P(A, 0, yr), P(-A, 0, yr), n, roof);
        face(P(-A, s * (W + oh), ye - thick), P(A, s * (W + oh), ye - thick), P(A, 0, yr - thick), P(-A, 0, yr - thick), -n,
             roof * .45f);
        face(P(-A, s * (W + oh), ye - thick), P(A, s * (W + oh), ye - thick), P(A, s * (W + oh), ye), P(-A, s * (W + oh), ye),
             f * s, roof * .7f);
        for (float e : { -1.f, 1.f })
            face(P(e * A, s * (W + oh), ye - thick), P(e * A, 0, yr - thick), P(e * A, 0, yr), P(e * A, s * (W + oh), ye), u * e,
                 roof * .75f);
    }
    if (b.kind == Barn) {   // big doors towards the yard, a row of small stable windows
        face(P(-1.6f, W + .03f, fl), P(1.6f, W + .03f, fl), P(1.6f, W + .03f, fl + 3.f), P(-1.6f, W + .03f, fl + 3.f), f,
             QVector3D(.20f, .17f, .14f));
    }
    if (far)
        return;
    if (b.kind != Barn) {
        float a = (rnd(b.seed, 3) < .5f ? -1.f : 1.f) * L * .35f, cw = .30f;
        QVector3D brick = rnd(b.seed, 4) < .5f ? QVector3D(.78f, .77f, .74f) : QVector3D(.52f, .25f, .18f);
        float cb = yr - .7f, ct = yr + .95f;
        for (float s : { -1.f, 1.f }) {
            face(P(a - cw, s * cw, cb), P(a + cw, s * cw, cb), P(a + cw, s * cw, ct), P(a - cw, s * cw, ct), f * s, brick);
            face(P(a + s * cw, -cw, cb), P(a + s * cw, cw, cb), P(a + s * cw, cw, ct), P(a + s * cw, -cw, ct), u * s, brick);
        }
        face(P(a - cw, -cw, ct), P(a + cw, -cw, ct), P(a + cw, cw, ct), P(a - cw, cw, ct), up, brick * .6f);
    }
    // Framed windows: white frame, dark glass slightly proud of it.
    QVector3D frame(.88f, .88f, .86f), glass(.12f, .15f, .19f);
    auto window = [&](float a, float s, float y, float w, float h, bool gableEnd) {
        QVector3D n = gableEnd ? u * s : f * s, along = gableEnd ? f : u;
        QVector3D p = gableEnd ? P(s * (L + .03f), a, y) : P(a, s * (W + .03f), y);
        auto q = [&](float dx, float dy, float out) { return p + along * dx + QVector3D(0, dy, 0) + n * out; };
        face(q(-w * .5f - .07f, -.07f, 0), q(w * .5f + .07f, -.07f, 0), q(w * .5f + .07f, h + .07f, 0),
             q(-w * .5f - .07f, h + .07f, 0), n, frame);
        face(q(-w * .5f, 0, .02f), q(w * .5f, 0, .02f), q(w * .5f, h, .02f), q(-w * .5f, h, .02f), n, glass);
    };
    if (b.kind == Barn) {
        for (float a = -L + 2.f; a < L - 1.5f; a += 3.f)
            window(a, -1, fl + 2.f, .7f, .5f, false);
        return;
    }
    float ww = b.kind == Longhouse ? .8f : 1.f, wh = b.kind == Longhouse ? .95f : 1.2f;
    int count = std::max(2, int(2 * L / 2.7f));
    int door = count / 2;
    QVector3D doorColor = rnd(b.seed, 5) < .5f ? QVector3D(.22f, .30f, .24f) : QVector3D(.36f, .24f, .16f);
    for (float s : { -1.f, 1.f })
        for (int i = 0; i < count; i++) {
            float a = -L + (i + .5f) * 2 * L / count;
            if (s > 0 && i == door) {   // front door facing the road
                QVector3D n = f;
                auto q = [&](float dx, float dy, float out) {
                    return P(a, W + .03f, fl) + u * dx + QVector3D(0, dy, 0) + n * out;
                };
                face(q(-.6f, 0, 0), q(.6f, 0, 0), q(.6f, 2.2f, 0), q(-.6f, 2.2f, 0), n, frame);
                face(q(-.48f, 0, .02f), q(.48f, 0, .02f), q(.48f, 2.08f, .02f), q(-.48f, 2.08f, .02f), n, doorColor);
                continue;
            }
            window(a, s, fl + .85f, ww, wh, false);
            if (b.wall > 3.2)
                window(a, s, fl + 2.45f, ww * .8f, wh * .75f, false);   // upper floor
        }
    if (rise > 2.f)
        for (float s : { -1.f, 1.f })
            window(0, s, y1 + .4f, .75f, .9f, true);
}

// Soft contact shadow: a fan whose centre is dark and rim transparent.
void Route3DScene::shadowDisc(std::vector<Vertex> &out, QVector3D c, QVector3D fwd, float rx, float rz, float strength,
                              const std::function<float(QVector3D)> &height)
{
    constexpr int segments = 14;
    QVector3D right(-fwd.z(), 0, fwd.x());
    c.setY(height(c));
    for (int i = 0; i < segments; i++) {
        float a = float(i * 6.283185307 / segments), b = float((i + 1) * 6.283185307 / segments);
        auto p = c + right * (std::cos(a) * rx) + fwd * (std::sin(a) * rz), q = c + right * (std::cos(b) * rx) + fwd * (std::sin(b) * rz);
        p.setY(height(p));
        q.setY(height(q));
        put(out, c, { 0, 1, 0 }, { 0, 0, 0 }, Shadow, strength);
        put(out, p, { 0, 1, 0 }, { 0, 0, 0 }, Shadow, 0);
        put(out, q, { 0, 1, 0 }, { 0, 0, 0 }, Shadow, 0);
    }
}

// Two camera-centred rings of distant hills, fading into the horizon haze.
void Route3DScene::buildRing()
{
    m_ring.clear();
    struct Layer { float radius, height, amp, vis; QVector3D color; float phase; };
    for (auto L : { Layer { 1750, 110, 170, .46f, QVector3D(.42f, .52f, .60f), 0.f },
                    Layer { 1150, 55, 90, .66f, QVector3D(.27f, .38f, .24f), 1.7f } }) {
        constexpr int n = 192;
        auto top = [&](int i) {
            double a = 6.283185307 * i / n;
            // Seamless noise around the circle; ridged term gives peaked crests.
            double cx = std::cos(a) * 3.2 + L.phase * 7, cz = std::sin(a) * 3.2 - L.phase * 3;
            double broad = WorldNoise::fbm(cx, cz, 4, uint32_t(51 + L.phase * 10));
            double r = std::clamp((WorldNoise::fbm(cx * 2.6, cz * 2.6, 3, uint32_t(61 + L.phase * 10)) - .5) * 3., -1., 1.);
            double h = std::clamp((broad - .5) * 2.4 + .5, 0., 1.) * .75 + (1 - std::abs(r)) * (1 - std::abs(r)) * .25;
            return QVector3D(float(std::cos(a)) * L.radius, L.height + L.amp * float(h) - L.amp * .5f,
                             float(std::sin(a)) * L.radius);
        };
        for (int i = 0; i < n; i++) {
            auto a = top(i), b = top(i + 1);
            QVector3D na = QVector3D(-a.x() / L.radius, 1.4f, -a.z() / L.radius).normalized(),
                      nb = QVector3D(-b.x() / L.radius, 1.4f, -b.z() / L.radius).normalized();
            auto at = [&](QVector3D p, float y) {
                p.setY(y);
                return p;
            };
            // Skirt below the horizon (pure haze), then the hill face.
            for (auto [y0, y1, e0, e1] : { std::array<float, 4> { -140, -6, 0, 0 }, std::array<float, 4> { -6, 0, 0, 1 } }) {
                auto a0 = at(a, y0), b0 = at(b, y0), a1 = y1 == 0 ? a : at(a, y1), b1 = y1 == 0 ? b : at(b, y1);
                put(m_ring, a0, na, L.color, Distant, e0 * L.vis);
                put(m_ring, b0, nb, L.color, Distant, e0 * L.vis);
                put(m_ring, b1, nb, L.color, Distant, e1 * L.vis);
                put(m_ring, a0, na, L.color, Distant, e0 * L.vis);
                put(m_ring, b1, nb, L.color, Distant, e1 * L.vis);
                put(m_ring, a1, na, L.color, Distant, e1 * L.vis);
            }
        }
    }
}

void Route3DScene::uploadTile()
{
    const int bytes = int((m_build.size() + m_buildDecals.size()) * sizeof(Vertex));
    m_buffer.bind();
    if (bytes > m_bufferCapacity) {   // grow only (+25 %): later tiles reuse it
        m_bufferCapacity = bytes + bytes / 4;
        m_buffer.allocate(m_bufferCapacity);
    }
    m_buffer.write(0, m_build.data(), int(m_build.size() * sizeof(Vertex)));
    if (!m_buildDecals.empty())
        m_buffer.write(int(m_build.size() * sizeof(Vertex)), m_buildDecals.data(),
                       int(m_buildDecals.size() * sizeof(Vertex)));
    m_meshCount = int(m_build.size());
    m_decalCount = int(m_buildDecals.size());
}

void Route3DScene::scene(double distance)
{
    // a finished job: show its tile
    if (m_tileJob.valid() && m_tileJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_tileJob.get();
            m_vao.bind();
            uploadTile();
            m_meshX = m_jobX;
            m_meshZ = m_jobZ;
        } catch (const std::exception &e) {
            qWarning("Route3D: tile build failed: %s", e.what());
        }
        m_jobX = m_jobZ = INT_MAX;
    }
    auto center = m_route->at(distance);
    const int tileX = int(std::floor(center.x / 32)), tileZ = int(std::floor(center.z / 32));
    if ((tileX == m_meshX && tileZ == m_meshZ) || m_tileJob.valid())
        return;   // current, or a job is running (the next frame checks again)
    if (m_meshCount == 0) {   // nothing to show yet: build it now
        buildTile(tileX, tileZ);
        m_vao.bind();
        uploadTile();
        m_meshX = tileX;
        m_meshZ = tileZ;
        return;
    }
    m_jobX = tileX;
    m_jobZ = tileZ;
    m_tileJob = std::async(std::launch::async, [this, tileX, tileZ] { buildTile(tileX, tileZ); });
}

void Route3DScene::buildTile(int tileX, int tileZ)
{
    const Route &route = *m_route;
    m_build.clear();
    m_buildDecals.clear();
    constexpr int radius = 52, n = 2 * radius + 1;
    int nx = tileX * 4 - radius, nz = tileZ * 4 - radius;
    // Terrain: compute each grid node once (position, smooth normal, tint).
    std::vector<QVector3D> pos(n * n), nor(n * n), col(n * n);
    auto h = [&](int ix, int iz) { return float(m_terrain.node(ix, iz)); };
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            int ix = nx + i, iz = nz + j;
            float x = float(ix * Terrain::spacing), z = float(iz * Terrain::spacing);
            pos[i * n + j] = QVector3D(x, h(ix, iz), z);
            nor[i * n + j] = QVector3D(h(ix - 1, iz) - h(ix + 1, iz), float(2 * Terrain::spacing), h(ix, iz - 1) - h(ix, iz + 1)).normalized();
            col[i * n + j] = groundTint(x, z);
        }
    for (int i = 1; i + 1 < n; i++)
        for (int j = 1; j + 1 < n; j++) {
            float hc = pos[i * n + j].y();
            float concave = (pos[(i - 1) * n + j].y() + pos[(i + 1) * n + j].y() + pos[i * n + j - 1].y() + pos[i * n + j + 1].y()) * .25f
                            - hc;   // >0 in hollows
            float steep = 1 - nor[i * n + j].y();
            float c = std::clamp(concave * .9f, -1.f, 1.f);
            QVector3D tint = c > 0 ? QVector3D(1, 1, 1) * (1 - .10f * c) + QVector3D(.80f, .95f, .75f) * .10f * c   // hollows: deeper green
                                   : QVector3D(1, 1, 1) * (1 + .18f * c) + QVector3D(1.18f, 1.10f, .78f) * (-.18f * c);   // crests: sun-dried
            tint *= 1 - std::min(.22f, steep * .9f);   // steep banks slightly darker
            col[i * n + j] *= tint;
        }
    m_build.reserve(size_t(n * n * 6 + 400000));
    for (int i = 0; i + 1 < n; i++)
        for (int j = 0; j + 1 < n; j++) {
            // Same diagonal as Terrain::surface(), so objects sit on the drawn surface.
            int a = i * n + j, b = (i + 1) * n + j, c = (i + 1) * n + j + 1, d = i * n + j + 1;
            for (int k : { a, b, c, a, c, d })
                put(pos[k], nor[k], col[k], Grass, 0);
        }
    // Draw all nearby sections, including the other side of a hairpin (route
    // points of the cells within 400 m, in route order).
    std::vector<uint32_t> nearby;
    {
        const int c0x = int(std::floor((tileX * 32 - 400) / kRouteCellM)), c1x = int(std::floor((tileX * 32 + 400) / kRouteCellM));
        const int c0z = int(std::floor((tileZ * 32 - 400) / kRouteCellM)), c1z = int(std::floor((tileZ * 32 + 400) / kRouteCellM));
        for (int cx = c0x; cx <= c1x; ++cx)
            for (int cz = c0z; cz <= c1z; ++cz)
                if (auto it = m_routeCells.find(routeCellKey(cx, cz)); it != m_routeCells.end())
                    nearby.insert(nearby.end(), it->second.begin(), it->second.end());
        std::sort(nearby.begin(), nearby.end());
    }
    for (const uint32_t idx : nearby) {
        const size_t i = idx;
        auto p = route.points[i];
        if (std::abs(p.x - tileX * 32) > 400 || std::abs(p.z - tileZ * 32) > 400)
            continue;
        double d = p.distance, e = route.points[i + 1].distance;
        auto a = v(p), b = v(route.points[i + 1]), sa = side(d), sb = side(e);
        auto leftA = a - sa * 3.6f, leftB = b - sb * 3.6f, rightA = a + sa * 3.6f, rightB = b + sb * 3.6f;
        auto below = [&](QVector3D q) {
            q.setY(float(m_terrain.surface(q.x(), q.z())));
            return q;
        };
        // A sloping gravel-to-grass verge rather than a vertical painted wall.
        auto outerLA = below(a - sa * 6.5f), outerLB = below(b - sb * 6.5f);
        auto outerRA = below(a + sa * 6.5f), outerRB = below(b + sb * 6.5f);
        outerLA.setY(outerLA.y() + .03f);
        outerLB.setY(outerLB.y() + .03f);
        outerRA.setY(outerRA.y() + .03f);
        outerRB.setY(outerRB.y() + .03f);
        quad(leftA, leftB, outerLB, outerLA, QVector3D(1, 1, 1), Verge, 0, 0, 1, 1);
        quad(rightA, rightB, outerRB, outerRA, QVector3D(1, 1, 1), Verge, 0, 0, 1, 1);
        quad(leftA, leftB, rightB, rightA, QVector3D(1, 1, 1), Gravel);
        a.setY(a.y() + .025f);
        b.setY(b.y() + .025f);
        // blend carries the lateral position (-1..1) for wheel tracks and edge wear.
        quad(a - sa * 3, b - sb * 3, b + sb * 3, a + sa * 3, QVector3D(1, 1, 1), Asphalt, -1, -1, 1, 1);
        a.setY(a.y() + .02f);
        b.setY(b.y() + .02f);
        for (float sign : { -1.f, 1.f })
            quad(a + sa * (sign * 2.75f - .04f), b + sb * (sign * 2.75f - .04f), b + sb * (sign * 2.75f + .04f),
                 a + sa * (sign * 2.75f + .04f), QVector3D(.87f, .88f, .79f), Paint);
        if (i % 3 == 0)
            quad(a - sa * .06f, b - sb * .06f, b + sb * .06f, a + sa * .06f, QVector3D(.9f, .88f, .73f), Paint);
    }
    double cx = tileX * 32 + 16, cz = tileZ * 32 + 16;
    QVector3D shadowDir = QVector3D(-m_sun.x(), 0, -m_sun.z()).normalized();
    auto ground = [&](QVector3D q) { return float(m_terrain.surface(q.x(), q.z())) + .06f; };
    // Houses and farms (trees avoid their footprints and gardens).
    auto buildings = Settlement::near(m_terrain, cx, cz, 380);
    for (const auto &b : buildings) {
        double dist = std::hypot(b.x - cx, b.z - cz);
        if (dist > 390)
            continue;
        building(b, dist > 150);
        if (dist < 230) {
            QVector3D u(float(b.ux()), 0, float(b.uz()));
            QVector3D c = QVector3D(float(b.x), 0, float(b.z)) + shadowDir * float(b.wall * .6);
            shadowDisc(m_buildDecals, c, u, float(b.width * .5 + 1.6), float(b.length * .5 + 1.6), .40f, ground);
        }
    }
    // Trees: detailed models near the road, simple ones further out (fog hides the rest).
    for (const auto &t : Vegetation::scatter(m_terrain, cx, cz, 300, &buildings)) {
        double dist = std::hypot(t.x - cx, t.z - cz);
        bool far = dist > 115;
        if (t.kind == Spruce)
            spruce(t, far);
        else if (t.kind == Broadleaf)
            broadleaf(t, far);
        else
            bush(t);
        if (dist < 170) {
            QVector3D c = QVector3D(float(t.x), 0, float(t.z)) + shadowDir * float(t.height * (t.kind == Bush ? .15 : .22));
            float r = float(t.radius) * (t.kind == Bush ? 1.1f : 1.25f);
            shadowDisc(m_buildDecals, c, shadowDir, r, r * 1.25f, t.kind == Bush ? .30f : .42f, ground);
        }
    }
}

void Route3DScene::attributes()
{
    for (int i = 0; i < 5; ++i)
        m_shader.enableAttributeArray(i);
    m_shader.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
    m_shader.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, sizeof(Vertex));
    m_shader.setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 3, sizeof(Vertex));
    m_shader.setAttributeBuffer(3, GL_FLOAT, 9 * sizeof(float), 1, sizeof(Vertex));
    m_shader.setAttributeBuffer(4, GL_FLOAT, 10 * sizeof(float), 1, sizeof(Vertex));
}

// --- frame ------------------------------------------------------------------------------------

void Route3DScene::render(const QSize &pixelSize, double distance, double speedKmh, double crank, float time)
{
    if (!m_ready || pixelSize.isEmpty())
        return;
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    // The scene graph can leave masks, blending, stencil and scissor state
    // behind. Reset BEFORE clearing: glClear respects depth/color write masks.
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(GL_LESS);
    glClearDepthf(1.0f);
    glClearColor(.73f, .81f, .88f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    if (!m_route || m_route->points.size() < 2)
        return;   // just the haze colour until a route is set
    const Route &route = *m_route;
    distance = std::clamp(distance, 0.0, route.length());
    scene(distance);

    auto pos = v(route.at(distance));
    auto forward = v(route.at(std::min(route.length(), distance + 12))) - v(route.at(std::max(0.0, distance - 3)));
    if (forward.lengthSquared() < .01f)
        forward = QVector3D(0, 0, -1);
    forward.normalize();
    auto eye = pos + QVector3D(0, kCameraHeightM, 0);
    // A rider's head stays closer to level than the road: follow only part of
    // the gradient, so climbs visibly rise ahead and descents drop away.
    QVector3D flat(forward.x(), 0, forward.z());
    if (flat.lengthSquared() < 1e-4f)
        flat = QVector3D(0, 0, -1);
    flat.normalize();
    float slope = forward.y() / std::max(.2f, std::sqrt(forward.x() * forward.x() + forward.z() * forward.z()));
    auto target = eye + flat * 35;
    target.setY(target.y() - 1.2f + 35 * slope * .4f);
    QMatrix4x4 projection, view;
    projection.perspective(66, float(pixelSize.width()) / float(std::max(1, pixelSize.height())), .15f, 2500);
    view.lookAt(eye, target, QVector3D(0, 1, 0));
    const QVector2D uvOrigin(float(std::floor(eye.x() / kTextureRepeatM) * kTextureRepeatM),
                             float(std::floor(eye.z() / kTextureRepeatM) * kTextureRepeatM));

    for (int i = 0; i < 4; ++i)
        m_textures[size_t(i)]->bind(uint(i));
    m_bannerTexture->bind(4u);
    // 1. Sky: one full-screen triangle, no depth (own, empty vertex array).
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_skyVao.bind();
    m_skyShader.bind();
    m_skyShader.setUniformValue("invViewProj", (projection * view).inverted());
    m_skyShader.setUniformValue("eye", eye);
    m_skyShader.setUniformValue("sunDir", m_sun);
    m_skyShader.setUniformValue("noiseMap", 3);
    m_skyShader.setUniformValue("time", time);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_skyShader.release();
    m_skyVao.release();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // 2. Opaque landscape.
    m_vao.bind();
    m_buffer.bind();
    m_shader.bind();
    m_shader.setUniformValue("mvp", projection * view);
    m_shader.setUniformValue("eye", eye);
    m_shader.setUniformValue("uvOrigin", uvOrigin);
    m_shader.setUniformValue("sunDir", m_sun);
    m_shader.setUniformValue("time", time);
    m_shader.setUniformValue("offset", QVector3D());
    m_shader.setUniformValue("opacity", 1.0f);
    m_shader.setUniformValue("asphaltMap", 0);
    m_shader.setUniformValue("grassMap", 1);
    m_shader.setUniformValue("gravelMap", 2);
    m_shader.setUniformValue("noiseMap", 3);
    m_shader.setUniformValue("bannerMap", 4);
    attributes();
    glDrawArrays(GL_TRIANGLES, 0, m_meshCount);
    // START / FINISH arch
    drawGates(distance);
    // 3. Distant hills, centred on the camera like a skybox.
    m_ringBuffer.bind();
    attributes();
    m_shader.setUniformValue("offset", QVector3D(eye.x(), eye.y(), eye.z()));
    glDrawArrays(GL_TRIANGLES, 0, int(m_ring.size()));
    m_shader.setUniformValue("offset", QVector3D());
    // 4. Soft contact shadows (blended, no depth writes) and the rider.
    if (!m_runner && !m_riderModelTried) {
        m_riderModelTried = true;
        m_ghost.setRiderModel(riderModel());
    }
    const auto &body = m_runner ? m_runnerFigure.build(route, distance, speedKmh, crank)
                                : m_ghost.build(route, distance, speedKmh, crank);
    std::vector<Vertex> ghostVertices;
    {
        auto pose = ghostPose(route, distance, speedKmh);
        QVector3D fwd(float(pose.fx), 0, float(pose.fz));
        if (fwd.lengthSquared() < 1e-4f)
            fwd = QVector3D(0, 0, -1);
        fwd.normalize();
        double along = std::min(route.length(), distance + 3);
        auto roadHeight = [&](QVector3D q) {
            return float(route.at(along + QVector3D::dotProduct(q - QVector3D(float(pose.center.x), 0, float(pose.center.z)), fwd)).y) + .06f;
        };
        QVector3D c(float(pose.center.x), 0, float(pose.center.z));
        // a soft shadow of the body, only a little towards the sun's side
        // (further out it no longer touches the wheels / feet: they look as
        // if they float)
        const QVector3D centre = c;
        c += QVector3D(-m_sun.x(), 0, -m_sun.z()).normalized() * .12f;
        shadowDisc(ghostVertices, c, fwd, m_runner ? .30f : .34f, m_runner ? .45f : .85f, .34f, roadHeight);
        // dark contact spots where the figure touches the road: the tyres'
        // contact patches, each foot while it is down (fading as it lifts)
        const QVector3D right(-fwd.z(), 0, fwd.x());
        auto surface = [&](const QVector3D &q) { return roadHeight(q) - .06f + .025f; };   // asphalt top
        struct Spot { QVector3D sum; int n = 0; float lowest = 1e9f; };
        std::map<std::pair<int, int>, Spot> spots;
        constexpr float kReach = .10f;   // a spot fades out over this height
        for (const auto &g : body) {
            const float h = g.position.y() - surface(g.position);
            if (h > kReach)
                continue;
            const QVector3D d = g.position - centre;
            const auto key = std::make_pair(int(std::floor(QVector3D::dotProduct(d, right) / .14f)),
                                            int(std::floor(QVector3D::dotProduct(d, fwd) / .35f)));
            Spot &s = spots[key];
            s.sum += g.position;
            ++s.n;
            s.lowest = std::min(s.lowest, h);
        }
        for (const auto &[key, s] : spots) {
            if (s.n < 3)
                continue;
            const float strength = .62f * std::clamp(1.f - std::max(0.f, s.lowest) / kReach, 0.f, 1.f);
            if (strength > .02f)
                shadowDisc(ghostVertices, s.sum / float(s.n), fwd, m_runner ? .07f : .045f, m_runner ? .14f : .13f,
                           strength, roadHeight);
        }
    }
    const size_t ghostShadowCount = ghostVertices.size();
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -2);
    if (m_decalCount > 0) {
        m_buffer.bind();
        attributes();
        glDrawArrays(GL_TRIANGLES, m_meshCount, m_decalCount);
    }
    ghostVertices.reserve(ghostVertices.size() + body.size());
    for (const auto &g : body)
        ghostVertices.push_back({ g.position.x(), g.position.y(), g.position.z(), g.color.x(), g.color.y(), g.color.z(),
                                  g.normal.x(), g.normal.y(), g.normal.z(), float(Solid), 0 });
    m_ghostBuffer.bind();
    m_ghostBuffer.allocate(ghostVertices.data(), int(ghostVertices.size() * sizeof(Vertex)));
    attributes();
    glDrawArrays(GL_TRIANGLES, 0, int(ghostShadowCount));
    // Body indices are static per model; re-upload only when the mesh layout changes.
    const auto &bodyIndex = m_runner ? m_runnerFigure.indices() : m_ghost.indices();
    if (m_ghostIndexShadow != ghostShadowCount || m_ghostIndexSource != bodyIndex.data()
        || m_ghostIndices.size() != bodyIndex.size()) {
        m_ghostIndices.resize(bodyIndex.size());
        for (size_t i = 0; i < bodyIndex.size(); ++i)
            m_ghostIndices[i] = bodyIndex[i] + uint32_t(ghostShadowCount);
        m_ghostIndexBuffer.bind();
        m_ghostIndexBuffer.allocate(m_ghostIndices.data(), int(m_ghostIndices.size() * sizeof(uint32_t)));
        m_ghostIndexShadow = ghostShadowCount;
        m_ghostIndexSource = bodyIndex.data();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    if (!m_ghostIndices.empty()) {
        // Opaque rider and bicycle: normal depth writes, no alpha blending.
        m_ghostBuffer.bind();
        attributes();
        m_ghostIndexBuffer.bind();
        glDrawElements(GL_TRIANGLES, int(m_ghostIndices.size()), GL_UNSIGNED_INT, nullptr);
        m_ghostIndexBuffer.release();
    }
    for (int i = 0; i < 4; ++i)
        m_textures[size_t(i)]->release(uint(i));
    m_bannerTexture->release(4u);
    glActiveTexture(GL_TEXTURE0);
    m_shader.release();
    QOpenGLBuffer::release(QOpenGLBuffer::VertexBuffer);
    m_vao.release();
    glDisable(GL_DEPTH_TEST);
}

} // namespace watt
