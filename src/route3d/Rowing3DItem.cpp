#include "Rowing3DItem.h"

#include "RiderAvatar.h"
#include "Route3DScene.h"
#include "glb_model.h"
#include "vegetation.h"
#include "world_noise.h"

#include <QCoreApplication>
#include <QFile>
#include <QPointer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QFont>
#include <QOpenGLVertexArrayObject>
#include <QQuickWindow>

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>
#include <vector>

using route3d::GhostVertex;
using route3d::GlbModel;

namespace watt {

namespace {

// --- the stroke ------------------------------------------------------------------------------------
// Frames: the ROWER frame (GlbModel::poseRowing): x = his right, y = up,
// z = the way he faces (the stern), the seat's top at y = 0. The BOAT
// frame: X = towards the bow (the way the boat moves), Y = up (water at 0),
// Z = the rower's right. boat = (kSeatX - z, y + kSeatY, x).
constexpr float kSeatX = -1.25f;       // the seat at the finish, along the boat
constexpr float kSeatY = 0.03f;        // the seat's top above the water
constexpr float kWaterY = -kSeatY;     // the water, rower frame
constexpr float kCatchZ = 0.53f, kFinishZ = 0.0f;   // at the catch the shins are vertical   // the sliding seat (shins near vertical at the catch)
constexpr float kHipY = 0.12f;   // the hip joint over the seat (the buttocks on it)
constexpr float kAnkleZ = 0.88f, kAnkleY = 0.0f, kAnkleX = 0.09f;   // heels below the hips; legs straight at the finish
constexpr float kPinX = 0.80f, kPinY = 0.24f, kPinZ = 0.42f;   // the oarlocks
constexpr float kInboard = 0.88f, kOutboard = 2.0f;            // oar: handle..pin, pin..blade tip
// oar angle (+ handles to the stern): the blades far towards the bow at the catch, angled
// back towards the stern at the finish (hands past the feet / at the ribs)
constexpr float kCatchDeg = 52.f, kFinishDeg = -28.f;   // (the handles stop in front of the body)
constexpr double kDrive = 0.36;                                 // share of the stroke cycle
constexpr float kGhostLaneZ = -6.5f;   // the ghost's lane (clear of your oars: 2.8 m each side)
constexpr double kRestPhase = 0.0;   // still: ready at the catch (blades squared and buried): the first pull starts here

double ss(double a, double b, double x)
{
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

struct Stroke {
    GlbModel::RowFit fit;
    float theta = 0;    // oar angle (rad)
    float bladeY = 0;   // blade centre height (rower frame)
    float feather = 0;  // 0 = blade square (vertical), 1 = feathered (flat)
};

Stroke strokeAt(double p)
{
    p -= std::floor(p);
    double legs, body, arms, in;
    if (p < kDrive) {
        const double u = p / kDrive;
        legs = ss(0, .65, u);
        body = ss(.25, .85, u);
        arms = ss(.55, 1, u);
        in = 1 - ss(.93, 1, u);
    } else {
        const double v = (p - kDrive) / (1 - kDrive);
        arms = 1 - ss(0, .25, v);
        body = 1 - ss(.12, .42, v);
        legs = 1 - ss(.30, .98, v);
        in = ss(.95, 1, v);   // the blade goes in at the catch
    }
    const double prog = .55 * legs + .25 * body + .20 * arms;
    Stroke s;
    s.theta = float((kCatchDeg + (kFinishDeg - kCatchDeg) * prog) * M_PI / 180.0);
    s.bladeY = float(kWaterY + .18 - .28 * in);
    // squared from just before the catch to the finish, feathered on the recovery
    // (switching inside the recovery: the feathering 0.34..0.42 runs to its end)
    s.feather = float(p < .6 ? ss(kDrive - .02, kDrive + .06, p) : 1 - ss(.86, .96, p));
    const float seatZ = float(kCatchZ + (kFinishZ - kCatchZ) * legs);
    s.fit.hipCenter = QVector3D(0, kHipY, seatZ);
    for (int k = 0; k < 2; ++k) {
        const float side = k ? 1.f : -1.f;
        s.fit.ankle[k] = QVector3D(side * kAnkleX, kAnkleY, kAnkleZ);
        // the handle: inboard of the pin along the oar (the oar is straight through the pin)
        const float dy = (kPinY - s.bladeY) * kInboard / kOutboard;
        const QVector3D handle(side * (kPinX - kInboard * std::cos(s.theta)), kPinY + dy, kPinZ + kInboard * std::sin(s.theta));
        // the left hand rides over the right where they overlap
        s.fit.wrist[k] = handle + QVector3D(0, k ? 0.f : .035f, -.02f);
    }
    s.fit.torsoDeg = 22 - 58 * body;   // body over at the catch, leaning well back at the finish (hands clear of the body)
    s.fit.headDeg = 4;
    return s;
}

// --- geometry ------------------------------------------------------------------------------------
// the boards' letters (one texture row)
constexpr char kGlyphChars[] = "0123456789mFINSH";
constexpr int kGlyphs = 16;
int glyphCell(QChar c)
{
    for (int k = 0; k < kGlyphs; ++k)
        if (c == QLatin1Char(kGlyphChars[k]))
            return k;
    return 0;
}
struct V { float x, y, z, nx, ny, nz, r, g, b, sway = 0; };   // sway: trees in the wind (0 foot .. 1 top)

void put(std::vector<V> &out, QVector3D p, QVector3D n, QVector3D c)
{
    out.push_back({ p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), c.x(), c.y(), c.z(), 0.f });
}

void quad(std::vector<V> &out, QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D col)
{
    QVector3D n = QVector3D::normal(a, b, c);
    for (const QVector3D &p : { a, b, c, a, c, d })
        put(out, p, n, col);
}

// a box from a to b, half widths w (across) and h (the other way: about `up`)
void bar(std::vector<V> &out, QVector3D a, QVector3D b, float w, float h, QVector3D col, QVector3D up = QVector3D(0, 1, 0))
{
    const QVector3D d = (b - a).normalized();
    QVector3D x = QVector3D::crossProduct(d, up);
    if (x.lengthSquared() < 1e-6f)
        x = QVector3D::crossProduct(d, QVector3D(1, 0, 0));
    x.normalize();
    const QVector3D y = QVector3D::crossProduct(x, d).normalized();
    const QVector3D c[4] = { -x * w - y * h, x * w - y * h, x * w + y * h, -x * w + y * h };
    for (int k = 0; k < 4; ++k)
        quad(out, a + c[k], a + c[(k + 1) % 4], b + c[(k + 1) % 4], b + c[k], col);
    quad(out, a + c[3], a + c[2], a + c[1], a + c[0], col);
    quad(out, b + c[0], b + c[1], b + c[2], b + c[3], col);
}

void sphere(std::vector<V> &out, QVector3D c, float r, QVector3D col, int rings = 5, int sides = 8)
{
    auto at = [&](int i, int k) {
        const float t = float(M_PI) * i / rings, f = 2 * float(M_PI) * k / sides;
        return QVector3D(std::sin(t) * std::cos(f), std::cos(t), std::sin(t) * std::sin(f));
    };
    for (int i = 0; i < rings; ++i)
        for (int k = 0; k < sides; ++k) {
            const QVector3D p[4] = { at(i, k), at(i, k + 1), at(i + 1, k + 1), at(i + 1, k) };
            for (int q : { 0, 2, 1, 0, 3, 2 })
                put(out, c + p[q] * r, p[q], col);
        }
}

constexpr QVector3D kHullColor(.74f, .09f, .08f);
constexpr QVector3D kCockpitInside(.22f, .24f, .27f);
constexpr float kHullLength = 8.2f, kHalfBeam = .25f, kKeel = .075f, kSheer = .11f;
constexpr float kHullCenter = kSeatX - (kCatchZ - kFinishZ) * .5f;   // (boat X of the hull's middle)
constexpr float kBow = kHullCenter + kHullLength * .5f;              // (boat X of the bow)
// the hull's shape at boat X: half the width at the sheer, the depth of the
// keel below the water, the sheer's height (higher at the ends)
float hullU(float X) { return std::clamp(2 * (X - kHullCenter) / kHullLength, -1.f, 1.f); }
float hullHalfWidth(float X)
{
    const float u = hullU(X);
    return kHalfBeam * std::pow(std::max(0.f, 1 - u * u), u > 0 ? .85f : .65f);   // finer at the bow
}
float hullDepth(float X)
{
    const float u = hullU(X);
    return kKeel * std::pow(std::max(0.f, 1 - u * u), .55f);
}
float hullSheer(float X)
{
    const float u = hullU(X);
    return kSheer + .025f * u * u;
}

// The oars (and riggers) of a boat at stroke s, in the boat frame.
void oars(std::vector<V> &out, const Stroke &s)
{
    auto boat = [](QVector3D r) { return QVector3D(kSeatX - r.z(), r.y() + kSeatY, r.x()); };
    const QVector3D shaft(.93f, .93f, .92f), blade(.80f, .10f, .12f), metal(.55f, .57f, .60f), grip(.10f, .10f, .11f);
    for (int k = 0; k < 2; ++k) {
        const float side = k ? 1.f : -1.f;
        const QVector3D pin(side * kPinX, kPinY, kPinZ);
        const QVector3D dir(side * std::cos(s.theta), 0, -std::sin(s.theta));   // outwards, along the oar (flat)
        const float dyOut = s.bladeY - kPinY;
        const QVector3D tip = pin + dir * kOutboard + QVector3D(0, dyOut, 0);
        const QVector3D handle = pin - dir * kInboard - QVector3D(0, dyOut * kInboard / kOutboard, 0);
        const QVector3D along = (tip - handle).normalized();
        const QVector3D neck = tip - along * .50f;
        bar(out, boat(handle), boat(handle + along * .28f), .018f, .018f, grip);
        bar(out, boat(handle + along * .28f), boat(neck), .015f, .015f, shaft);
        // the blade: square (vertical) in the water, flat on the recovery
        const QVector3D flatAcross = QVector3D::crossProduct(along, QVector3D(0, 1, 0)).normalized();
        const float a = s.feather * float(M_PI) * .5f;
        const QVector3D across = (QVector3D(0, 1, 0) * std::cos(a) + flatAcross * std::sin(a)).normalized();
        const QVector3D thick = QVector3D::crossProduct(along, across).normalized() * .006f;
        const QVector3D b0 = neck, b1 = tip;
        const QVector3D w0 = across * .07f, w1 = across * .11f;
        quad(out, boat(b0 - w0 + thick), boat(b1 - w1 + thick), boat(b1 + w1 + thick), boat(b0 + w0 + thick), blade);
        quad(out, boat(b0 + w0 - thick), boat(b1 + w1 - thick), boat(b1 - w1 - thick), boat(b0 - w0 - thick), blade * .85f);
        // rigger: two struts from the hull to the pin, and the pin
        for (float dz : { -.28f, .22f })
            bar(out, boat(QVector3D(side * hullHalfWidth(kSeatX - (kPinZ + dz)), .06f, kPinZ + dz)), boat(pin - QVector3D(0, .04f, 0)),
                .012f, .012f, metal);
        bar(out, boat(pin - QVector3D(0, .05f, 0)), boat(pin + QVector3D(0, .06f, 0)), .015f, .015f, metal);
    }
}

// --- models (loaded once per process) ---------------------------------------------------------------

// A single scull in the boat frame (X to the bow, water at Y = 0): 8.2 m, a
// coloured hull with egg-white decks fore and aft, the open cockpit (dark)
// with the seat's tracks, a bow ball and a fin. The rower sits about the
// middle of it (kHullCenter: the middle of the seat's slide).
// the hull's standard colour (the profile can choose another: hull(out, colour))
void hull(std::vector<V> &out, QVector3D hullColor = kHullColor)
{
    constexpr float L = kHullLength, keel = kKeel;
    constexpr int N = 40, R = 8;   // stations along, points round the half section
    // a coloured hull (red), egg-white decks and coamings
    const QVector3D red = hullColor, deck(.94f, .91f, .82f), inside = kCockpitInside, black(.08f, .08f, .09f);
    const QVector3D white = deck;
    // open from the stern deck (past the feet) to the bow deck (past the seat at the finish)
    constexpr float cockpitA = kSeatX - 1.2f, cockpitB = kSeatX + .25f;   // (the back at the finish: at its end)
    auto x = [&](int i) { return kHullCenter - L * .5f + L * float(i) / N; };
    auto width = [&](float X) { return hullHalfWidth(X); };
    auto depth = [&](float X) { return hullDepth(X); };
    auto sheerY = [&](float X) { return hullSheer(X); };
    auto skin = [&](float X, int k) {   // half ellipses from sheer to keel to sheer
        const float a = float(M_PI) * float(k) / R;
        const float w = width(X), d = depth(X), sh = sheerY(X);
        return QVector3D(X, sh - (sh + d) * std::sin(a), w * std::cos(a));
    };
    for (int i = 0; i < N; ++i)
        for (int k = 0; k < R; ++k)
            quad(out, skin(x(i), k), skin(x(i), k + 1), skin(x(i + 1), k + 1), skin(x(i + 1), k), red);
    for (int i = 0; i < N; ++i) {
        const float X0 = x(i), X1 = x(i + 1), Xm = (X0 + X1) * .5f;
        const float w0 = width(X0), w1 = width(X1);
        if (!(Xm > cockpitA && Xm < cockpitB)) {   // decks with a slight crown
            for (float side : { -1.f, 1.f }) {
                const QVector3D e0(X0, sheerY(X0), side * w0), e1(X1, sheerY(X1), side * w1);
                const QVector3D c0(X0, sheerY(X0) + .022f, 0), c1(X1, sheerY(X1) + .022f, 0);
                quad(out, e0, e1, c1, c0, deck);
            }
        } else {   // the cockpit: floor and the inside of the sides
            const float y0 = -.045f;   // (deep enough for the heels)
            // inside the skin: its half width at the floor's depth (a half
            // ellipse from the sheer to the keel) - not out through the round hull
            auto atFloor = [&](float X) {
                const float sh = sheerY(X), sinA = std::clamp((sh - y0) / std::max(1e-4f, sh + depth(X)), 0.f, 1.f);
                return width(X) * std::sqrt(std::max(0.f, 1 - sinA * sinA)) * .9f;
            };
            const float f0 = atFloor(X0), f1 = atFloor(X1);
            quad(out, QVector3D(X0, y0, -f0), QVector3D(X1, y0, -f1), QVector3D(X1, y0, f1), QVector3D(X0, y0, f0), inside);
            for (float side : { -1.f, 1.f })
                quad(out, QVector3D(X0, sheerY(X0), side * w0 * .97f), QVector3D(X1, sheerY(X1), side * w1 * .97f),
                     QVector3D(X1, y0, side * f1), QVector3D(X0, y0, side * f0), inside * 1.3f);
        }
    }
    for (float X : { cockpitA, cockpitB }) {   // coamings at the cockpit's ends
        const float w = width(X);
        quad(out, QVector3D(X, sheerY(X), -w), QVector3D(X, sheerY(X), w), QVector3D(X, sheerY(X) + .05f, w * .3f),
             QVector3D(X, sheerY(X) + .05f, -w * .3f), white * .9f);
    }
    bar(out, QVector3D(kSeatX + .12f, kSeatY - .012f, 0), QVector3D(kSeatX - .12f, kSeatY - .012f, 0), .15f, .01f,
        QVector3D(.55f, .57f, .60f));   // seat (at the finish)
    for (float z : { -.07f, .07f })   // the seat's tracks
        bar(out, QVector3D(kSeatX - .7f, kSeatY - .03f, z), QVector3D(kSeatX + .2f, kSeatY - .03f, z), .012f, .012f,
            QVector3D(.55f, .57f, .60f));
    bar(out, QVector3D(kSeatX - .96f, -.03f, 0), QVector3D(kSeatX - 1.10f, .17f, 0), .12f, .008f, QVector3D(.62f, .64f, .67f),
        QVector3D(0, 0, 1));   // foot stretcher   // foot stretcher
    sphere(out, QVector3D(kBow - .02f, sheerY(kBow) - .01f, 0), .03f, white, 4, 8);                      // bow ball
    const float fin = kHullCenter - 2.75f;   // (well aft)
    quad(out, QVector3D(fin - .15f, -keel * .6f, 0), QVector3D(fin + .15f, -keel * .6f, 0), QVector3D(fin + .09f, -.17f, 0),
         QVector3D(fin - .11f, -.17f, 0), black);   // fin
}

// --- GL --------------------------------------------------------------------------------------------
const char *const kVs = R"(
in vec3 position; in vec3 normal; in vec3 color; in float sway;
uniform mat4 mvp; uniform mat4 model; uniform float time;
out vec3 world; out vec3 nrm; out vec3 col;
void main(){
    vec4 w=model*vec4(position,1.);
    float s=sway*sway;   // the trees' gentle wind sway (as in the GPX Ride world)
    w.x+=sin(time*1.3+w.x*.07+w.z*.05)*.12*s;
    w.z+=cos(time*1.05+w.x*.05-w.z*.04)*.08*s;
    world=w.xyz; nrm=mat3(model)*normal; col=color;
    gl_Position=mvp*w;
}
)";
const char *const kFs = R"(
in vec3 world; in vec3 nrm; in vec3 col;
uniform vec3 eye; uniform vec3 sunDir; uniform float alpha; uniform int mode; uniform float time; uniform float ghost;
uniform sampler2D digits;
out vec4 frag;
vec3 sky(vec3 d){float h=max(d.y,0.);return mix(vec3(.74,.82,.89),vec3(.30,.50,.78),pow(h,.55));}
void main(){
    if(mode==1){frag=vec4(col,1.);return;}
    if(mode==3){   // the distance boards' numbers: black ink on the white board (col.rg = uv)
        float ink=texture(digits,col.rg).a;
        vec3 v3=normalize(world-eye);
        vec3 c3=mix(vec3(.95),vec3(.06),ink)*.95;
        c3=mix(c3,sky(vec3(v3.x,.02,v3.z)),clamp((length(world.xz-eye.xz)-70.)/420.,0.,.9));
        frag=vec4(c3,1.);return;
    }
    vec3 v=normalize(world-eye);
    float dist=length(world.xz-eye.xz);
    vec3 c;
    if(mode==2){
        // water: small moving ripples, the sky mirrored (Fresnel), the sun's glint
        // calm water: soft ripples in several directions, fading with distance
        vec2 p=world.xz;
        float fade=1./(1.+dist*.08);
        vec2 g=vec2(cos(p.x*1.3+p.y*.7+time*1.1)+.6*cos(p.x*.6-p.y*1.9-time*.8)+.4*cos(p.x*3.1+p.y*2.2+time*1.9),
                    sin(p.y*1.1-p.x*.5+time*.9)+.6*sin(p.x*1.7+p.y*.9-time*1.3)+.4*sin(p.y*3.3-p.x*2.4+time*2.1));
        vec3 n=normalize(vec3(g.x*.012*fade,1.,g.y*.012*fade));
        vec3 r=reflect(v,n);
        float f=.02+.98*pow(1.-max(dot(-v,n),0.),5.);
        c=mix(vec3(.10,.27,.33),sky(r)*.92,clamp(f*.9+.15,0.,.85));
        c+=vec3(1.,.95,.85)*pow(max(dot(r,sunDir),0.),220.)*.9*fade;
    }else{
        vec3 n=normalize(nrm);
        if(dot(n,v)>0.)n=-n;   // thin parts (blades) lit on the side in view
        float d=max(dot(n,sunDir),0.);
        c=col*(.42+.62*d)+col*vec3(.06,.08,.12)*(n.y*.5+.5);
        c=mix(c,vec3(.80,.92,1.),ghost*.45);
    }
    c=mix(c,sky(vec3(v.x,.02,v.z)),clamp((dist-70.)/420.,0.,.9));
    frag=vec4(c,alpha);
}
)";

class RowingRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions
{
public:
    ~RowingRenderer() override
    {
        if (QOpenGLContext::currentContext() && m_ready) {
            m_vao.destroy();
            for (QOpenGLBuffer *b : { &m_worldBuf, &m_waterBuf, &m_boatBuf, &m_rowerIdx, &m_skyBuf, &m_textBuf, &m_nearBuf })
                b->destroy();
            for (Posed &pb : m_slots) {
                pb.rower.destroy();
                pb.oars.destroy();
            }
            m_digits.reset();
        }
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        auto *view = static_cast<Rowing3DItem *>(item);
        if (!m_tried) {
            m_tried = true;
            initialize();
            view->setRendererError(m_ready ? QString() : m_error);
        }
        m_frame = view->frame();
        if (!m_avatarSet || view->avatar3d() != m_avatar) {
            // (applied in render(): GL calls belong there, with our VAO bound)
            m_figureWanted = !m_avatarSet || view->avatar3d().model != m_avatar.model;
            m_avatar = view->avatar3d();
            m_avatarSet = true;
            m_avatarDirty = true;
        }
    }

    // The profile's rower (render thread, our VAO bound): the figure - none
    // (the boat alone) while it loads in the background - and its clothes'
    // colours, the boat's colour; both boats posed again.
    void applyAvatar()
    {
        if (m_figureWanted) {
            bool pending = false;
            auto figure = m_avatar.figureIfReady(&pending);
            m_figureWanted = pending;   // (looked for again next frame)
            if (figure != m_rower) {
                m_rower = figure;
                m_rowerIndexCount = 0;
                if (m_rower) {
                    m_rowerIdx.bind();
                    m_rowerIdx.allocate(m_rower->indices().data(), int(m_rower->indices().size() * sizeof(uint32_t)));
                    m_rowerIndexCount = int(m_rower->indices().size());
                }
                m_avatarDirty = true;
            }
        }
        if (!m_avatarDirty)
            return;
        m_avatarDirty = false;
        m_rowerColors = m_rower ? m_avatar.figureColors(*m_rower) : std::vector<QVector3D>();
        // the boat's colour (both boats share the hull)
        std::vector<V> b;
        hull(b, m_avatar.boatColor.x() >= 0 ? m_avatar.boatColor : kHullColor);
        upload(m_boatBuf, b, m_boatCount);
        for (Posed &p : m_slots)
            p.valid = false;
    }

    void render() override
    {
        if (!m_ready)
            return;
        QOpenGLFramebufferObject *fbo = framebufferObject();
        const QSize size = fbo->size();
        glViewport(0, 0, size.width(), size.height());
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(.74f, .82f, .89f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        m_vao.bind();
        applyAvatar();
        m_prog.bind();

        // side view, a little ahead of the boat (it moves to the right)
        // (the distance is the bow's: it starts on the line)
        const float boatX = float(m_frame.boat) - kBow;
        // (on the boat's middle: the rower about the middle of the picture)
        const float lookX = boatX + kHullCenter + .4f;
        const QVector3D eye(lookX, 1.9f, 9.5f), target(lookX, .55f, 0);
        QMatrix4x4 proj, view;
        proj.perspective(33, float(size.width()) / float(std::max(1, size.height())), .3f, 1500);
        view.lookAt(eye, target, QVector3D(0, 1, 0));
        const QMatrix4x4 vp = proj * view;
        m_prog.setUniformValue("eye", eye);
        m_prog.setUniformValue("sunDir", QVector3D(-.35f, .62f, .70f).normalized());
        m_prog.setUniformValue("time", m_frame.time);
        m_prog.setUniformValue("alpha", 1.f);
        m_prog.setUniformValue("ghost", 0.f);

        // sky gradient (full screen, behind everything)
        glDepthMask(GL_FALSE);
        m_prog.setUniformValue("mode", 1);
        m_prog.setUniformValue("mvp", QMatrix4x4());
        m_prog.setUniformValue("model", QMatrix4x4());
        draw(m_skyBuf, m_skyCount);
        glDepthMask(GL_TRUE);

        // the lake and its shores (rebuilt per 200 m)
        const int chunk = int(std::floor(boatX / 200.f));
        if (chunk != m_chunk || m_frame.finish != m_builtFinish) {
            m_chunk = chunk;
            m_builtFinish = m_frame.finish;
            buildWorld(chunk * 200.0);
        }
        const int nearChunk = int(std::floor(boatX / 20.f));
        if (nearChunk != m_nearChunk) {
            m_nearChunk = nearChunk;
            buildNear(nearChunk * 20.0);
        }
        m_prog.setUniformValue("mvp", vp);
        m_prog.setUniformValue("model", QMatrix4x4());
        m_prog.setUniformValue("mode", 2);
        draw(m_waterBuf, m_waterCount);
        m_prog.setUniformValue("mode", 0);
        draw(m_worldBuf, m_worldCount);
        draw(m_nearBuf, m_nearCount);
        if (m_digits) {
            m_digits->bind(0);
            m_prog.setUniformValue("digits", 0);
            m_prog.setUniformValue("mode", 3);
            draw(m_textBuf, m_textCount);
            m_prog.setUniformValue("mode", 0);
        }

        // the boats: yours, then the ghost (half transparent, the next lane out)
        drawBoat(boatX, 0.f, m_frame.phase, 0);
        if (m_frame.ghostVisible && std::abs(m_frame.ghost - m_frame.boat) < 120) {
            // two passes: depth first, then only the front-most surface in
            // colour - else the see-through figure shows its own insides,
            // which pop in and out as the limbs move
            const float gx = float(m_frame.ghost) - kBow;
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            drawBoat(gx, kGhostLaneZ, m_frame.ghostPhase, 1);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            m_prog.setUniformValue("alpha", .42f);
            m_prog.setUniformValue("ghost", 1.f);
            drawBoat(gx, kGhostLaneZ, m_frame.ghostPhase, 1);   // (the same pose: its buffers are reused)
            m_prog.setUniformValue("alpha", 1.f);
            m_prog.setUniformValue("ghost", 0.f);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        m_prog.release();
        m_vao.release();
    }

private:
    void initialize()
    {
        initializeOpenGLFunctions();
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        const QSurfaceFormat f = ctx->format();
        const bool es = ctx->isOpenGLES();
        if (es ? f.majorVersion() < 3 : (f.majorVersion() < 3 || (f.majorVersion() == 3 && f.minorVersion() < 1))) {
            m_error = QStringLiteral("The 3D view needs OpenGL 3.1 or OpenGL ES 3.0.");
            return;
        }
        const QByteArray head = es ? QByteArrayLiteral("#version 300 es\nprecision highp float;\nprecision highp int;\n")
                                   : (f.majorVersion() > 3 || f.minorVersion() >= 3) ? QByteArrayLiteral("#version 330 core\n")
                                                                                     : QByteArrayLiteral("#version 140\n");
        if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, head + kVs)
            || !m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, head + kFs)) {
            m_error = QStringLiteral("Rowing 3D shaders could not be compiled: ") + m_prog.log();
            return;
        }
        m_prog.bindAttributeLocation("position", 0);
        m_prog.bindAttributeLocation("normal", 1);
        m_prog.bindAttributeLocation("color", 2);
        m_prog.bindAttributeLocation("sway", 3);
        if (!m_prog.link()) {
            m_error = QStringLiteral("Rowing 3D shaders could not be linked: ") + m_prog.log();
            return;
        }
        m_vao.create();
        for (QOpenGLBuffer *b : { &m_worldBuf, &m_waterBuf, &m_boatBuf, &m_skyBuf, &m_textBuf, &m_nearBuf })
            b->create();
        {   // digits 0..9 and "m" for the distance boards (black on transparent)
            QImage img(kGlyphs * 64, 96, QImage::Format_RGBA8888);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing);
            QFont f;
            f.setPixelSize(82);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::black);
            for (int k = 0; k < kGlyphs; ++k)
                p.drawText(QRect(k * 64, 0, 64, 96), Qt::AlignCenter, QString(QLatin1Char(kGlyphChars[k])));
            p.end();
            m_digits = std::make_unique<QOpenGLTexture>(img, QOpenGLTexture::GenerateMipMaps);
            m_digits->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            m_digits->setMagnificationFilter(QOpenGLTexture::Linear);
            m_digits->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
        m_rowerIdx = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
        m_rowerIdx.create();
        for (Posed &pb : m_slots)
            for (QOpenGLBuffer *b : { &pb.rower, &pb.oars }) {
                b->create();
                b->setUsagePattern(QOpenGLBuffer::DynamicDraw);
            }
        // sky: two triangles in clip space, horizon colour at the bottom
        {
            std::vector<V> s;
            const QVector3D lo(.80f, .87f, .92f), hi(.33f, .53f, .80f), n(0, 0, 1);
            const QVector3D a(-1, -1, .999f), b(1, -1, .999f), c(1, 1, .999f), d(-1, 1, .999f);
            put(s, a, n, lo); put(s, b, n, lo); put(s, c, n, hi);
            put(s, a, n, lo); put(s, c, n, hi); put(s, d, n, hi);
            upload(m_skyBuf, s, m_skyCount);
        }
        // the single scull (procedural: smooth lines at a few hundred triangles)
        {
            std::vector<V> b;
            hull(b);
            upload(m_boatBuf, b, m_boatCount);
        }
        m_ready = true;   // (the rower: setAvatar(), from synchronize())
    }

    void upload(QOpenGLBuffer &buf, const std::vector<V> &v, int &count)
    {
        buf.bind();
        buf.allocate(v.data(), int(v.size() * sizeof(V)));
        count = int(v.size());
    }

    void attributes()
    {
        for (int i = 0; i < 4; ++i)
            m_prog.enableAttributeArray(i);
        m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(V));
        m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, sizeof(V));
        m_prog.setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 3, sizeof(V));
        m_prog.setAttributeBuffer(3, GL_FLOAT, 9 * sizeof(float), 1, sizeof(V));
    }

    void draw(QOpenGLBuffer &buf, int count)
    {
        if (count <= 0)
            return;
        buf.bind();
        attributes();
        glDrawArrays(GL_TRIANGLES, 0, count);
    }

    // One boat with its rower and oars at stroke phase `phase`. Each boat (0
    // yours, 1 the ghost) keeps its posed figure in its own buffers: posed
    // again only when its phase changed - not on extra frames (resizing the
    // window to full screen draws many frames of the same moment).
    void drawBoat(float x, float z, double phase, int slot)
    {
        QMatrix4x4 model;
        model.translate(x, 0, z);
        m_prog.setUniformValue("model", model);
        draw(m_boatBuf, m_boatCount);
        Posed &pb = m_slots[slot];
        const bool fresh = pb.phase != phase || !pb.valid;
        const Stroke s = strokeAt(phase);
        if (fresh) {
            std::vector<V> o;
            o.reserve(1600);
            oars(o, s);
            pb.oars.bind();
            pb.oars.allocate(o.data(), int(o.size() * sizeof(V)));
            pb.oarCount = int(o.size());
        }
        draw(pb.oars, pb.oarCount);
        if (m_rower && m_rowerIndexCount > 0) {
            if (fresh) {
                m_rower->poseRowing(s.fit, m_posed);
                m_rowerV.resize(m_posed.size());
                const bool recolour = m_rowerColors.size() == m_posed.size();   // (the profile's colours)
                for (size_t i = 0; i < m_posed.size(); ++i) {
                    const GhostVertex &g = m_posed[i];
                    const QVector3D c = recolour ? m_rowerColors[i] : g.color;
                    m_rowerV[i] = { kSeatX - g.position.z(), g.position.y() + kSeatY, g.position.x(),
                                    -g.normal.z(), g.normal.y(), g.normal.x(), c.x(), c.y(), c.z(), 0.f };
                }
                pb.rower.bind();
                pb.rower.allocate(m_rowerV.data(), int(m_rowerV.size() * sizeof(V)));
            }
            pb.rower.bind();
            attributes();
            m_rowerIdx.bind();
            glDrawElements(GL_TRIANGLES, m_rowerIndexCount, GL_UNSIGNED_INT, nullptr);
        }
        pb.phase = phase;
        pb.valid = true;
    }

    // The lane buoys around the boat (they are small: only these few are on
    // screen): from x0 - 30 to x0 + 60 - white every 10 m, red every 100 m,
    // yellow every 500 m.
    void buildNear(double x0)
    {
        std::vector<V> near;
        const float a = float(x0 - 30), b = float(x0 + 60);
        const QVector3D white(.95f, .95f, .93f), red(.86f, .14f, .10f), yellow(1.f, .82f, .12f), orange(1.f, .55f, .12f);
        for (int m = int(std::ceil(a / 10)) * 10; m <= int(b); m += 10)
            for (float z : { kGhostLaneZ * .5f, kGhostLaneZ * 1.5f }) {
                const bool big = m % 500 == 0, hundred = m % 100 == 0;
                sphere(near, QVector3D(float(m), 0.f, z), big ? .16f : .075f,
                       big ? yellow : hundred ? red : (z < kGhostLaneZ ? orange : white), 4, 7);
            }
        upload(m_nearBuf, near, m_nearCount);
    }

    // The lake from x0 - 300 to x0 + 500 (water, shore, hills; the trees and the
    // finish only x0 - 80 .. x0 + 280: what can come into view while the boat
    // is in this 200 m); deterministic (the same metre looks the same).
    void buildWorld(double x0)
    {
        const float a = float(x0 - 300), b = float(x0 + 500);
        std::vector<V> water, w;
        quad(water, QVector3D(a, 0, 30), QVector3D(b, 0, 30), QVector3D(b, 0, -60), QVector3D(a, 0, -60), QVector3D(1, 1, 1));
        upload(m_waterBuf, water, m_waterCount);
        // (the buoys: buildNear(), only around the boat)
        // the distance boards every 250 m on the far side of the course: a
        // white board on a post, facing the course, the distance in black
        std::vector<V> text;
        for (int m = int(std::ceil(a / 250)) * 250; m <= int(b); m += 250) {
            if (m <= 0)
                continue;
            const float x = float(m), z = kGhostLaneZ * 1.5f - 1.5f, bw = 1.6f, y0 = 1.6f, y1 = 2.45f;
            const QVector3D face(0, 0, 1), post(.30f, .31f, .33f);
            bar(w, QVector3D(x, -.3f, z - .05f), QVector3D(x, y0, z - .05f), .06f, .06f, post);
            bar(w, QVector3D(x - bw / 2 - .04f, (y0 + y1) / 2, z - .03f), QVector3D(x + bw / 2 + .04f, (y0 + y1) / 2, z - .03f),
                (y1 - y0) / 2 + .04f, .02f, QVector3D(.10f, .30f, .70f), face);   // frame
            const QString label = QString::number(m) + QStringLiteral("m");
            const float ch = (y1 - y0) * .8f, cw = ch * .62f;
            float cx = x - cw * label.size() * .5f;
            // the whole board's face (white where there is no digit) as glyph cells
            const float fy0 = y0 + .03f, fy1 = y1 - .03f;
            auto glyph = [&](float gx0, float gx1, int cell) {
                const float u0 = cell / float(kGlyphs), u1 = (cell + 1) / float(kGlyphs);
                const float yy0 = (y0 + y1) / 2 - ch / 2, yy1 = yy0 + ch;
                text.push_back({ gx0, yy0, z, 0, 0, 1, u0, 1, 0, 0 });
                text.push_back({ gx1, yy0, z, 0, 0, 1, u1, 1, 0, 0 });
                text.push_back({ gx1, yy1, z, 0, 0, 1, u1, 0, 0, 0 });
                text.push_back({ gx0, yy0, z, 0, 0, 1, u0, 1, 0, 0 });
                text.push_back({ gx1, yy1, z, 0, 0, 1, u1, 0, 0, 0 });
                text.push_back({ gx0, yy1, z, 0, 0, 1, u0, 0, 0, 0 });
            };
            quad(w, QVector3D(x - bw / 2, fy0, z - .005f), QVector3D(x + bw / 2, fy0, z - .005f), QVector3D(x + bw / 2, fy1, z - .005f),
                 QVector3D(x - bw / 2, fy1, z - .005f), QVector3D(.95f, .95f, .95f));
            for (const QChar c : label) {
                glyph(cx, cx + cw, glyphCell(c));
                cx += cw;
            }
        }
        if (m_frame.finish > 0 && m_frame.finish >= x0 - 80 && m_frame.finish <= x0 + 300)
            finishArea(w, text, float(m_frame.finish));
        upload(m_textBuf, text, m_textCount);
        // the far shore: a grass bank, a tree line, hills behind
        const QVector3D grass(.33f, .52f, .22f), dark(.17f, .33f, .15f), hill(.30f, .46f, .30f), sand(.62f, .58f, .45f);
        quad(w, QVector3D(a, -.2f, -55), QVector3D(b, -.2f, -55), QVector3D(b, .5f, -58), QVector3D(a, .5f, -58), sand);
        quad(w, QVector3D(a, .5f, -58), QVector3D(b, .5f, -58), QVector3D(b, 1.6f, -90), QVector3D(a, 1.6f, -90), grass);
        // the trees of the GPX Ride / Treadmill Run world (Route3DScene's
        // spruces, broadleaves and bushes): a few rows along the shore
        std::vector<Route3DScene::MeshVertex> trees;
        for (int i = int(std::floor((x0 - 80) / 6)); i <= int((x0 + 280) / 6); ++i) {
            for (int row = 0; row < 4; ++row) {
                const double r1 = route3d::WorldNoise::random(i, row, 3101), r2 = route3d::WorldNoise::random(i, row, 3107);
                const double r3 = route3d::WorldNoise::random(i, row, 3109);
                if (r1 < (row == 0 ? .45 : .2))
                    continue;
                if (m_frame.finish > 0 && (i + r2) * 6.0 > m_frame.finish - 40 && (i + r2) * 6.0 < m_frame.finish + 24 && row < 3)
                    continue;   // the finish's tower and grandstand stand here
                route3d::TreeSpec t;
                t.x = (i + r2) * 6.0;
                t.z = -60.5 - row * 6.5 - r1 * 3.5;
                t.y = .55 + row * .3;
                t.seed = route3d::WorldNoise::hash(i, row, 3111);
                t.kind = row == 0 && r3 < .3 ? route3d::Bush : r3 < .55 ? route3d::Spruce : route3d::Broadleaf;
                const double size = r2;
                if (t.kind == route3d::Spruce) { t.height = 9 + 8 * size; t.radius = t.height * .25; }
                else if (t.kind == route3d::Broadleaf) { t.height = 7 + 7 * size; t.radius = t.height * .33; }
                else { t.height = 1.3 + 1.6 * size; t.radius = t.height * .65; }
                m_treeMaker.treeMesh(t, row >= 2, trees);
            }
        }
        for (const auto &m : trees)
            w.push_back({ m.position.x(), m.position.y(), m.position.z(), m.normal.x(), m.normal.y(), m.normal.z(),
                          m.color.x(), m.color.y(), m.color.z(), m.sway });
        // hills (a ridge with gentle noise)
        for (float x = std::floor(a / 40) * 40; x < b; x += 40) {
            auto hgt = [](float xx) { return 6.f + 16.f * float(route3d::WorldNoise::fbm(xx / 260.0, 3.7, 3, 3203)); };
            const float shade = .9f + .2f * float(route3d::WorldNoise::random(int(x / 40), 0, 3209));
            quad(w, QVector3D(x, 1.5f, -110), QVector3D(x + 40, 1.5f, -110), QVector3D(x + 40, hgt(x + 40), -260),
                 QVector3D(x, hgt(x), -260), hill * shade);
        }
        upload(m_worldBuf, w, m_worldCount);
    }

    // The finish: a line of red buoys across the course, red-and-white posts
    // at its ends, the judges' tower with "FINISH" on the far shore and a
    // grandstand full of small spectators waving (their arms sway).
    void finishArea(std::vector<V> &w, std::vector<V> &text, float f)
    {
        const QVector3D red(.86f, .12f, .10f), white(.96f, .96f, .94f), wall(.93f, .91f, .86f), roof(.55f, .17f, .12f);
        const QVector3D glass(.16f, .26f, .38f), wood(.50f, .38f, .26f), concrete(.70f, .70f, .68f);
        // the line across the lanes, the posts
        for (float z = 1.6f; z >= kGhostLaneZ * 1.5f; z -= 1.15f)
            sphere(w, QVector3D(f, 0, z), .13f, red, 4, 7);
        for (float z : { kGhostLaneZ * 1.5f - .8f, 3.0f }) {
            for (int k = 0; k < 6; ++k)
                bar(w, QVector3D(f, -.2f + k * .5f, z), QVector3D(f, .3f + k * .5f, z), .07f, .07f, k % 2 ? white : red);
        }
        // the judges' tower on the shore at the line: two storeys, a glazed top floor, a red roof
        const float tz0 = -59.f, tz1 = -65.f, tx0 = f - 4.f, tx1 = f + 4.f, base = .6f;
        auto block = [&](float x0, float x1, float y0, float y1, float z0, float z1, QVector3D c) {
            bar(w, QVector3D((x0 + x1) / 2, y0, (z0 + z1) / 2), QVector3D((x0 + x1) / 2, y1, (z0 + z1) / 2), (x1 - x0) / 2,
                (z0 - z1) / 2, c, QVector3D(0, 0, 1));
        };
        block(tx0, tx1, base, base + 3.f, tz0, tz1, wall);
        block(tx0 + .1f, tx1 - .1f, base + 3.f, base + 5.6f, tz0 - .1f, tz1 + .1f, glass);
        for (float x = tx0 + .1f; x <= tx1 - .05f; x += 1.3f)   // the window frames
            block(x - .06f, x + .06f, base + 3.f, base + 5.6f, tz0 + .02f, tz0 - .02f, white);
        block(tx0 - .4f, tx1 + .4f, base + 5.6f, base + 5.85f, tz0 + .4f, tz1 - .4f, white);
        // a gabled roof
        quad(w, QVector3D(tx0 - .5f, base + 5.85f, tz0 + .5f), QVector3D(tx1 + .5f, base + 5.85f, tz0 + .5f),
             QVector3D(tx1 + .5f, base + 7.4f, (tz0 + tz1) / 2), QVector3D(tx0 - .5f, base + 7.4f, (tz0 + tz1) / 2), roof);
        quad(w, QVector3D(tx0 - .5f, base + 7.4f, (tz0 + tz1) / 2), QVector3D(tx1 + .5f, base + 7.4f, (tz0 + tz1) / 2),
             QVector3D(tx1 + .5f, base + 5.85f, tz1 - .5f), QVector3D(tx0 - .5f, base + 5.85f, tz1 - .5f), roof * .8f);
        // door and ground-floor windows
        block(f - .5f, f + .5f, base, base + 2.1f, tz0 + .03f, tz0 - .01f, wood);
        for (float x : { tx0 + 1.2f, tx1 - 1.2f })
            block(x - .6f, x + .6f, base + 1.f, base + 2.2f, tz0 + .03f, tz0 - .01f, glass);
        // the FINISH board on the roof's front edge, facing the course
        {
            const float by0 = base + 5.95f, by1 = base + 7.0f, bz = tz0 + .7f;
            bar(w, QVector3D(f - 2.6f, (by0 + by1) / 2, bz - .03f), QVector3D(f + 2.6f, (by0 + by1) / 2, bz - .03f),
                (by1 - by0) / 2 + .06f, .03f, red, QVector3D(0, 0, 1));
            quad(w, QVector3D(f - 2.5f, by0, bz), QVector3D(f + 2.5f, by0, bz), QVector3D(f + 2.5f, by1, bz),
                 QVector3D(f - 2.5f, by1, bz), white);
            const QString label = QStringLiteral("FINISH");
            const float ch = (by1 - by0) * .85f, cw = ch * .62f;
            float cx = f - cw * label.size() * .5f;
            for (const QChar c : label) {
                const int cell = glyphCell(c);
                const float u0 = cell / float(kGlyphs), u1 = (cell + 1) / float(kGlyphs), yy0 = (by0 + by1) / 2 - ch / 2, yy1 = yy0 + ch;
                const float z = bz + .01f;
                for (const V &v : { V { cx, yy0, z, 0, 0, 1, u0, 1, 0, 0 }, V { cx + cw, yy0, z, 0, 0, 1, u1, 1, 0, 0 },
                                    V { cx + cw, yy1, z, 0, 0, 1, u1, 0, 0, 0 }, V { cx, yy0, z, 0, 0, 1, u0, 1, 0, 0 },
                                    V { cx + cw, yy1, z, 0, 0, 1, u1, 0, 0, 0 }, V { cx, yy1, z, 0, 0, 1, u0, 0, 0, 0 } })
                    text.push_back(v);
                cx += cw;
            }
        }
        // flags on poles
        for (float x : { tx0 - 2.f, tx1 + 2.f }) {
            bar(w, QVector3D(x, base, tz0 + 1.f), QVector3D(x, base + 9.f, tz0 + 1.f), .05f, .05f, white);
            V flag[6];
            const QVector3D fc = x < f ? red : QVector3D(.10f, .30f, .75f);
            const QVector3D p0(x, base + 8.9f, tz0 + 1.f), p1(x + 1.6f, base + 8.9f, tz0 + 1.f), p2(x + 1.6f, base + 7.9f, tz0 + 1.f),
                p3(x, base + 7.9f, tz0 + 1.f);
            const QVector3D pts[6] = { p0, p1, p2, p0, p2, p3 };
            const float sw[6] = { 0, 1, 1, 0, 1, 0 };
            for (int k = 0; k < 6; ++k)
                flag[k] = { pts[k].x(), pts[k].y(), pts[k].z(), 0, 0, 1, fc.x(), fc.y(), fc.z(), sw[k] };
            w.insert(w.end(), flag, flag + 6);
        }
        // the grandstand before the line: four steps
        const float gx0 = f - 34.f, gx1 = f - 7.f;
        for (int k = 0; k < 4; ++k) {
            const float z0 = -58.5f - k * .9f, y = base + .45f * (k + 1);
            block(gx0, gx1, base, y, z0, z0 - .9f, concrete * (k % 2 ? .95f : 1.f));
        }
        // the spectators: small figures on the steps and along the bank, many waving
        static const QVector3D shirts[] = { { .85f, .12f, .10f }, { .95f, .95f, .93f }, { .95f, .78f, .12f }, { .12f, .32f, .78f },
                                            { .15f, .55f, .25f }, { .95f, .45f, .10f }, { .90f, .45f, .62f }, { .12f, .12f, .13f } };
        static const QVector3D skins[] = { { .93f, .76f, .64f }, { .80f, .60f, .45f }, { .55f, .38f, .26f } };
        auto person = [&](QVector3D foot, uint32_t seed) {
            auto rnd = [&](int k) { return float(route3d::WorldNoise::random(int(seed % 100003), k, 3301)); };
            const float h = 1.55f + .3f * rnd(1);
            const QVector3D shirt = shirts[int(rnd(2) * 8) % 8], skin = skins[int(rnd(3) * 3) % 3];
            const QVector3D legs = rnd(4) < .5f ? QVector3D(.18f, .22f, .32f) : QVector3D(.12f, .12f, .13f);
            bar(w, foot, foot + QVector3D(0, h * .47f, 0), .09f, .07f, legs, QVector3D(0, 0, 1));
            bar(w, foot + QVector3D(0, h * .47f, 0), foot + QVector3D(0, h * .82f, 0), .13f, .08f, shirt, QVector3D(0, 0, 1));
            sphere(w, foot + QVector3D(0, h * .90f, 0), h * .065f, skin, 3, 6);
            // arms: up and waving (they sway), or down
            const bool cheering = rnd(5) < .65f;
            for (float side : { -1.f, 1.f }) {
                const QVector3D sh = foot + QVector3D(side * .15f, h * .80f, 0);
                const QVector3D hand = cheering ? sh + QVector3D(side * .18f, h * .36f, .05f) : sh + QVector3D(side * .04f, -h * .33f, 0);
                const size_t from = w.size();
                bar(w, sh, hand, .035f, .035f, cheering ? skin : shirt);
                if (cheering)   // the upper end waves
                    for (size_t i = from; i < w.size(); ++i)
                        w[i].sway = std::clamp((w[i].y - sh.y()) / (h * .36f), 0.f, 1.f) * 1.6f;
            }
        };
        uint32_t seed = 1;
        for (int k = 0; k < 4; ++k) {
            const float z = -58.95f - k * .9f, y = base + .45f * (k + 1);
            for (float x = gx0 + .4f; x < gx1 - .3f; x += .75f) {
                ++seed;
                if (route3d::WorldNoise::random(int(seed), k, 3303) < .18)
                    continue;
                person(QVector3D(x + .2f * float(route3d::WorldNoise::random(int(seed), 1, 3305)), y, z), seed * 7919u);
            }
        }
        for (float x = f - 6.f; x < f + 18.f; x += 1.1f) {   // along the water's edge past the tower
            ++seed;
            if (std::abs(x - f) < 4.5f || route3d::WorldNoise::random(int(seed), 9, 3307) < .3)
                continue;
            person(QVector3D(x, .58f, -58.6f - 1.2f * float(route3d::WorldNoise::random(int(seed), 2, 3309))), seed * 104729u);
        }
    }

    bool m_tried = false, m_ready = false;
    QString m_error;
    Rowing3DItem::Frame m_frame;
    QOpenGLShaderProgram m_prog;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_worldBuf, m_waterBuf, m_boatBuf, m_skyBuf, m_textBuf, m_nearBuf;
    std::unique_ptr<QOpenGLTexture> m_digits;
    int m_textCount = 0;
    struct Posed {
        QOpenGLBuffer rower, oars;
        int oarCount = 0;
        double phase = -1;
        bool valid = false;
    };
    Posed m_slots[2];
    QOpenGLBuffer m_rowerIdx { QOpenGLBuffer::IndexBuffer };
    int m_worldCount = 0, m_waterCount = 0, m_boatCount = 0, m_skyCount = 0, m_rowerIndexCount = 0;
    int m_chunk = INT_MIN, m_nearChunk = INT_MIN;
    int m_nearCount = 0;
    double m_builtFinish = -1;
    std::shared_ptr<const GlbModel> m_rower;
    RiderAvatar m_avatar { RiderAvatar::Rower };
    bool m_avatarSet = false, m_avatarDirty = false, m_figureWanted = false;
    std::vector<QVector3D> m_rowerColors;
    Route3DScene m_treeMaker;   // (geometry only: never initialised for GL)
    std::vector<GhostVertex> m_posed;
    std::vector<V> m_rowerV;
};

} // namespace

void rowerPreviewPose(const route3d::GlbModel &figure, double phase, std::vector<route3d::GhostVertex> &out)
{
    figure.poseRowing(strokeAt(phase).fit, out);
    // (afloat: the heels in the cockpit, under the water line, are not seen -
    // flattened onto it, behind the hull's side, as rowerPreviewBoat())
    for (route3d::GhostVertex &v : out)
        v.position.setY(std::max(v.position.y(), kWaterY + .03f));   // (a little above: the hull narrows down there)
}

QVector3D standardHullColor()
{
    return kHullColor;
}

float rowerHullOverhang(const route3d::GlbModel &figure)
{
    constexpr float kOverGunwale = .06f;
    // the rower's points between the water and the sheer that are outside
    // the hull's side (the skin there), over the stroke - how far the worst
    // one is out
    float worst = 0;
    std::vector<route3d::GhostVertex> v;
    for (int k = 0; k < 24; ++k) {
        figure.poseRowing(strokeAt(k / 24.0).fit, v);
        for (const auto &g : v) {
            const float X = kSeatX - g.position.z(), Y = g.position.y() + kSeatY;
            // (what the water hides is not seen; well above the gunwale the
            // legs are free - just over it they lie on it: within it)
            if (Y > hullSheer(X) + kOverGunwale || Y < 0 || X < kBow - kHullLength || X > kBow)
                continue;
            // the skin's half width at this height (a half ellipse from the sheer to the keel)
            const float sh = hullSheer(X), d = hullDepth(X), w = hullHalfWidth(X);
            const float sinA = std::clamp((sh - Y) / std::max(1e-4f, sh + d), 0.f, 1.f);
            const float half = w * std::sqrt(std::max(0.f, 1 - sinA * sinA));
            worst = std::max(worst, std::abs(g.position.x()) - half);
        }
    }
    return worst;
}

void rowerPreviewBoat(QVector3D hullColor, double halfLength, std::vector<route3d::GhostVertex> &out)
{
    std::vector<V> b;
    hull(b, hullColor.x() >= 0 ? hullColor : kHullColor);
    out.clear();
    // boat frame (x to the bow) -> rower frame (z forward, facing the stern):
    // the inverse of the view's rower placement
    for (size_t t = 0; t + 2 < b.size(); t += 3) {
        bool near = true;
        for (size_t k = t; k < t + 3; ++k)
            near = near && std::abs(b[k].x - kSeatX) < halfLength;
        if (!near)
            continue;
        // (as afloat: what is under the water is not seen - flattened onto it)
        bool dry = false;
        for (size_t k = t; k < t + 3; ++k)
            dry = dry || b[k].y > 0;
        if (!dry)
            continue;
        for (size_t k = t; k < t + 3; ++k)
            out.push_back({ QVector3D(b[k].z, std::max(b[k].y, 0.f) - kSeatY, kSeatX - b[k].x),
                            QVector3D(b[k].nz, b[k].ny, -b[k].nx), QVector3D(b[k].r, b[k].g, b[k].b) });
    }
}

void Rowing3DItem::setAvatar(const QVariantMap &values)
{
    if (values == m_avatarValues)
        return;
    m_avatarValues = values;
    m_avatar = RiderAvatar::fromVariant(values, RiderAvatar::Rower);
    emit avatarChanged();
    update();
    // loaded in the background: drawn again when it is there
    m_avatar.preload([self = QPointer<Rowing3DItem>(this)] {
        QMetaObject::invokeMethod(qApp, [self] { if (self) self->update(); }, Qt::QueuedConnection);
    });
}

Rowing3DItem::Rowing3DItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    setMirrorVertically(true);
    m_clock.start();
    m_frameClock.start();
    m_ghostClock.start();
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Rowing3DItem::onFrame);
    m_supported = QQuickWindow::graphicsApi() == QSGRendererInterface::OpenGL;
    setFlag(ItemHasContents, m_supported);
    if (!m_supported)
        m_error = QStringLiteral("The 3D view needs OpenGL, which is not available on this computer.");
    m_frame.phase = kRestPhase;
    m_frame.ghostPhase = kRestPhase;
    schedule();
}

QQuickFramebufferObject::Renderer *Rowing3DItem::createRenderer() const
{
    return new RowingRenderer;
}

#define SETTER(name, member, type)                  \
    void Rowing3DItem::name(type v)                 \
    {                                               \
        if (member == v)                            \
            return;                                 \
        member = v;                                 \
        emit inputChanged();                        \
        schedule();                                 \
    }
SETTER(setDistance, m_distance, double)
SETTER(setSpeedMps, m_speed, double)
SETTER(setStrokeRate, m_rate, int)
SETTER(setRowing, m_rowing, bool)
SETTER(setGhostVisible, m_ghostVisible, bool)
SETTER(setFinishDistance, m_finish, double)
#undef SETTER

void Rowing3DItem::setGhostDistance(double v)
{
    if (m_ghostDistance == v)
        return;
    // the ghost's speed from its updates (it has no pace of its own here),
    // measured over at least 0.5 s and smoothed: updates come in uneven steps
    if (m_lastGhostTarget < 0 || v < m_lastGhostTarget) {   // a new race
        m_ghostClock.restart();
        m_lastGhostTarget = v;
        m_ghostSpeed = 0;
    } else {
        const double dt = m_ghostClock.elapsed() / 1000.0;
        if (dt >= .5) {
            const double speed = std::clamp((v - m_lastGhostTarget) / dt, 0.0, 8.0);
            m_ghostSpeed = m_ghostSpeed <= 0 ? speed : m_ghostSpeed * .7 + speed * .3;
            m_ghostClock.restart();
            m_lastGhostTarget = v;
        }
    }
    m_ghostDistance = v;
    emit inputChanged();
    schedule();
}

void Rowing3DItem::setRendererError(const QString &error)
{
    QMetaObject::invokeMethod(this, [this, error] {
        if (m_error == error)
            return;
        m_error = error;
        emit statusChanged();
        schedule();
    }, Qt::QueuedConnection);
}

void Rowing3DItem::schedule()
{
    // only while shown and drawable (like Route3DItem)
    const int want = !isVisible() || !m_supported || !m_error.isEmpty() ? 0 : m_rowing ? kFrameMs : kIdleFrameMs;
    if (want == 0) {
        m_timer.stop();
    } else if (!m_timer.isActive() || m_timer.interval() != want) {
        if (!m_timer.isActive())
            m_frameClock.restart();
        m_timer.start(want);
    }
}

void Rowing3DItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickFramebufferObject::itemChange(change, value);
    if (change == ItemVisibleHasChanged) {
        schedule();
        if (isVisible())
            update();
    }
}

void Rowing3DItem::onFrame()
{
    const double dt = std::clamp(m_frameClock.restart() / 1000.0, 0.0, .25);
    // the boats: on with their speed, pulled towards the session's distance
    if (m_first || std::abs(m_distance - m_shownBoat) > 30) {
        m_shownBoat = m_distance;
        m_shownGhost = m_ghostDistance;
        m_first = false;
    }
    if (m_rowing)
        m_shownBoat += m_speed * dt;
    // (a gentle pull: the rower's distance arrives in ~1 m steps 4 x a second;
    // a strong pull made the boat - and with it the camera, the world and the
    // ghost - surge and stall with every step)
    m_shownBoat += (m_distance - m_shownBoat) * std::min(1.0, dt * .8);
    if (std::abs(m_ghostDistance - m_shownGhost) > 30)
        m_shownGhost = m_ghostDistance;
    // no new ghost distance for a while (it has finished, or stopped): it stops
    if (m_ghostClock.elapsed() > 1500 && m_lastGhostTarget >= 0 && m_ghostDistance == m_lastGhostTarget)
        m_ghostSpeed = 0;
    if (m_rowing)
        m_shownGhost += m_ghostSpeed * dt;
    // (a gentle pull: the ghost's distance arrives in steps)
    m_shownGhost += (m_ghostDistance - m_shownGhost) * std::min(1.0, dt * .8);
    // never past where it really is, nor past the finish
    m_shownGhost = std::min(m_shownGhost, m_ghostDistance + 2.0);
    if (m_finish > 0)
        m_shownGhost = std::min(m_shownGhost, m_finish);
    // the ghost rows while it moves (with some hysteresis: no stop-start on a slow update)
    if (m_ghostSpeed > .4)
        m_ghostMoving = true;
    else if (m_ghostSpeed < .15)
        m_ghostMoving = false;
    // the stroke: at the rower's rate; still = glide to the rest position
    auto advance = [&](double &phase, double rate, bool moving) {
        if (moving && rate > 0) {
            phase += dt * rate / 60.0;
        } else {
            // glide on (through the recovery) to the ready position
            const double frac = phase - std::floor(phase - kRestPhase) - kRestPhase;   // 0..1 past the rest phase
            if (frac > .01 && frac < .99)
                phase = std::min(phase + dt * .6, std::floor(phase - kRestPhase) + kRestPhase + 1.0);
        }
        phase -= std::floor(phase);
    };
    advance(m_frame.phase, m_rate, m_rowing);
    // the ghost's own steady rate (24 / min, faster when it is quick), not the
    // rower's changing one
    const double ghostRate = std::clamp(18.0 + 2.0 * m_ghostSpeed, 20.0, 34.0);
    advance(m_frame.ghostPhase, ghostRate, m_rowing && m_ghostMoving);
    m_frame.boat = m_shownBoat;
    m_frame.ghost = m_shownGhost;
    m_frame.ghostVisible = m_ghostVisible;
    m_frame.finish = m_finish;
    m_frame.time = float(m_clock.elapsed() / 1000.0);
    if (isVisible())
        update();
}

} // namespace watt
