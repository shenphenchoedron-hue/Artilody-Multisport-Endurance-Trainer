#include "Route3DScene.h"

#include <QFile>
#include <map>

#include "materials.h"
#include "settlement.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace route3d;

namespace watt {

namespace {

// Shared GLSL: sky colour and the matching horizon haze, so fogged terrain
// melts into the sky instead of into a flat colour.
const char *const kSkyCommon = R"(
uniform vec3 sunDir;
uniform vec3 zenith;uniform vec3 horizon;uniform vec3 sunGlow;   // per biome (biome.h)
vec3 skyColor(vec3 d){
    float h=max(d.y,0.);
    vec3 c=mix(horizon,zenith,pow(h,.5));
    float s=max(dot(d,sunDir),0.);
    c+=sunGlow*(pow(s,6.)*.20+pow(s,48.)*.25);
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
    if(mat==12){ // wind turbine blades: turn around the hub (colour) and the rotor axis (normal); blend = phase
        float a=time*1.25+blend;
        vec3 k=normalize(normal),h=color+offset,d=p-h;
        p=h+d*cos(a)+cross(k,d)*sin(a)+k*dot(k,d)*(1.-cos(a));
        gl_Position=mvp*vec4(p,1);col=vec3(.90,.91,.92);world=p;norm=normal;edge=0.;return;
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
uniform float opacity;uniform float time;
// the biome (biome.h): light, ground and water
uniform vec3 sunLight;uniform vec3 ambientSky;uniform vec3 ambientGround;
uniform vec3 grassTint;uniform vec3 sandTint;uniform float sandCover;
uniform float snowCover;uniform float snowLine;uniform float treeSnow;uniform float iceWater;
uniform vec3 waterShallow;uniform vec3 waterDeep;
out vec4 frag;
)";

const char *const kFragmentShaderBody = R"(
vec3 grass(vec2 p){
    vec3 a=texture(grassMap,p*.42).rgb;
    vec3 b=texture(grassMap,mat2(.8,-.6,.6,.8)*p*.13+.37).rgb; // rotated second scale hides tiling
    vec3 m=texture(noiseMap,p*.006).rgb;                          // 40 m and 10 m patches
    vec3 g=mix(a,b,.45)*(.80+m.g*.40);
    return mix(g,g*vec3(1.12,1.05,.74),smoothstep(.55,.80,m.r)*.5)*grassTint;
}
vec3 sand(vec2 p){
    vec3 g=texture(gravelMap,p*.35).rgb;
    float m=texture(noiseMap,p*.02).b;
    return mix(vec3(.80,.73,.56),g*vec3(1.10,1.06,1.),.35)*(.92+.16*m)*sandTint;
}
// any snow in this biome (uniforms only: no texture work where there is none)
bool hasSnow(){return snowCover>0.||snowLine<1e4;}
// the snow line, a little ragged (h: the height in metres)
float snowAt(vec2 p,float h){
    float m=texture(noiseMap,p*.004).b;
    return max(snowCover,smoothstep(snowLine-18.,snowLine+18.,h+(m-.5)*50.));
}
vec3 snow(vec2 p){
    float m=texture(noiseMap,p*.05).g,f=texture(grassMap,p*.6).g;
    return vec3(.86,.89,.94)*(.93+.07*m+.05*(f-.45));
}
// The open ground of the biome over the grass g: sand in patches (deserts,
// the outback), snow (the icy north, above the mountains' snow line). up:
// how level the ground is (normal y; steep ground keeps no snow).
vec3 biomeGround(vec3 g,vec2 p,float up,float h){
    if(sandCover>0.){
        float m=texture(noiseMap,p*.011).g*.6+texture(noiseMap,p*.05).b*.4;
        float s=smoothstep(.36,.62,sandCover+(m-.5)*.7);
        g=mix(g,sand(p),s);
    }
    if(!hasSnow())return g;   // (uniform: the same branch for every fragment)
    float sn=snowAt(p,h)*smoothstep(.55,.80,up+(texture(noiseMap,p*.07).r-.5)*.2);
    return mix(g,snow(p),sn);
}
// Lakes and the sea: animated ripples, sky reflection (Fresnel), sun glitter,
// shallow water lighter and a pale rim where it meets the shore.
vec3 water(vec2 p,vec3 view,float dist,float depth){
    vec3 v=view/dist;
    if(iceWater>.5){   // frozen: pale, dull ice with a faint sky sheen and snow drifts
        vec3 m=texture(noiseMap,p*.045).rgb;
        vec3 ice=mix(vec3(.62,.74,.82),vec3(.84,.90,.94),m.g);
        ice=mix(ice,snow(p),smoothstep(.55,.75,m.r)*.8);
        ice*=.70+.30*max(sunDir.y,0.)+.15;
        return mix(ice,skyColor(reflect(v,vec3(0.,1.,0.))),.12);
    }
    vec3 a=texture(noiseMap,p*.030+vec2(time*.011,time*.006)).rgb;
    vec3 b=texture(noiseMap,p*.085-vec2(time*.014,-time*.009)).rgb;
    float calm=1.-smoothstep(60.,260.,dist)*.6;   // ripples fade with distance (no shimmer)
    vec3 n=normalize(vec3((a.r-b.g)*.30*calm,1.,(a.g-b.r)*.30*calm));
    vec3 sky=skyColor(reflect(v,n));
    float fresnel=.03+.97*pow(1.-max(dot(-v,n),0.),5.);
    float d=clamp(depth/2.8,0.,1.);
    vec3 body=mix(waterShallow,waterDeep,d)*(.62+.38*max(dot(n,sunDir),0.));
    vec3 c=mix(body,sky,clamp(fresnel+.10,0.,1.));
    c+=vec3(1.,.93,.80)*pow(max(dot(reflect(-sunDir,n),-v),0.),110.)*.9;
    return mix(c,vec3(.80,.84,.82),(1.-smoothstep(0.,.22,depth))*.40);
}
void main(){
    vec3 view=world-eye;float dist=length(view);
    float fog=max(1.-exp(-dist*dist/(290.*290.)),smoothstep(335.,395.,dist));
    if(mat==13)fog=1.-exp(-dist*dist/(620.*620.));   // birds: high up the air is clearer (seen beyond the landscape)
    if(mat==6){frag=vec4(.03,.05,.07,smoothstep(0.,.38,edge)*.52*(1.-fog));return;}
    vec2 uv=world.xz-uvOrigin;
    vec3 haze=hazeColor(view);
    if(mat==10){frag=vec4(mix(water(uv,view,max(dist,.01),edge),haze,fog),1.);return;}
    vec3 base=col;
    if(mat==1){
        base=texture(asphaltMap,uv*.8).rgb*1.06;
        float lat=abs(edge);
        float tracks=smoothstep(.10,.24,lat)*(1.-smoothstep(.26,.40,lat))+smoothstep(.60,.74,lat)*(1.-smoothstep(.76,.90,lat));
        base*=1.-.09*tracks;
        base*=1.+.12*smoothstep(.88,1.,lat); // dusty edges
        base*=.90+.20*texture(noiseMap,uv*.018).b; // repair patches
    }
    if(mat==2){base=grass(uv)*col;if(edge>.002)base=mix(base,sand(uv),clamp(edge,0.,1.));
        float up=normalize(norm).y;
        base=biomeGround(base,uv,up,world.y);
        // steep slopes (mountainsides): grey rock and scree instead of grass
        // (patchy, so it reads as rock showing through the turf, not as paint)
        float patch=texture(noiseMap,uv*.06).r;
        float steep=smoothstep(.78,.58,up+(patch-.5)*.18);
        if(steep>0.){
            float g=texture(gravelMap,uv*.22).r;
            vec3 rock=mix(vec3(.36,.35,.33),vec3(.58,.56,.52),g)*(.85+.3*patch);
            base=mix(base,rock,steep*.9);
        }
    }
    if(mat==3)base=texture(gravelMap,uv*.75).rgb;
    if(mat==4){float grain=texture(asphaltMap,uv*.8).r;base=col*(.9+grain*.35);}
    if(mat==9){
        vec4 t=texture(bannerMap,col.rg);
#ifdef CUTOUT
        if(t.a<.5)discard;   // cut-out letters: only in the gate program (discard costs early depth tests)
#endif
        base=t.rgb;
    }
    if(mat==11){   // polka dots (the KOM arch): col.rg = surface position in dot periods
        vec2 g=col.rg;g.x+=.5*mod(floor(g.y),2.);
        float r=length(fract(g)-.5),w=fwidth(r)*.75+.01;
        base=mix(vec3(.80,.10,.09),vec3(.95,.94,.91),smoothstep(.27-w,.27+w,r));
    }
    if(mat==5){
        vec3 g=biomeGround(grass(uv),uv,1.,world.y);
        vec3 stone=texture(gravelMap,uv*.75).rgb;
        float variation=texture(grassMap,uv*1.5).g-.46;
        float mixValue=smoothstep(.12,.95,clamp(edge+variation*.32*sin(edge*3.14159),0.,1.));
        base=mix(stone,g,mixValue);
    }
    vec3 n=normalize(norm);
    if(mat==8&&hasSnow()){   // snow on the trees (the icy north, above the snow line)
        float s=max(treeSnow,snowAt(uv,world.y)*.85);
        if(s>0.)base=mix(base,vec3(.84,.87,.92),s*smoothstep(.35,.85,n.y));
    }
    if(mat==12&&dot(n,view)>0.)n=-n;   // turbine blades: one face, lit on the side you see
    float ndl=dot(n,sunDir);
    // Foliage uses wrapped diffuse: light scatters through leaves and needles.
    float diffuse=mat==8?clamp((ndl+.45)/1.45,0.,1.):max(ndl,0.);
    vec3 ambient=mix(ambientGround,ambientSky,n.y*.5+.5);
    vec3 lit=base*(ambient*.95+sunLight*diffuse*.92);
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
uniform float cloudShift;   // > 0: fewer clouds (biome.h)
out vec4 frag;
)";

const char *const kSkyFragmentShaderBody = R"(
void main(){
    vec3 d=normalize(ray);vec3 c=skyColor(d);
    if(d.y>0.){
        vec2 uv=d.xz/(d.y+.10)*.10+vec2(time*.0020,time*.0007);
        vec4 a=texture(noiseMap,uv),b=texture(noiseMap,uv*2.7+.31);
        float density=a.r*.62+b.g*.38;
        float cover=smoothstep(.50+cloudShift,.76+cloudShift,density)*smoothstep(.0,.22,d.y);
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
// The road's cross section (from the centre line): gravel shoulder to
// kGravelEdgeM, then the sloping verge down to the terrain at kVergeEdgeM.
constexpr float kGravelEdgeM = 3.6f, kVergeEdgeM = 6.5f;
constexpr float kArchHalfSpan = 4.4f;   // tube centre beside the road (beyond the gravel)
constexpr float kArchLegHeight = 3.0f;  // straight legs, then a half circle
constexpr float kArchTube = .48f;       // inflated tube radius
constexpr float kBannerHalfWidth = 2.6f;   // banner height: from its printed face (arch())
constexpr double kArchClearM = 10.0;       // a KOM arch this close to START / FINISH is left out

// The one banner texture (1024 x 1024 RGBA, mipmapped) holds every printed
// face, so the arches, the bike's label, the sprint and the KOM add no
// texture memory:
//   rows   0..255  "START" white on green               (arch banner)
//   rows 256..511  "FINISH" white on red (the red runs on to row 527, so its
//                  edge filters red)                       (arch banner)
//   rows 528..639  sprint signs: green "SPRINT" (x 0..511) and red
//                  "SPRINT FINISH" (x 512..1023)
//   rows 640..751  KOM banners: red "KOM" on white with red dots (x 0..511)
//                  and "KOM FINISH" white on red (x 512..1023)
//                  (each 512 x 112 cell is sampled 12 px inside its colour)
//   rows 752..783  digits 0..9 for the kilometre posts: black on opaque
//                  white (no cut-out: far away they blur to grey instead of
//                  vanishing), 40 px apart (kDigit())
//   rows 816..919  road text "SPRINT IN 200 m"          (kRoadAnnounce)
//   rows 920..1023 road text "SPRINT" (x 0..511)         (kRoadSprint)
//                  and the bike's brand label (x 512..1023, kLabel)
// The KOM arch's polka-dot tube is drawn by the shader (material Polka).
// Road text and the label are white letters on transparent (cut out in the
// shader); transparent rows keep them apart from the opaque faces, whose
// colour would bleed in at the coarse mip levels small faces sample.
struct TexRect { float u0, v0, u1, v1; };
constexpr TexRect texPixels(int x, int y, int w, int h)
{
    return { x / 1024.f, y / 1024.f, (x + w) / 1024.f, (y + h) / 1024.f };
}
constexpr int kCellRows = 112, kCellInset = 12;   // sign / KOM banner cells: 512 x 112
constexpr int kSprintCellRow = 528, kKomCellRow = 640;
constexpr int kDigitRow = 752, kDigitCell = 40;
constexpr TexRect kDigit(int digit)
{
    return texPixels(digit * kDigitCell + 5, kDigitRow + 1, kDigitCell - 10, 30);
}
constexpr TexRect cellFace(int x, int row)
{
    return texPixels(x + kCellInset, row + kCellInset, 512 - 2 * kCellInset, kCellRows - 2 * kCellInset);
}
constexpr TexRect kStartBanner = texPixels(0, 0, 1024, 256);
constexpr TexRect kFinishBanner = texPixels(0, 256, 1024, 256);
constexpr TexRect kSprintSign = cellFace(0, kSprintCellRow);
constexpr TexRect kSprintFinishSign = cellFace(512, kSprintCellRow);
constexpr TexRect kKomBanner = cellFace(0, kKomCellRow);
constexpr TexRect kKomFinishBanner = cellFace(512, kKomCellRow);
constexpr TexRect kRoadAnnounce = texPixels(0, 816, 1024, 104);
constexpr TexRect kRoadSprint = texPixels(0, 920, 512, 104);
constexpr TexRect kLabel = texPixels(512, 920, 512, 104);
const QString kBikeBrand = QStringLiteral("Artilody");

// The largest bold font (up to maxPx) that fits `text` (one line per '\n') in r.
QFont fittedFont(const QString &text, const QRect &r, int maxPx, bool italic = false)
{
    QFont f = QGuiApplication::font();
    f.setWeight(QFont::Black);
    f.setItalic(italic);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int px = maxPx; px > 8; px -= 2) {
        f.setPixelSize(px);
        const QFontMetrics m(f);
        int widest = 0;
        for (const QString &l : lines)
            widest = std::max(widest, m.horizontalAdvance(l));
        if (widest <= r.width() * .92 && m.height() * lines.size() <= r.height() * 1.02)
            break;
    }
    return f;
}

QImage bannerImage()
{
    QImage img(1024, 1024, QImage::Format_RGBA8888);
    img.fill(QColor(255, 255, 255, 0));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    auto rgb = [](QVector3D c) { return QColor::fromRgbF(c.x(), c.y(), c.z()); };
    auto half = [&](int y, QVector3D c, const QString &text) {
        const QRect r(0, y, 1024, 256);
        p.fillRect(r, rgb(c));
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
    p.fillRect(QRect(0, 512, 1024, 16), rgb(kFinishRed * 1.1f));
    half(256, kFinishRed * 1.1f, QStringLiteral("FINISH"));
    // sign / KOM banner cells: the colour fills the whole cell, the face is sampled inside it
    auto cell = [&](int x, int row, QVector3D c, const QString &text, bool dots = false) {
        const QRect all(x, row, 512, kCellRows);
        const QRect face = all.adjusted(kCellInset, kCellInset, -kCellInset, -kCellInset);
        const QColor ink = dots ? rgb(kFinishRed) : QColor(Qt::white);
        p.fillRect(all, dots ? QColor(242, 240, 233) : rgb(c));
        if (dots) {   // the polka-dot jersey of the best climber
            p.setPen(Qt::NoPen);
            p.setBrush(rgb(kFinishRed));
            for (int y = 0; y * 26 < kCellRows + 26; ++y)
                for (int k = -1; k * 26 < 512 + 26; ++k)
                    p.drawEllipse(QPointF(x + k * 26 + (y % 2) * 13, row + y * 26), 6.5, 6.5);
            p.setBrush(QColor(242, 240, 233));
            p.drawRoundedRect(face.adjusted(70, 10, -70, -10), 14, 14);
            p.setBrush(Qt::NoBrush);
        }
        p.setPen(QPen(ink, 5));
        p.drawRect(face.adjusted(6, 6, -6, -6));
        const QRect inner = face.adjusted(dots ? 80 : 16, 12, dots ? -80 : -16, -12);
        p.setFont(fittedFont(text, inner, 96));
        p.setPen(ink);
        p.drawText(inner, Qt::AlignCenter, text);
    };
    cell(0, kSprintCellRow, kStartGreen * 1.1f, QStringLiteral("SPRINT"));
    cell(512, kSprintCellRow, kFinishRed * 1.1f, QStringLiteral("SPRINT\nFINISH"));
    cell(0, kKomCellRow, kFinishRed, QStringLiteral("KOM"), true);
    cell(512, kKomCellRow, kFinishRed * 1.1f, QStringLiteral("KOM FINISH"));
    // white letters on transparent: road text and the bike's label
    auto cutOut = [&](const QRect &r, const QString &text, bool italic) {
        const QRect inner = r.adjusted(0, 4, 0, -4);
        p.setFont(fittedFont(text, inner, 104, italic));
        p.setPen(Qt::white);
        p.drawText(inner, Qt::AlignCenter, text);
    };
    {   // kilometre post digits: black on white
        p.fillRect(QRect(0, kDigitRow, 10 * kDigitCell, 32), Qt::white);
        const QFont f = fittedFont(QStringLiteral("0"), QRect(0, 0, kDigitCell - 16, 27), 34);
        p.setFont(f);
        p.setPen(QColor(18, 18, 18));
        for (int k = 0; k < 10; ++k)
            p.drawText(QRect(k * kDigitCell, kDigitRow, kDigitCell, 32), Qt::AlignCenter, QString::number(k));
    }
    cutOut(QRect(0, 816, 1024, 104), QStringLiteral("SPRINT IN 200 m"), false);
    cutOut(QRect(0, 920, 512, 104), QStringLiteral("SPRINT"), false);
    cutOut(QRect(512, 920, 512, 104), kBikeBrand, true);
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
    m_biomeUniformsDirty = true;   // new programs
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
    // The same shaders with cut-out letters (discard), only for the gate
    // buffer: a shader that may discard loses early depth testing on many
    // GPUs (the Raspberry Pi's among them), so the landscape never uses it.
    const QByteArray cutoutFs = head + "#define CUTOUT\n" + kFragmentShaderHead + kSkyCommon + kFragmentShaderBody;
    if (!m_cutoutShader.addShaderFromSourceCode(QOpenGLShader::Vertex, head + kVertexShader)
        || !m_cutoutShader.addShaderFromSourceCode(QOpenGLShader::Fragment, cutoutFs)) {
        m_error = QStringLiteral("3D shaders could not be compiled: ") + m_cutoutShader.log();
        return false;
    }
    bindAttributes(m_cutoutShader);
    if (!m_cutoutShader.link()) {
        m_error = QStringLiteral("3D shaders could not be linked: ") + m_cutoutShader.log();
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
    if (context->hasExtension(QByteArrayLiteral("GL_EXT_texture_filter_anisotropic"))) {
        GLfloat limit = 1;   // road text is seen at a grazing angle: keep it sharp
        glGetFloatv(0x84FF, &limit);
        m_bannerTexture->setMaximumAnisotropy(std::min(8.f, limit));
    }
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
    m_birdBuffer.create();
    m_birdBuffer.setUsagePattern(QOpenGLBuffer::StreamDraw);
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
    m_birdBuffer.destroy();
    m_ringBuffer.destroy();
    m_vao.destroy();
    m_skyVao.destroy();
    m_shader.removeAllShaders();
    m_cutoutShader.removeAllShaders();
    m_skyShader.removeAllShaders();
    m_ready = false;
}

void Route3DScene::setRoute(std::shared_ptr<const Route> route)
{
    waitForTile();   // the job reads the route and the terrain
    m_route = std::move(route);
    m_perches.clear();
    m_birdGround = BirdGround();
    applyProfile();
    if (m_route)
        m_terrain.reset(*m_route);   // keeps a pointer: m_route owns the route
    else
        m_terrain = Terrain();
    indexRoute();
    m_benches = m_route ? Benches::along(m_terrain, *m_route) : std::vector<BenchSpec>();
    placeDisplayBike();
    m_meshX = m_meshZ = INT_MAX;
    m_build.clear();
    m_buildDecals.clear();
    m_meshCount = m_decalCount = 0;
    m_gatesDirty = true;
    m_ghostIndices.clear();
    m_ghostIndexShadow = size_t(-1);
    m_ghostIndexSource = nullptr;
}

// The route's biome: its profile (the mountains' snow line from the road's
// heights), the sun, the terrain's relief and lakes, the distant hills.
void Route3DScene::applyProfile()
{
    const Biome biome = m_route ? Biome(std::clamp(m_route->biome, 0, int(Biome::Mountain))) : Biome::Nordic;
    double lo = 0, mean = 0, hi = 0;
    if (m_route && !m_route->points.empty()) {
        lo = 1e18;
        hi = -1e18;
        for (const auto &p : m_route->points) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
            mean += p.y;
        }
        mean /= double(m_route->points.size());
    }
    const bool ringChanged = biome != m_profile.biome;
    m_profile = Biomes::profile(biome, lo, mean, hi);
    m_sun = QVector3D(m_profile.sun[0], m_profile.sun[1], m_profile.sun[2]).normalized();
    m_terrain = Terrain();
    m_terrain.hillScale = m_profile.hillScale;
    m_terrain.lakeChance = m_profile.lakeChance;
    m_biomeUniformsDirty = true;
    if (ringChanged) {
        buildRing();
        m_ringDirty = true;
    }
}

void Route3DScene::setBiomeUniforms(QOpenGLShaderProgram &program, bool sky)
{
    const BiomeProfile &p = m_profile;
    program.setUniformValue("zenith", rgb(p.zenith));
    program.setUniformValue("horizon", rgb(p.horizon));
    program.setUniformValue("sunGlow", rgb(p.sunGlow));
    if (sky) {
        program.setUniformValue("cloudShift", p.cloudShift);
        return;
    }
    program.setUniformValue("sunLight", rgb(p.sunLight));
    program.setUniformValue("ambientSky", rgb(p.ambientSky));
    program.setUniformValue("ambientGround", rgb(p.ambientGround));
    program.setUniformValue("grassTint", rgb(p.grassTint));
    program.setUniformValue("sandTint", rgb(p.sandTint));
    program.setUniformValue("sandCover", p.sandCover);
    program.setUniformValue("snowCover", p.snowCover);
    program.setUniformValue("snowLine", p.snowLine);
    program.setUniformValue("treeSnow", p.treeSnow);
    program.setUniformValue("iceWater", p.iceWater ? 1.f : 0.f);
    program.setUniformValue("waterShallow", rgb(p.waterShallow));
    program.setUniformValue("waterDeep", rgb(p.waterDeep));
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
    // applied in scene() when no tile job runs (the job reads the bike's spot
    // and the terrain; the arches read the terrain too)
    m_startDistance = drawnMetres;
    m_startDirty = true;
}

// An inflatable arch: a fat tube up one side, over the road in a half circle
// and down the other, in light/dark segments like the sewn panels of a real
// one, with a printed banner on both faces (readable from either side).
void Route3DScene::arch(std::vector<Vertex> &out, double d, ArchStyle style)
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
    const bool polka = style == KomStartArch;
    const QVector3D color = style == StartArch ? kStartGreen : kFinishRed;
    const TexRect banner = style == StartArch ? kStartBanner
                           : style == FinishArch ? kFinishBanner
                           : style == KomStartArch ? kKomBanner : kKomFinishBanner;
    // the banner keeps the printed face's proportions (square texture)
    const float bannerHeight = 2 * kBannerHalfWidth * (banner.v1 - banner.v0) / (banner.u1 - banner.u0);
    // polka dots: whole periods around the tube (no seam), about as long along it
    constexpr int dotsAround = 6;
    const float dotPeriod = float(2 * 3.14159265 * kArchTube / dotsAround);
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
        const float alongA = along;
        along += (b - a).length();
        const bool seam = int(along / 1.3f) % 2 == 0;
        const QVector3D col = color * (seam ? 1.0f : .86f);
        // slight bulge between seams: inflated panels
        for (int k = 0; k < sides; ++k) {
            const float a0 = float(6.2831853 * k / sides), a1 = float(6.2831853 * (k + 1) / sides);
            const QVector3D r0 = n * std::cos(a0) + f * std::sin(a0), r1 = n * std::cos(a1) + f * std::sin(a1);
            const QVector3D p00 = a + r0 * kArchTube, p01 = a + r1 * kArchTube, p10 = b + r0 * kArchTube,
                            p11 = b + r1 * kArchTube;
            if (polka) {
                const float ua = alongA / dotPeriod, ub = along / dotPeriod;
                const float v0 = float(k) * dotsAround / sides, v1 = float(k + 1) * dotsAround / sides;
                put(out, p00, r0, QVector3D(ua, v0, 0), Polka, 0);
                put(out, p10, r0, QVector3D(ub, v0, 0), Polka, 0);
                put(out, p11, r1, QVector3D(ub, v1, 0), Polka, 0);
                put(out, p00, r0, QVector3D(ua, v0, 0), Polka, 0);
                put(out, p11, r1, QVector3D(ub, v1, 0), Polka, 0);
                put(out, p01, r1, QVector3D(ua, v1, 0), Polka, 0);
                continue;
            }
            put(out, p00, r0, col, Solid, 0);
            put(out, p10, r0, col, Solid, 0);
            put(out, p11, r1, col, Solid, 0);
            put(out, p00, r0, col, Solid, 0);
            put(out, p11, r1, col, Solid, 0);
            put(out, p01, r1, col, Solid, 0);
        }
    }
    // banner on the front (towards the approaching rider) and on the back
    const float by1 = top + kArchHalfSpan - .25f, by0 = by1 - bannerHeight;
    for (float side : { -1.f, 1.f }) {   // -1: front (faces -f)
        const QVector3D off = f * (side * (kArchTube + .03f));
        const QVector3D n = f * side;
        auto P = [&](float lateral, float y) { return QVector3D(c.x(), y, c.z()) + s * lateral + off; };
        // front: left edge (-s) = u 0; back: seen from behind, left edge is +s
        const float uL = side < 0 ? banner.u0 : banner.u1, uR = side < 0 ? banner.u1 : banner.u0;
        const QVector3D tl(uL, banner.v0, 0), tr(uR, banner.v0, 0), bl(uL, banner.v1, 0), br(uR, banner.v1, 0);
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

// The arches and the display bike -> m_gateBuffer. Only while no tile job
// runs (scene()): arch() reads the terrain, whose cache the job writes.
void Route3DScene::buildGates()
{
    const double length = m_route->length();
    m_gateVertices.clear();
    // START ahead of the start point (seen whole before you ride through
    // it), FINISH just before the end (you finish under it)
    m_builtStartGate = startGate(length);
    arch(m_gateVertices, m_builtStartGate, StartArch);
    m_startGateCount = int(m_gateVertices.size());
    const double finishArch = std::max(0.0, m_route->finish() - 1.0);
    arch(m_gateVertices, finishArch, FinishArch);
    m_finishGateEnd = int(m_gateVertices.size());
    displayBike(m_gateVertices);
    for (const SprintMarks &s : m_sprints)
        sprintMarks(m_gateVertices, s);
    // the KOM: polka-dot arch at the foot of the climb, red at the top (not
    // into the START / FINISH arch when the climb begins or ends there)
    for (const KomMarks &kom : m_koms) {
        if (std::abs(kom.start - m_builtStartGate) > kArchClearM && std::abs(kom.start - finishArch) > kArchClearM)
            arch(m_gateVertices, kom.start, KomStartArch);
        if (std::abs(kom.finish - m_builtStartGate) > kArchClearM && std::abs(kom.finish - finishArch) > kArchClearM)
            arch(m_gateVertices, kom.finish, KomFinishArch);
    }
    // kilometre posts (the short put() writes here meanwhile)
    m_target = &m_gateVertices;
    kmPosts();
    finishTunnel(finishArch);
    m_target = &m_build;
    m_gateBuffer.bind();
    m_gateBuffer.allocate(m_gateVertices.data(), int(m_gateVertices.size() * sizeof(Vertex)));
    m_gatesDirty = false;
}

void Route3DScene::drawGates(double distance)
{
    if (!m_route || m_gateVertices.empty())
        return;
    // START until you have ridden through it, then FINISH (a loop has both at one place)
    const bool finishHalf = distance > m_builtStartGate + 2.0;
    m_gateBuffer.bind();
    attributes();
    if (finishHalf)
        glDrawArrays(GL_TRIANGLES, m_startGateCount, m_finishGateEnd - m_startGateCount);
    else
        glDrawArrays(GL_TRIANGLES, 0, m_startGateCount);
    if (int(m_gateVertices.size()) > m_finishGateEnd)
        glDrawArrays(GL_TRIANGLES, m_finishGateEnd, int(m_gateVertices.size()) - m_finishGateEnd);
}

namespace {
// The display bike (:/models/trail-bike.glb, or WATT_TRAIL_BIKE_GLB=<file>,
// =none for no bike). Loaded once per process; null when missing / unusable.
std::shared_ptr<const route3d::StaticModel> trailBikeModel()
{
    static const std::shared_ptr<const route3d::StaticModel> model = []() -> std::shared_ptr<const route3d::StaticModel> {
        const QString override = qEnvironmentVariable("WATT_TRAIL_BIKE_GLB");
        if (override == QLatin1String("none"))
            return {};
        const QString path = override.isEmpty() ? QStringLiteral(":/models/trail-bike.glb") : override;
        if (!QFile::exists(path))
            return {};
        auto m = std::make_shared<route3d::StaticModel>();
        QString error;
        if (!m->loadFile(path, &error)) {
            qWarning("Route3D: display bike %s not used: %s", qPrintable(path), qPrintable(error));
            return {};
        }
        return m;
    }();
    return model;
}

constexpr double kBikeBeforeGateM = 6.0;   // along the road, before the START arch
constexpr float kBikeLateralM = 5.6f;      // from the road's centre line, on the verge
constexpr float kBikeTurnDeg = 35.f;       // turned towards the approaching rider
} // namespace

float Route3DScene::DisplaySpot::groundAt(QVector3D q) const
{
    const float lat = lateral + QVector3D::dotProduct(q - pos, roadSide);
    const float t = std::clamp((lat - kGravelEdgeM) / (kVergeEdgeM - kGravelEdgeM), 0.f, 1.f);
    return roadY + (outerY - roadY) * t;
}

void Route3DScene::placeDisplayBike()
{
    m_bikeSpot = DisplaySpot();
    if (!m_route || m_route->points.size() < 2 || !trailBikeModel())
        return;
    const Route &route = *m_route;
    const double d = std::max(0.0, startGate(route.length()) - kBikeBeforeGateM);
    QVector3D f = v(route.at(std::min(route.length(), d + 3))) - v(route.at(std::max(0.0, d - 3)));
    f.setY(0);
    if (f.lengthSquared() < 1e-6f)
        f = QVector3D(0, 0, -1);
    f.normalize();
    const QVector3D right(-f.z(), 0, f.x()), up(0, 1, 0);
    const QVector3D road = v(route.at(d));
    DisplaySpot &b = m_bikeSpot;
    b.roadSide = right;
    b.lateral = kBikeLateralM;
    b.roadY = road.y();
    const QVector3D outer = road + right * kVergeEdgeM;
    b.outerY = float(m_terrain.surface(outer.x(), outer.z())) + .03f;
    b.pos = road + right * kBikeLateralM;
    b.pos.setY(b.groundAt(b.pos) - .04f);   // the post a little into the ground
    // its decal side (+z) faces the road and the rider coming up to it
    const float a = qDegreesToRadians(kBikeTurnDeg);
    b.z = (-right * std::cos(a) - f * std::sin(a)).normalized();
    b.x = QVector3D::crossProduct(up, b.z);
    b.valid = true;
}

void Route3DScene::displayBike(std::vector<Vertex> &out)
{
    const auto model = trailBikeModel();
    if (!m_bikeSpot.valid || !model)
        return;
    const DisplaySpot &b = m_bikeSpot;
    const QVector3D up(0, 1, 0);
    auto at = [&](QVector3D p) { return b.pos + b.x * p.x() + up * p.y() + b.z * p.z(); };
    auto dir = [&](QVector3D n) { return (b.x * n.x() + up * n.y() + b.z * n.z()).normalized(); };
    out.reserve(out.size() + model->triangles().size() + 6 * model->decals().size());
    for (const auto &t : model->triangles())
        put(out, at(t.position), dir(t.normal), t.color, Solid, 0);
    // the brand name on the frame (the model's decal places), from the banner texture
    for (const auto &d : model->decals()) {
        const QVector3D n = d.normal, u = d.up, r = QVector3D::crossProduct(u, n).normalized();
        const QVector3D c = d.at + n * .008f;   // just off the tube
        const float w = d.width * .5f, h = d.height * .5f;
        const QVector3D wn = dir(n);
        const QVector3D tl(kLabel.u0, kLabel.v0, 0), tr(kLabel.u1, kLabel.v0, 0), bl(kLabel.u0, kLabel.v1, 0),
                        br(kLabel.u1, kLabel.v1, 0);
        const QVector3D pTL = at(c - r * w + u * h), pTR = at(c + r * w + u * h), pBL = at(c - r * w - u * h),
                        pBR = at(c + r * w - u * h);
        put(out, pTL, wn, tl, Banner, 0);
        put(out, pTR, wn, tr, Banner, 0);
        put(out, pBR, wn, br, Banner, 0);
        put(out, pTL, wn, tl, Banner, 0);
        put(out, pBR, wn, br, Banner, 0);
        put(out, pBL, wn, bl, Banner, 0);
    }
}

void Route3DScene::setKmPosts(const std::vector<double> &drawnMetres)
{
    if (drawnMetres == m_kmPosts)
        return;
    m_kmPosts = drawnMetres;
    m_gatesDirty = true;   // rebuilt in scene() (reads the terrain: no tile job then)
}

float Route3DScene::vergeHeight(double d, float lateral) const
{
    const Route &route = *m_route;
    d = std::clamp(d, 0.0, route.length());
    const QVector3D road = v(route.at(d));
    const QVector3D s = side(d);
    const float lat = std::abs(lateral), sign = lateral < 0 ? -1.f : 1.f;
    if (lat <= kGravelEdgeM)
        return road.y();
    if (lat >= kVergeEdgeM) {
        const QVector3D q = road + s * (sign * lat);
        return float(m_terrain.surface(q.x(), q.z()));
    }
    const QVector3D outer = road + s * (sign * kVergeEdgeM);
    const float outerY = float(m_terrain.surface(outer.x(), outer.z())) + .03f;
    return road.y() + (outerY - road.y()) * (lat - kGravelEdgeM) / (kVergeEdgeM - kGravelEdgeM);
}

// The finisher tunnel: crowd barriers with coloured banners along both edges
// of the road from before the FINISH arch to the end of the run-out (before
// it tapers away), and flags on poles behind them.
void Route3DScene::finishTunnel(double finishArch)
{
    const Route &route = *m_route;
    const double from = std::max(0.0, finishArch - 40.0);
    const double to = std::min(route.length() - kTaperM - 2.0, finishArch + 45.0);
    if (to <= from + 5 || std::abs(finishArch - m_builtStartGate) < 60)
        return;   // (no run-out, or a loop: start and finish at one place)
    const QVector3D up(0, 1, 0), steel(.62f, .64f, .67f);
    static const QVector3D banners[] = { { .80f, .10f, .09f }, { .95f, .95f, .93f }, { .10f, .30f, .72f }, { .98f, .78f, .10f } };
    constexpr double panel = 2.4;
    constexpr float lateral = 3.35f, height = 1.05f;
    int n = 0;
    for (double d = from; d + panel <= to; d += panel, ++n) {
        if (std::abs(d + panel * .5 - finishArch) < 2.2)
            continue;   // the arch's legs
        for (float sign : { -1.f, 1.f }) {
            const QVector3D a0 = v(route.at(d)) + side(d) * (sign * lateral), b0 = v(route.at(d + panel - .15)) + side(d + panel - .15) * (sign * lateral);
            QVector3D a = a0, b = b0;
            a.setY(vergeHeight(d, sign * lateral));
            b.setY(vergeHeight(d + panel - .15, sign * lateral));
            const QVector3D inward = -side(d) * sign;   // facing the road
            // frame: two feet, two posts, top and bottom rails
            for (const QVector3D &p : { a, b }) {
                box(p + up * .02f, inward * .32f, up * .02f, (b - a).normalized() * .03f, steel * .8f);
                box(p + up * (height * .5f), inward * .02f, up * (height * .5f), (b - a).normalized() * .02f, steel);
            }
            limb(a + up * height, b + up * height, .02f, steel);
            limb(a + up * .12f, b + up * .12f, .015f, steel);
            // the banner on the road side
            const QVector3D col = banners[(n + (sign > 0 ? 2 : 0)) % 4];
            const QVector3D o = inward * .03f;
            face(a + up * .2f + o, b + up * .2f + o, b + up * (height - .06f) + o, a + up * (height - .06f) + o, inward, col);
            face(b + up * .2f - o, a + up * .2f - o, a + up * (height - .06f) - o, b + up * (height - .06f) - o, -inward, col * .6f);
        }
        // flags on poles behind the barriers, every third panel
        if (n % 3 == 1)
            for (float sign : { -1.f, 1.f }) {
                const float lat = lateral + 1.6f;
                QVector3D foot = v(route.at(d)) + side(d) * (sign * lat);
                foot.setY(vergeHeight(d, sign * lat));
                limb(foot, foot + up * 4.2f, .03f, QVector3D(.9f, .9f, .9f));
                const QVector3D f = (v(route.at(d + 1)) - v(route.at(d))).normalized();
                const QVector3D col = banners[(n / 3 + (sign > 0 ? 1 : 3)) % 4];
                const QVector3D top = foot + up * 4.15f;
                for (int k = 0; k < 2; ++k) {   // a hanging flag (both sides), waving
                    const QVector3D nn = (k ? -1.f : 1.f) * QVector3D::crossProduct(up, f).normalized();
                    const QVector3D p0 = top, p1 = top + f * .9f, p2 = top + f * .9f - up * 1.4f, p3 = top - up * 1.5f;
                    for (const QVector3D &p : { p0, p1, p2, p0, p2, p3 })
                        put(p, nn, col, Foliage, (p - top).length() > .5f ? .7f : .1f);
                }
            }
    }
}

// Kilometre posts: a short white post on the right-hand verge with a small
// plate showing the kilometre (digits from the banner texture).
void Route3DScene::kmPosts()
{
    const Route &route = *m_route;
    const double length = route.length();
    const QVector3D white(.92f, .92f, .89f), black(.10f, .10f, .10f), plateWhite(1, 1, 1);
    for (size_t i = 0; i < m_kmPosts.size(); ++i) {
        double d = m_kmPosts[i];
        // not into a sign or an arch
        std::vector<double> busy { m_builtStartGate };
        for (const SprintMarks &s : m_sprints)
            busy.insert(busy.end(), { s.start, s.finish });
        for (const KomMarks &k : m_koms)
            busy.insert(busy.end(), { k.start, k.finish });
        for (double b : busy)
            if (std::abs(d - b) < 4)
                d = b + 6;
        if (d < 1 || d > length - 1)
            continue;
        const float lat = kGravelEdgeM + .55f;
        QVector3D f = v(route.at(std::min(length, d + 3))) - v(route.at(std::max(0.0, d - 3)));
        f.setY(0);
        f = f.lengthSquared() > 1e-6f ? f.normalized() : QVector3D(0, 0, -1);
        const QVector3D right = side(d), up(0, 1, 0);
        QVector3D foot = v(route.at(d)) + right * lat;
        foot.setY(vergeHeight(d, lat));
        const QVector3D n = (-f * std::cos(qDegreesToRadians(20.f)) - right * std::sin(qDegreesToRadians(20.f))).normalized();
        const QVector3D r = QVector3D::crossProduct(up, n);
        box(foot + up * .45f, r * .07f, up * .70f, n * .07f, white);
        box(foot + up * 1.10f, r * .073f, up * .05f, n * .073f, black);
        // the plate: big digits (read from the saddle while riding past)
        const QString number = QString::number(int(i) + 1);
        const float dh = .40f, dw = dh * (kDigit(0).u1 - kDigit(0).u0) / (kDigit(0).v1 - kDigit(0).v0);
        const float pw = (number.size() * dw + .12f) * .5f, ph = dh * .5f + .05f;
        const QVector3D plate = foot + up * (1.15f + ph) + n * .07f;
        box(plate - n * .045f, r * (pw + .02f), up * (ph + .02f), n * .015f, black);   // dark rim and back (well behind the face: no depth fighting)
        const QVector3D front = plate + n * .001f;
        face(front - r * pw - up * ph, front + r * pw - up * ph, front + r * pw + up * ph, front - r * pw + up * ph, n, plateWhite);
        float x = -number.size() * dw * .5f;
        for (const QChar ch : number) {
            const TexRect t = kDigit(ch.digitValue());
            const QVector3D c0 = front + n * .025f + r * x - up * (dh * .5f), c1 = c0 + r * dw, c2 = c1 + up * dh, c3 = c0 + up * dh;
            const QVector3D tl(t.u0, t.v0, 0), tr(t.u1, t.v0, 0), bl(t.u0, t.v1, 0), br(t.u1, t.v1, 0);
            put(c0, n, bl, Banner, 0);
            put(c1, n, br, Banner, 0);
            put(c2, n, tr, Banner, 0);
            put(c0, n, bl, Banner, 0);
            put(c2, n, tr, Banner, 0);
            put(c3, n, tl, Banner, 0);
            x += dw;
        }
    }
}

void Route3DScene::setKoms(const std::vector<KomMarks> &marks)
{
    if (marks == m_koms)
        return;
    m_koms = marks;
    m_gatesDirty = true;   // rebuilt in scene() (reads the terrain: no tile job then)
}

void Route3DScene::setSprints(const std::vector<SprintMarks> &marks)
{
    if (marks == m_sprints)
        return;
    m_sprints = marks;
    m_gatesDirty = true;   // rebuilt in scene() (reads the terrain: no tile job then)
}

namespace {
constexpr float kRoadMarkLift = .07f;      // over the road centre line (asphalt +.025, paint +.045)
constexpr float kRoadTextLengthM = 6.f;    // road text: stretched along the road (read from the saddle)
constexpr float kSignLateralM = kGravelEdgeM + .9f;   // sprint signs on the right-hand verge
constexpr float kSignWidthM = 1.3f;
constexpr float kSignHeightM = kSignWidthM * (kSprintSign.v1 - kSprintSign.v0) / (kSprintSign.u1 - kSprintSign.u0);
constexpr float kSignBottomM = 1.15f;      // board's lower edge over the ground
constexpr float kSignTurnDeg = 20.f;       // turned towards the road
} // namespace

// The sprint on the road (GPX Ride): painted lines across the asphalt, road
// text before the first two and a small sign on the verge at the start and
// the finish line. All in the gate buffer (static, ~400 vertices).
void Route3DScene::sprintMarks(std::vector<Vertex> &out, const SprintMarks &sprint)
{
    if (!m_route)
        return;
    const SprintMarks &marks = sprint;
    const Route &route = *m_route;
    const float length = float(route.length());
    const QVector3D up(0, 1, 0);
    auto centre = [&](double d) {
        QVector3D c = v(route.at(std::clamp(d, 0.0, double(length))));
        c.setY(c.y() + kRoadMarkLift);
        return c;
    };
    auto quadOut = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n, QVector3D ca, QVector3D cb,
                       QVector3D cc, QVector3D cd, int material) {
        put(out, a, n, ca, material, 0);
        put(out, b, n, cb, material, 0);
        put(out, c, n, cc, material, 0);
        put(out, a, n, ca, material, 0);
        put(out, c, n, cc, material, 0);
        put(out, d, n, cd, material, 0);
    };
    // a painted line across the road, `width` metres along it
    auto line = [&](double d, float width, QVector3D color) {
        const double d0 = d - width * .5, d1 = d + width * .5;
        const QVector3D c0 = centre(d0), c1 = centre(d1), s0 = side(d0), s1 = side(d1);
        const float half = 3.f;   // the asphalt's width
        quadOut(c0 - s0 * half, c0 + s0 * half, c1 + s1 * half, c1 - s1 * half, up, color, color, color, color, Paint);
    };
    // road text from d0 (near: the letters' foot) to d1, following the road
    auto text = [&](double d0, double d1, float halfWidth, const TexRect &r) {
        constexpr int steps = 6;
        for (int k = 0; k < steps; ++k) {
            const double a = d0 + (d1 - d0) * k / steps, b = d0 + (d1 - d0) * (k + 1) / steps;
            const float va = r.v1 + (r.v0 - r.v1) * float(k) / steps, vb = r.v1 + (r.v0 - r.v1) * float(k + 1) / steps;
            const QVector3D ca = centre(a), cb = centre(b), sa = side(a), sb = side(b);
            quadOut(ca - sa * halfWidth, ca + sa * halfWidth, cb + sb * halfWidth, cb - sb * halfWidth, up,
                    QVector3D(r.u0, va, 0), QVector3D(r.u1, va, 0), QVector3D(r.u1, vb, 0), QVector3D(r.u0, vb, 0), Banner);
        }
    };
    // a sign on a post on the right-hand verge, facing the approaching rider
    auto sign = [&](double d, const TexRect &face, QVector3D color) {
        const QVector3D road = v(route.at(std::clamp(d, 0.0, double(length))));
        QVector3D f = v(route.at(std::min(double(length), d + 3))) - v(route.at(std::max(0.0, d - 3)));
        f.setY(0);
        f = f.lengthSquared() > 1e-6f ? f.normalized() : QVector3D(0, 0, -1);
        const QVector3D right(-f.z(), 0, f.x());
        const QVector3D outer = road + right * kVergeEdgeM;
        const float outerY = float(m_terrain.surface(outer.x(), outer.z())) + .03f;
        const float t = (kSignLateralM - kGravelEdgeM) / (kVergeEdgeM - kGravelEdgeM);
        QVector3D foot = road + right * kSignLateralM;
        foot.setY(road.y() + (outerY - road.y()) * t);
        const float turn = qDegreesToRadians(kSignTurnDeg);
        const QVector3D n = (-f * std::cos(turn) - right * std::sin(turn)).normalized();   // the face's normal
        const QVector3D r = QVector3D::crossProduct(up, n);                               // the face's right
        const QVector3D grey(.52f, .53f, .55f);
        // post (square, behind the board)
        const float pw = .035f, top = kSignBottomM + kSignHeightM - .04f;
        const QVector3D p0 = foot - up * .2f, p1 = foot + up * top;
        const QVector3D px = r * pw, pz = n * pw;
        const QVector3D corners[4] = { -px - pz, px - pz, px + pz, -px + pz };
        for (int k = 0; k < 4; ++k) {
            const QVector3D a = corners[k], b = corners[(k + 1) % 4];
            const QVector3D sn = (a + b).normalized();
            quadOut(p0 + a, p0 + b, p1 + b, p1 + a, sn, grey, grey, grey, grey, Solid);
        }
        // board: printed front, grey back, coloured edges
        const float hw = kSignWidthM * .5f, depth = .015f;
        const QVector3D mid = foot + up * (kSignBottomM + kSignHeightM * .5f) + n * (pw + depth);
        const QVector3D hx = r * hw, hy = up * (kSignHeightM * .5f), hz = n * depth;
        const QVector3D fTL = mid - hx + hy + hz, fTR = mid + hx + hy + hz, fBR = mid + hx - hy + hz, fBL = mid - hx - hy + hz;
        const QVector3D bTL = fTL - hz * 2, bTR = fTR - hz * 2, bBR = fBR - hz * 2, bBL = fBL - hz * 2;
        quadOut(fTL, fTR, fBR, fBL, n, QVector3D(face.u0, face.v0, 0), QVector3D(face.u1, face.v0, 0),
                QVector3D(face.u1, face.v1, 0), QVector3D(face.u0, face.v1, 0), Banner);
        quadOut(bTR, bTL, bBL, bBR, -n, grey, grey, grey, grey, Solid);
        quadOut(bTL, bTR, fTR, fTL, up, color, color, color, color, Solid);
        quadOut(fBL, fBR, bBR, bBL, -up, color, color, color, color, Solid);
        quadOut(bTL, fTL, fBL, bBL, -r, color, color, color, color, Solid);
        quadOut(fTR, bTR, bBR, fBR, r, color, color, color, color, Solid);
    };
    // painted lines a little brighter than the arches (paint on dark asphalt)
    const QVector3D green = kStartGreen, red = kFinishRed;
    const QVector3D paintGreen(.20f, .85f, .32f), paintRed(.95f, .18f, .14f);
    // "SPRINT IN 200 m", a thin green line
    if (marks.announce >= 0) {
        text(marks.announce - 1 - kRoadTextLengthM, marks.announce - 1, 2.6f, kRoadAnnounce);
        line(marks.announce, .45f, paintGreen);
    }
    // "SPRINT", the thick green start line and its sign
    text(marks.start - 1 - kRoadTextLengthM, marks.start - 1, 2.2f, kRoadSprint);
    line(marks.start, 1.4f, paintGreen);
    sign(marks.start, kSprintSign, green);
    // the red finish line and its sign
    line(marks.finish, 1.4f, paintRed);
    sign(marks.finish, kSprintFinishSign, red);
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
    QVector3D green = QVector3D(.20f + .10f * tone, .32f + .08f * tone, .11f + .04f * rnd(t.seed, 6)) * rgb(m_profile.leafTint);
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

void Route3DScene::treeMesh(const TreeSpec &t, bool far, std::vector<MeshVertex> &out)
{
    std::vector<Vertex> buf;
    std::vector<Vertex> *saved = m_target;
    m_target = &buf;
    tree(t, far);
    m_target = saved;
    out.reserve(out.size() + buf.size());
    for (const Vertex &v : buf)
        out.push_back({ QVector3D(v.x, v.y, v.z), QVector3D(v.nx, v.ny, v.nz), QVector3D(v.r, v.g, v.b),
                        int(v.material + .5f) == Foliage ? v.blend : 0.f });
}

void Route3DScene::bush(const TreeSpec &t)
{
    QVector3D base(float(t.x), float(t.y), float(t.z));
    float h = float(t.height);
    QVector3D green = QVector3D(.21f + .08f * rnd(t.seed, 7), .31f + .06f * rnd(t.seed, 8), .12f) * rgb(m_profile.leafTint);
    QVector3D c = base + QVector3D(0, h * .45f, 0);
    blob(c, c, float(t.radius), green, .75f, t.seed, 4, 7, base.y(), h * 2.5f);
}

void Route3DScene::tree(const TreeSpec &t, bool far)
{
    switch (t.kind) {
    case Spruce: spruce(t, far); break;
    case Broadleaf: broadleaf(t, far); break;
    case Palm: palm(t, far); break;
    case Cactus: cactus(t, far); break;
    case Eucalyptus: eucalyptus(t, far); break;
    case Acacia: acacia(t, far); break;
    default: bush(t); break;
    }
}

// A coconut palm: a slender, ringed trunk bending gently to one side and a
// crown of long, drooping fronds (each a strip with a raised spine), a dead
// brown one or two hanging below. The fronds sway in the wind.
void Route3DScene::palm(const TreeSpec &t, bool far)
{
    const QVector3D base(float(t.x), float(t.y), float(t.z));
    const float h = float(t.height);
    const float lean = rnd(t.seed, 1) * 6.283f, bend = h * (.06f + .12f * rnd(t.seed, 2));
    const QVector3D dir(std::cos(lean), 0, std::sin(lean));
    auto at = [&](float u) { return base + QVector3D(0, h * u, 0) + dir * (bend * u * u); };
    const QVector3D bark(.47f, .40f, .31f);
    // (far: about as many vertices as a far broadleaf)
    const int segs = far ? 2 : 6;
    for (int i = 0; i < segs; ++i) {
        const float u0 = float(i) / segs, u1 = float(i + 1) / segs;
        const float r0 = h * (.028f - .010f * u0), r1 = h * (.028f - .010f * u1);
        taper(at(u0), at(u1), r0, r1, far ? 3 : 6, bark * (.78f + .10f * (i % 2)), bark * (.92f + .10f * (i % 2)));
    }
    const QVector3D crown = at(1.f);
    const float L = float(t.radius) * 1.05f;
    const int fronds = far ? 5 : 11, steps = far ? 2 : 4;
    const float spin = rnd(t.seed, 3) * 6.283f;
    const QVector3D green(.19f + .05f * rnd(t.seed, 4), .37f + .06f * rnd(t.seed, 5), .13f);
    auto vert = [&](const QVector3D &p, const QVector3D &n, const QVector3D &c) {
        put(p, n, c, Foliage, std::clamp((p.y() - base.y()) / h, 0.f, 1.3f));
    };
    for (int f = 0; f < fronds; ++f) {
        const bool dead = !far && f >= fronds - 2 && rnd(t.seed, 10 + f) < .6f;
        const float a = spin + f * 6.283f / fronds + (rnd(t.seed, 30 + f) - .5f) * .4f;
        const QVector3D out(std::cos(a), 0, std::sin(a)), side(-out.z(), 0, out.x());
        // rise, then droop (dead fronds hang down along the trunk)
        const float rise = dead ? -.6f : .25f + .35f * rnd(t.seed, 50 + f), droop = dead ? .6f : .85f + .4f * rnd(t.seed, 70 + f);
        const float len = L * (dead ? .7f : .85f + .3f * rnd(t.seed, 90 + f));
        const QVector3D col = dead ? QVector3D(.45f, .36f, .20f) : green * (.85f + .3f * rnd(t.seed, 110 + f));
        auto spine = [&](float s) {
            const float d = len * s;
            return crown + out * d + QVector3D(0, d * rise - d * d / len * droop, 0);
        };
        for (int k = 0; k < steps; ++k) {
            const float s0 = float(k) / steps, s1 = float(k + 1) / steps;
            const QVector3D p0 = spine(s0), p1 = spine(s1);
            // leaflets: widest in the middle, the edges hanging lower than the spine (a V)
            const float w0 = len * .17f * std::sin(3.1416f * std::max(s0, .08f)), w1 = len * .17f * std::sin(3.1416f * s1);
            for (float sd : { -1.f, 1.f }) {
                const QVector3D e0 = p0 + side * (sd * w0) - QVector3D(0, w0 * .45f, 0);
                const QVector3D e1 = p1 + side * (sd * w1) - QVector3D(0, w1 * .45f, 0);
                QVector3D n = QVector3D::crossProduct(p1 - p0, e0 - p0).normalized();
                if (n.y() < 0)
                    n = -n;
                n = (n + QVector3D(0, .6f, 0)).normalized();
                vert(p0, n, col * 1.08f);
                vert(p1, n, col * 1.08f);
                vert(e1, n, col * .82f);
                vert(p0, n, col * 1.08f);
                vert(e1, n, col * .82f);
                vert(e0, n, col * .82f);
            }
        }
    }
    if (!far)   // coconuts under the crown
        for (int k = 0; k < 3; ++k) {
            const float a = spin + k * 2.1f;
            const QVector3D c = crown + QVector3D(std::cos(a) * h * .03f, -h * .025f, std::sin(a) * h * .03f);
            const float r = .13f;
            ellipsoid(c, QVector3D(r, 0, 0), QVector3D(0, r * 1.1f, 0), QVector3D(0, 0, r), 3, 5,
                      [](QVector3D) { return QVector3D(.30f, .24f, .12f); });
        }
}

// A saguaro: a ribbed green column with a rounded top and, when tall enough,
// arms that bend upwards. Small ones are a single stem.
void Route3DScene::cactus(const TreeSpec &t, bool far)
{
    const QVector3D base(float(t.x), float(t.y), float(t.z));
    const float h = float(t.height), r = std::clamp(h * .065f, .14f, .40f);
    const QVector3D green = QVector3D(.27f + .05f * rnd(t.seed, 1), .38f + .05f * rnd(t.seed, 2), .22f);
    const int sides = far ? 5 : 9;
    auto cap = [&](const QVector3D &top, float radius) {
        if (far)
            return;
        ellipsoid(top, QVector3D(radius, 0, 0), QVector3D(0, radius * .7f, 0), QVector3D(0, 0, radius), 3, sides,
                  [&](QVector3D d) { return green * (.95f + .1f * d.y()); });
    };
    const QVector3D top = base + QVector3D(0, h, 0);
    taper(base, top, r, r * .92f, sides, green * .75f, green);
    cap(top, r * .92f);
    if (h < 3.2f)
        return;
    const int arms = 1 + int(rnd(t.seed, 3) * (h > 5 ? 3.f : 2.f));
    const float spin = rnd(t.seed, 4) * 6.283f;
    for (int k = 0; k < arms; ++k) {
        const float a = spin + k * 2.4f + (rnd(t.seed, 10 + k) - .5f) * .8f;
        const QVector3D out(std::cos(a), 0, std::sin(a));
        const float ra = r * .72f;
        const QVector3D p0 = base + QVector3D(0, h * (.35f + .25f * rnd(t.seed, 20 + k)), 0);
        const QVector3D p1 = p0 + out * (r * 2.6f) + QVector3D(0, ra * 1.2f, 0);
        const QVector3D p2 = p1 + QVector3D(0, std::min(h * (.22f + .25f * rnd(t.seed, 30 + k)), base.y() + h - p1.y() - .2f), 0);
        taper(p0, p1, ra, ra, sides, green * .8f, green * .9f);
        if (!far)
            ellipsoid(p1, QVector3D(ra, 0, 0), QVector3D(0, ra, 0), QVector3D(0, 0, ra), 3, sides,
                      [&](QVector3D) { return green * .88f; });
        if (p2.y() > p1.y() + .1f) {
            taper(p1, p2, ra, ra * .92f, sides, green * .85f, green);
            cap(p2, ra * .92f);
        }
    }
}

// A gum tree: a tall, pale, smooth trunk forking into a few limbs that carry
// loose, grey-green clumps of leaves (an open crown: you see the sky through it).
void Route3DScene::eucalyptus(const TreeSpec &t, bool far)
{
    const QVector3D base(float(t.x), float(t.y), float(t.z));
    const float h = float(t.height), R = float(t.radius);
    const QVector3D bark = rnd(t.seed, 1) < .5f ? QVector3D(.80f, .77f, .70f) : QVector3D(.70f, .62f, .52f);
    const QVector3D leaves = QVector3D(.34f + .05f * rnd(t.seed, 2), .42f + .05f * rnd(t.seed, 3), .30f) * rgb(m_profile.leafTint);
    const float lean = rnd(t.seed, 4) * 6.283f;
    const QVector3D fork = base + QVector3D(std::cos(lean) * h * .05f, h * .48f, std::sin(lean) * h * .05f);
    taper(base, fork, h * .035f, h * .024f, far ? 4 : 7, bark * .8f, bark);
    const int limbs = far ? 2 : 3 + int(rnd(t.seed, 5) * 2);
    const float spin = rnd(t.seed, 6) * 6.283f;
    for (int k = 0; k < limbs; ++k) {
        const float a = spin + k * 6.283f / limbs + (rnd(t.seed, 10 + k) - .5f) * .7f;
        const QVector3D out(std::cos(a), 0, std::sin(a));
        const QVector3D end = fork + out * (R * (.45f + .35f * rnd(t.seed, 20 + k))) + QVector3D(0, h * (.25f + .2f * rnd(t.seed, 30 + k)), 0);
        taper(fork, end, h * .016f, h * .008f, far ? 3 : 5, bark, bark * 1.05f);   // (far too: no floating clumps)
        const float r = R * (.38f + .14f * rnd(t.seed, 40 + k));
        blob(end + QVector3D(0, r * .2f, 0), end, r, leaves * (.9f + .2f * rnd(t.seed, 50 + k)), .62f, t.seed + uint32_t(k) * 31u,
             far ? 3 : 4, far ? 5 : 7, base.y(), h);
    }
    if (!far)   // a clump at the top
        blob(base + QVector3D(0, h * .9f, 0), fork, R * .4f, leaves, .6f, t.seed + 997u, 4, 7, base.y(), h);
}

// An umbrella thorn: a short trunk splitting into a few slanting limbs under
// a wide, flat crown.
void Route3DScene::acacia(const TreeSpec &t, bool far)
{
    const QVector3D base(float(t.x), float(t.y), float(t.z));
    const float h = float(t.height), R = float(t.radius);
    const QVector3D bark(.33f, .27f, .21f);
    const QVector3D leaves = QVector3D(.30f + .05f * rnd(t.seed, 1), .37f + .05f * rnd(t.seed, 2), .17f) * rgb(m_profile.leafTint);
    const QVector3D fork = base + QVector3D(0, h * .38f, 0);
    taper(base, fork, h * .04f, h * .03f, far ? 4 : 6, bark * .8f, bark);
    const QVector3D crown = base + QVector3D(0, h * .82f, 0);
    // the limbs up to the crown (far too, simpler: else the crown floats over a stump)
    const float spin = rnd(t.seed, 3) * 6.283f;
    for (int k = 0; k < 3; ++k) {
        const float a = spin + k * 2.09f;
        const QVector3D end = crown + QVector3D(std::cos(a) * R * .45f, -h * .05f, std::sin(a) * R * .45f);
        taper(fork, end, h * .022f, h * .012f, far ? 3 : 5, bark, bark);
    }
    if (far) {
        blob(crown, crown, R, leaves, .26f, t.seed, 3, 7, base.y(), h);
        return;
    }
    blob(crown, crown, R * .85f, leaves, .26f, t.seed, 4, 9, base.y(), h);
    for (int k = 0; k < 2; ++k) {   // a lumpy, layered top
        const float a = spin + 1.f + k * 3.1f;
        const QVector3D c = crown + QVector3D(std::cos(a) * R * .4f, h * .04f, std::sin(a) * R * .4f);
        blob(c, crown, R * .55f, leaves * (.92f + .16f * rnd(t.seed, 10 + k)), .3f, t.seed + uint32_t(k) + 7u, 4, 8, base.y(), h);
    }
}

// A clump of wild flowers: each a thin stem and a small cup-shaped head
// (5 petals around a darker heart), swaying gently with the trees.
void Route3DScene::flowers(const FlowerClump &f)
{
    static const QVector3D petals[Flowers::colors] = {
        { .86f, .10f, .08f },   // poppy red
        { .98f, .82f, .12f },   // buttercup yellow
        { .96f, .96f, .92f },   // daisy white
        { .30f, .42f, .92f },   // cornflower blue
        { .93f, .45f, .70f },   // pink
        { .98f, .55f, .12f },   // orange
        { .62f, .32f, .82f },   // violet
    };
    static const QVector3D hearts[Flowers::colors] = {
        { .08f, .06f, .05f }, { .85f, .60f, .08f }, { .95f, .75f, .10f }, { .20f, .24f, .55f },
        { .95f, .85f, .30f }, { .55f, .25f, .05f }, { .95f, .85f, .30f },
    };
    const QVector3D stemGreen(.20f, .38f, .12f);
    const float size = float(f.size);
    for (int k = 0; k < f.count; ++k) {
        const float a = rnd(f.seed, k * 7) * 6.283f;
        const float d = (k == 0 ? 0.f : .12f + .38f * rnd(f.seed, k * 7 + 1)) * size;
        const QVector3D foot(float(f.x) + std::cos(a) * d, float(f.y) - .05f, float(f.z) + std::sin(a) * d);
        const float h = (.20f + .22f * rnd(f.seed, k * 7 + 2)) * size;
        const float r = (.055f + .035f * rnd(f.seed, k * 7 + 3)) * size;
        const float lean = rnd(f.seed, k * 7 + 4) * 6.283f;
        const QVector3D head = foot + QVector3D(std::cos(lean) * h * .12f, h, std::sin(lean) * h * .12f);
        const float wh = std::min(1.f, h / 1.2f);   // sway weight at the head
        // stem: one thin triangle, facing a random way
        const QVector3D across(std::cos(lean + 1.57f) * .012f, 0, std::sin(lean + 1.57f) * .012f);
        const QVector3D sn = QVector3D(std::cos(lean), .3f, std::sin(lean)).normalized();
        put(foot - across, sn, stemGreen * .8f, Foliage, 0);
        put(foot + across, sn, stemGreen * .8f, Foliage, 0);
        put(head, sn, stemGreen, Foliage, wh);
        // head
        const QVector3D petal = petals[size_t(f.color % Flowers::colors)] * (.92f + .16f * rnd(f.seed, k * 7 + 5));
        const QVector3D heart = hearts[size_t(f.color % Flowers::colors)];
        // the head faces up and a little sideways (seen from the road, not edge-on)
        const float face = rnd(f.seed, k * 7 + 6) * 6.283f;
        const QVector3D up = QVector3D(std::cos(face) * .85f, 1.f, std::sin(face) * .85f).normalized();
        const QVector3D u = QVector3D::crossProduct(up, QVector3D(0, 0, 1)).normalized();
        const QVector3D w = QVector3D::crossProduct(up, u);
        const QVector3D centre = head - up * (r * .25f);   // a shallow cup
        auto rim = [&](int i) {
            const float t = face + float(i) * 6.283185f / 5.f;
            return head + (u * std::cos(t) + w * std::sin(t)) * r;
        };
        for (int i = 0; i < 5; ++i) {
            const QVector3D p = rim(i), q = rim(i + 1);
            put(centre, up, heart, Foliage, wh);
            put(p, (up * 1.5f + (p - head) / r).normalized(), petal, Foliage, wh);
            put(q, (up * 1.5f + (q - head) / r).normalized(), petal, Foliage, wh);
        }
    }
}

// A weathered stone: a noise-displaced, squashed ellipsoid with flat-shaded
// facets, grey with a warm or cool cast and some moss on the top faces.
void Route3DScene::rock(const RockSpec &s, bool far)
{
    const int rings = far ? 4 : 5, sides = far ? 6 : 8;
    const float size = float(s.size);
    const float rx = size * (.75f + .45f * rnd(s.seed, 1)), ry = size * (.60f + .30f * rnd(s.seed, 2)),
                rz = size * (.70f + .40f * rnd(s.seed, 3));
    const float spin = rnd(s.seed, 4) * 6.283f;
    const float cs = std::cos(spin), sn = std::sin(spin);
    const QVector3D centre(float(s.x), float(s.y) + ry * .45f, float(s.z));
    const float tone = .38f + .16f * rnd(s.seed, 5);
    QVector3D grey = rnd(s.seed, 6) < .5f ? QVector3D(tone * 1.05f, tone, tone * .92f)    // warm granite
                                           : QVector3D(tone * .95f, tone, tone * 1.04f);  // cool
    // sandstone and red rock where the ground is sand (deserts, the outback); no moss there or in snow
    const bool dry = m_profile.sandCover > .2f;
    if (dry)
        grey *= QVector3D(1, 1, 1) + (rgb(m_profile.sandTint) - QVector3D(1, 1, 1)) * .8f + QVector3D(.18f, .12f, .05f);
    const float moss = dry || m_profile.snowCover > 0 ? 0.f : rnd(s.seed, 7) * .6f;
    const QVector3D mossGreen(.27f, .34f, .16f);
    std::vector<QVector3D> pts;
    pts.reserve(size_t((rings + 1) * sides));
    for (int i = 0; i <= rings; ++i)
        for (int k = 0; k < sides; ++k) {
            const double a = 3.141592654 * i / rings, t = 6.283185307 * k / sides + (i % 2) * 3.14159 / sides;
            QVector3D d(float(std::sin(a) * std::cos(t)), float(std::cos(a)), float(std::sin(a) * std::sin(t)));
            const float n = float(.78 + .42 * WorldNoise::value(d.x() * 1.7 + s.seed % 89, d.z() * 1.7 + d.y() * 1.3, s.seed));
            QVector3D p(d.x() * rx * n, d.y() * ry * n, d.z() * rz * n);
            if (i == 0)
                p.setY(p.y() * .7f);   // flattish top
            pts.push_back(centre + QVector3D(p.x() * cs - p.z() * sn, p.y(), p.x() * sn + p.z() * cs));
        }
    auto P = [&](int i, int k) { return pts[size_t(i * sides + (k % sides))]; };
    int facet = 0;
    auto face3 = [&](QVector3D a, QVector3D b, QVector3D c) {
        QVector3D n = QVector3D::crossProduct(b - a, c - a);
        if (n.lengthSquared() < 1e-10f)
            return;
        n.normalize();
        if (QVector3D::dotProduct(n, (a + b + c) / 3.f - centre) < 0)
            n = -n;
        QVector3D col = grey * (.88f + .24f * rnd(s.seed, 20 + facet++));
        const float m = moss * std::clamp((n.y() - .55f) * 2.5f, 0.f, 1.f);
        col = col * (1 - m) + mossGreen * m;
        put(a, n, col, Solid, 0);
        put(b, n, col, Solid, 0);
        put(c, n, col, Solid, 0);
    };
    for (int i = 0; i < rings; ++i)
        for (int k = 0; k < sides; ++k) {
            if (i > 0)
                face3(P(i, k), P(i, k + 1), P(i + 1, k + 1));
            if (i < rings - 1)
                face3(P(i, k), P(i + 1, k + 1), P(i + 1, k));
        }
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
    // the biome's palettes (Nordic: Danish whitewash, ochre, red and tarred walls)
    float tint = .92f + .16f * rnd(b.seed, 1);
    QVector3D wall = rgb(m_profile.walls[b.walls & 3]) * tint,
              roof = rgb(m_profile.roofs[std::clamp(b.roof, 0, 4)]) * (.9f + .2f * rnd(b.seed, 2)),
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
        QVector3D p = gableEnd ? P(s * (L + .06f), a, y) : P(a, s * (W + .06f), y);
        auto q = [&](float dx, float dy, float out) { return p + along * dx + QVector3D(0, dy, 0) + n * out; };
        face(q(-w * .5f - .07f, -.07f, 0), q(w * .5f + .07f, -.07f, 0), q(w * .5f + .07f, h + .07f, 0),
             q(-w * .5f - .07f, h + .07f, 0), n, frame);
        face(q(-w * .5f, 0, .05f), q(w * .5f, 0, .05f), q(w * .5f, h, .05f), q(-w * .5f, h, .05f), n, glass);
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
                    return P(a, W + .06f, fl) + u * dx + QVector3D(0, dy, 0) + n * out;
                };
                face(q(-.6f, 0, 0), q(.6f, 0, 0), q(.6f, 2.2f, 0), q(-.6f, 2.2f, 0), n, frame);
                face(q(-.48f, 0, .05f), q(.48f, 0, .05f), q(.48f, 2.08f, .05f), q(-.48f, 2.08f, .05f), n, doorColor);
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
    const float k = m_profile.ringScale;
    // snow caps (the icy north, the mountains): the upper part of the far ridge turns white
    const QVector3D snowCap(.86f, .89f, .94f);
    for (auto L : { Layer { 1750, 110 * k, 170 * k, .46f, rgb(m_profile.ringFar), 0.f },
                    Layer { 1150, 55 * k, 90 * k, .66f, rgb(m_profile.ringNear), 1.7f } }) {
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
            // the top's colour: snow on the higher crests of the far ridge
            auto crest = [&](const QVector3D &p) {
                if (!m_profile.ringSnow || L.phase != 0.f)
                    return L.color;
                const float s = std::clamp((p.y() - L.height) / (L.amp * .35f), 0.f, 1.f);
                return L.color * (1 - s) + snowCap * s;
            };
            // Skirt below the horizon (pure haze), then the hill face.
            for (auto [y0, y1, e0, e1] : { std::array<float, 4> { -140, -6, 0, 0 }, std::array<float, 4> { -6, 0, 0, 1 } }) {
                auto a0 = at(a, y0), b0 = at(b, y0), a1 = y1 == 0 ? a : at(a, y1), b1 = y1 == 0 ? b : at(b, y1);
                const QVector3D ca = y1 == 0 ? crest(a) : L.color, cb = y1 == 0 ? crest(b) : L.color;
                put(m_ring, a0, na, L.color, Distant, e0 * L.vis);
                put(m_ring, b0, nb, L.color, Distant, e0 * L.vis);
                put(m_ring, b1, nb, cb, Distant, e1 * L.vis);
                put(m_ring, a0, na, L.color, Distant, e0 * L.vis);
                put(m_ring, b1, nb, cb, Distant, e1 * L.vis);
                put(m_ring, a1, na, ca, Distant, e1 * L.vis);
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
    m_perches.swap(m_buildPerches);
    std::swap(m_birdGround, m_buildBirdGround);
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
    if (!m_tileJob.valid()) {   // nothing reads the terrain / the bike's spot meanwhile
        if (m_startDirty) {
            m_startDirty = false;
            const DisplaySpot old = m_bikeSpot;
            placeDisplayBike();
            // rebuild the tile when its trees / rocks / flowers may change (the old or new spot is in reach)
            const QVector3D tile(float(m_meshX * 32.0 + 16), 0, float(m_meshZ * 32.0 + 16));
            auto near = [&](const DisplaySpot &b) {
                return b.valid && std::hypot(b.pos.x() - tile.x(), b.pos.z() - tile.z()) < 420.f;
            };
            if (m_meshX != INT_MAX && (near(old) || near(m_bikeSpot)))
                m_meshX = m_meshZ = INT_MAX;
            m_gatesDirty = true;
        }
        if (m_gatesDirty)
            buildGates();
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
    std::vector<float> water(n * n), sand(n * n);   // water level (NaN: none), sand 0..1
    auto h = [&](int ix, int iz) { return float(m_terrain.node(ix, iz)); };
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            int ix = nx + i, iz = nz + j;
            float x = float(ix * Terrain::spacing), z = float(iz * Terrain::spacing);
            const auto data = m_terrain.nodeData(ix, iz);
            water[i * n + j] = float(data.water);
            sand[i * n + j] = data.sand;
            pos[i * n + j] = QVector3D(x, float(data.height), z);
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
    // the birds' ground: per 4 x 4 nodes (32 m) the highest node around
    {
        constexpr int kBlock = 4, m = (n - 1) / kBlock;
        BirdGround &g = m_buildBirdGround;
        g.step = float(kBlock * Terrain::spacing);
        g.n = m;
        g.x0 = float(nx + kBlock / 2) / kBlock;
        g.z0 = float(nz + kBlock / 2) / kBlock;
        g.h.assign(size_t(m * m), -1e9f);
        for (int bi = 0; bi < m; ++bi)
            for (int bj = 0; bj < m; ++bj) {
                float top = -1e9f;
                for (int i = bi * kBlock; i <= std::min(n - 1, bi * kBlock + kBlock); ++i)
                    for (int j = bj * kBlock; j <= std::min(n - 1, bj * kBlock + kBlock); ++j)
                        top = std::max(top, pos[i * n + j].y());
                g.h[size_t(bi * m + bj)] = top;
            }
    }
    m_build.reserve(size_t(n * n * 6 + 400000));
    for (int i = 0; i + 1 < n; i++)
        for (int j = 0; j + 1 < n; j++) {
            // Same diagonal as Terrain::surface(), so objects sit on the drawn surface.
            int a = i * n + j, b = (i + 1) * n + j, c = (i + 1) * n + j + 1, d = i * n + j + 1;
            for (int k : { a, b, c, a, c, d })
                put(pos[k], nor[k], col[k], Grass, sand[k]);
        }
    // Water: a flat sheet over every cell with a corner under a lake or the sea;
    // the banks above the water level hide its edges (depth test).
    for (int i = 0; i + 1 < n; i++)
        for (int j = 0; j + 1 < n; j++) {
            const int corners[4] = { i * n + j, (i + 1) * n + j, (i + 1) * n + j + 1, i * n + j + 1 };
            float level = std::numeric_limits<float>::quiet_NaN();
            for (int k : corners)
                if (std::isfinite(water[k]) && pos[k].y() < water[k])
                    level = std::isfinite(level) ? std::max(level, water[k]) : water[k];
            if (!std::isfinite(level))
                continue;
            auto at = [&](int k) {
                QVector3D p = pos[k];
                p.setY(std::isfinite(water[k]) ? water[k] : level);
                return p;
            };
            for (int k : { corners[0], corners[1], corners[2], corners[0], corners[2], corners[3] })
                put(at(k), QVector3D(0, 1, 0), QVector3D(1, 1, 1), Water, at(k).y() - pos[k].y());
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
    // (left, right) cross-section scale at d: the inner side of a bend
    // tighter than the verge's width is narrowed to ~0.8 of its radius
    auto innerSqueeze = [&](double at) {
        const QVector3D p0 = v(route.at(at - 3)), p1 = v(route.at(at)), p2 = v(route.at(at + 3));
        QVector3D t1 = p1 - p0, t2 = p2 - p1;
        t1.setY(0);
        t2.setY(0);
        const float l1 = t1.length(), l2 = t2.length();
        if (l1 < 1e-3f || l2 < 1e-3f)
            return QVector2D(1, 1);
        t1 /= l1;
        t2 /= l2;
        const float cross = t1.x() * t2.z() - t1.z() * t2.x();
        const float angle = std::atan2(std::abs(cross), QVector3D::dotProduct(t1, t2));
        if (angle < 1e-4f)
            return QVector2D(1, 1);
        const float radius = (l1 + l2) * .5f / angle;
        const float f = std::clamp(.8f * radius / kVergeEdgeM, .35f, 1.f);
        // side() = the direction rotated +90 degrees: turning that way (cross > 0) = the inside is right
        return cross > 0 ? QVector2D(1, f) : QVector2D(f, 1);
    };
    // the run-out's taper: 1 (full width) .. 0 at the drawn road's very end
    const bool runOut = route.finishDistance >= 0 && route.length() > route.finish() + kTaperM;
    auto runOutTaper = [&](double at) {
        if (!runOut)
            return 1.f;
        const double u = std::clamp((route.length() - at) / kTaperM, 0.0, 1.0);
        return float(u * u * (3 - 2 * u));
    };
    // Where the route meets itself (a loop closing, a junction with a part
    // ridden before or after), a road's gravel shoulder, verge and edge line
    // would lie across the other road's asphalt: on that side it becomes
    // asphalt instead (the two roads join). "Other": more than kJoinApartM
    // along the route; road points drawn by an earlier pass (shared) are left
    // out - that pass is the road.
    constexpr double kJoinApartM = 30.0, kJoinCellM = 8.0;
    std::unordered_map<long long, std::vector<uint32_t>> joinGrid;   // segment j..j+1 by its ends' cells
    auto joinKey = [](long long x, long long z) { return (x << 32) ^ (z & 0xffffffffLL); };
    for (const uint32_t j : nearby) {
        if (j + 1 >= route.points.size() || (route.isShared(j) && route.isShared(j + 1)))
            continue;
        const auto &pa = route.points[j], &pb = route.points[j + 1];
        const long long ax = (long long)std::floor(pa.x / kJoinCellM), az = (long long)std::floor(pa.z / kJoinCellM);
        const long long bx = (long long)std::floor(pb.x / kJoinCellM), bz = (long long)std::floor(pb.z / kJoinCellM);
        joinGrid[joinKey(ax, az)].push_back(j);
        if (bx != ax || bz != az)
            joinGrid[joinKey(bx, bz)].push_back(j);
    }
    // the nearest other part of the road (not within kJoinApartM of `along`)
    // to (x, z), at most `reach` away: its distance and the point on its centre line
    struct Other { double dist, x, z; };
    // (another road more than Route::kSnapRiseM higher or lower - a hairpin
    // on a climb, a bridge - is not joined)
    auto otherRoad = [&](double x, double y, double z, double along, double reach) {
        Other best { reach, x, z };
        const long long cx0 = (long long)std::floor((x - reach) / kJoinCellM), cx1 = (long long)std::floor((x + reach) / kJoinCellM);
        const long long cz0 = (long long)std::floor((z - reach) / kJoinCellM), cz1 = (long long)std::floor((z + reach) / kJoinCellM);
        for (long long gx = cx0 - 1; gx <= cx1 + 1; ++gx)
            for (long long gz = cz0 - 1; gz <= cz1 + 1; ++gz) {
                auto it = joinGrid.find(joinKey(gx, gz));
                if (it == joinGrid.end())
                    continue;
                for (const uint32_t j : it->second) {
                    const auto &pa = route.points[j], &pb = route.points[j + 1];
                    if (std::abs(pa.distance - along) < kJoinApartM || std::abs(pb.distance - along) < kJoinApartM)
                        continue;
                    const double sx = pb.x - pa.x, sz = pb.z - pa.z, l2 = sx * sx + sz * sz;
                    const double t = l2 > 1e-12 ? std::clamp(((x - pa.x) * sx + (z - pa.z) * sz) / l2, 0.0, 1.0) : 0.0;
                    const double nx = pa.x + sx * t, nz = pa.z + sz * t, dd = std::hypot(x - nx, z - nz);
                    if (std::abs(pa.y + (pb.y - pa.y) * t - y) > route3d::Route::kSnapRiseM)
                        continue;
                    if (dd < best.dist)
                        best = { dd, nx, nz };
                }
            }
        return best;
    };
    // Rounded corners where roads join: on a side facing another part of
    // the road, the asphalt widens along a quarter circle of radius
    // kFilletM tangent to both road edges (half width kRoadHalfM). q: the
    // distance from this road's edge to the other road's centre line.
    constexpr float kRoadHalfM = 3.0f, kFilletM = 6.0f;
    auto fillet = [&](double q) -> float {
        if (q <= kRoadHalfM)
            return 0.f;   // on the other road itself: its asphalt is there (no square patch)
        if (q >= kRoadHalfM + kFilletM)
            return 0.f;
        const double u = kRoadHalfM + kFilletM - q;
        return float(kFilletM - std::sqrt(std::max(0.0, double(kFilletM) * kFilletM - u * u)));
    };
    // the widening of side `sign` (-1 left, +1 right) at centre point c (route distance along)
    auto widening = [&](QVector3D c, QVector3D sd, float sign, double along, double *q) {
        const QVector3D edge = c + sd * (sign * kRoadHalfM);
        const Other o = otherRoad(edge.x(), c.y(), edge.z(), along, kRoadHalfM + kFilletM + 1);
        *q = o.dist;
        // -1: no other road at this side (it must reach out on this side, not
        // just end at the centre); else the widening (0 on the other road itself)
        if (o.dist >= kRoadHalfM + kFilletM)
            return -1.f;
        const double lateral = ((o.x - c.x()) * sd.x() + (o.z - c.z()) * sd.z()) * sign;
        return lateral > 1.5 || o.dist <= kRoadHalfM ? fillet(o.dist) : -1.f;
    };
    for (const uint32_t idx : nearby) {
        const size_t i = idx;
        auto p = route.points[i];
        if (std::abs(p.x - tileX * 32) > 400 || std::abs(p.z - tileZ * 32) > 400)
            continue;
        // ridden earlier on the route (out-and-back, loops): that pass draws it
        if (route.isShared(i) && route.isShared(i + 1))
            continue;
        double d = p.distance, e = route.points[i + 1].distance;
        auto a = v(p), b = v(route.points[i + 1]), sa = side(d), sb = side(e);
        // the run-out after the finish (not ridden): the road narrows to
        // nothing over its last kTaperM, the lines stop where it starts to
        const float ta = runOutTaper(d), tb = runOutTaper(e);
        const bool lined = ta >= 1 && tb >= 1;
        sa *= ta;
        sb *= tb;
        // on the inside of a tight bend (a hairpin) the cross section is
        // narrowed, so its inner edge does not fold over and cut up through
        // the asphalt of the road just before / after
        const QVector2D fa = innerSqueeze(d), fb = innerSqueeze(e);   // (left, right) scale
        const QVector3D saL = sa * fa.x(), saR = sa * fa.y(), sbL = sb * fb.x(), sbR = sb * fb.y();
        auto leftA = a - saL * kGravelEdgeM, leftB = b - sbL * kGravelEdgeM, rightA = a + saR * kGravelEdgeM,
             rightB = b + sbR * kGravelEdgeM;
        auto below = [&](QVector3D q) {
            q.setY(float(m_terrain.surface(q.x(), q.z())));
            return q;
        };
        // A sloping gravel-to-grass verge rather than a vertical painted wall.
        auto outerLA = below(a - saL * kVergeEdgeM), outerLB = below(b - sbL * kVergeEdgeM);
        auto outerRA = below(a + saR * kVergeEdgeM), outerRB = below(b + sbR * kVergeEdgeM);
        outerLA.setY(outerLA.y() + .03f);
        outerLB.setY(outerLB.y() + .03f);
        outerRA.setY(outerRA.y() + .03f);
        outerRB.setY(outerRB.y() + .03f);
        // a side joining another part of the road: rounded corner of asphalt,
        // no gravel shoulder / edge line there (the verge follows the curve)
        bool join[2] = { false, false };   // left, right
        float wide[2][2] = { { 0, 0 }, { 0, 0 } };   // [side][end]
        double qEdge[2][2] = { { 99, 99 }, { 99, 99 } };
        for (int k = 0; k < 2; ++k) {
            const float sign = k ? 1.f : -1.f;
            wide[k][0] = widening(a, sa, sign, d, &qEdge[k][0]);
            wide[k][1] = widening(b, sb, sign, e, &qEdge[k][1]);
            join[k] = wide[k][0] >= 0 || wide[k][1] >= 0;
            wide[k][0] = std::max(0.f, wide[k][0]);
            wide[k][1] = std::max(0.f, wide[k][1]);
        }
        if (!join[0])
            quad(leftA, leftB, outerLB, outerLA, QVector3D(1, 1, 1), Verge, 0, 0, 1, 1);
        if (!join[1])
            quad(rightA, rightB, outerRB, outerRA, QVector3D(1, 1, 1), Verge, 0, 0, 1, 1);
        // the gravel shoulders: strips from the asphalt's edge out (not one
        // sheet under the whole road - on a steep bend the sheet of the
        // segment a few metres on lies higher and cut up through the asphalt)
        if (!join[0])
            quad(leftA, leftB, b - sbL * 2.95f, a - saL * 2.95f, QVector3D(1, 1, 1), Gravel);
        if (!join[1])
            quad(a + saR * 2.95f, b + sbR * 2.95f, rightB, rightA, QVector3D(1, 1, 1), Gravel);
        a.setY(a.y() + .025f);
        b.setY(b.y() + .025f);
        // blend carries the lateral position (-1..1) for wheel tracks and edge wear.
        quad(a - saL * 3, b - sbL * 3, b + sbR * 3, a + saR * 3, QVector3D(1, 1, 1), Asphalt, -1, -1, 1, 1);
        // joining side: the rounded corner, in a few steps along the segment
        for (int k = 0; k < 2; ++k) {
            if (!join[k])
                continue;
            const float sign = k ? 1.f : -1.f;
            constexpr int steps = 8;
            for (int t = 0; t < steps; ++t) {
                const float f0 = float(t) / steps, f1 = float(t + 1) / steps;
                const double d0 = d + (e - d) * f0, d1 = d + (e - d) * f1;
                const QVector3D c0 = a + (b - a) * f0, c1 = a + (b - a) * f1;
                const QVector3D s0 = side(d0), s1 = side(d1);
                double q0 = 99, q1 = 99;
                const float w0 = t == 0 ? wide[k][0] : std::max(0.f, widening(c0, s0, sign, d0, &q0));
                const float w1 = t == steps - 1 ? wide[k][1] : std::max(0.f, widening(c1, s1, sign, d1, &q1));
                if (t == 0) q0 = qEdge[k][0];
                if (t == steps - 1) q1 = qEdge[k][1];
                if (w0 <= .01f && w1 <= .01f) {
                    // the curve has faded out here (or the other road is right
                    // here): the plain shoulder and verge, unless on the other road
                    if (q0 > kRoadHalfM + .6 && q1 > kRoadHalfM + .6) {
                        const QVector3D in0 = c0 + s0 * (sign * kGravelEdgeM), in1 = c1 + s1 * (sign * kGravelEdgeM);
                        QVector3D g0 = below(c0 + s0 * (sign * kVergeEdgeM)), g1 = below(c1 + s1 * (sign * kVergeEdgeM));
                        g0.setY(g0.y() + .03f);
                        g1.setY(g1.y() + .03f);
                        QVector3D e0 = c0 + s0 * (sign * kRoadHalfM), e1 = c1 + s1 * (sign * kRoadHalfM);
                        e0.setY(e0.y() - .025f);
                        e1.setY(e1.y() - .025f);
                        QVector3D i0g = in0, i1g = in1;
                        i0g.setY(i0g.y() - .025f);
                        i1g.setY(i1g.y() - .025f);
                        // (as the road's own shoulder and verge: inner edge first)
                        quad(e0, e1, i1g, i0g, QVector3D(1, 1, 1), Gravel);
                        quad(i0g, i1g, g1, g0, QVector3D(1, 1, 1), Verge, 0, 0, 1, 1);
                    }
                    continue;
                }
                // one end inside the other road (no widening there): the curve
                // meets the other road's edge in between - end the piece there
                QVector3D c0e = c0, c1e = c1, s0e = s0, s1e = s1;
                float w0e = w0, w1e = w1;
                if ((w0 <= .01f) != (w1 <= .01f) && std::min(q0, q1) <= kRoadHalfM) {
                    const double qi = w0 <= .01f ? q0 : q1, qo = w0 <= .01f ? q1 : q0;
                    const float f = float(std::clamp((kRoadHalfM - qi) / std::max(1e-6, qo - qi), 0.0, 1.0));
                    const QVector3D cm = w0 <= .01f ? c0 + (c1 - c0) * f : c1 + (c0 - c1) * f;
                    const QVector3D sm = w0 <= .01f ? (s0 + (s1 - s0) * f).normalized() : (s1 + (s0 - s1) * f).normalized();
                    if (w0 <= .01f) { c0e = cm; s0e = sm; w0e = kFilletM; }
                    else { c1e = cm; s1e = sm; w1e = kFilletM; }
                }
                const QVector3D i0 = c0e + s0e * (sign * 2.9f), i1 = c1e + s1e * (sign * 2.9f);
                const QVector3D o0 = c0e + s0e * (sign * (kRoadHalfM + w0e)), o1 = c1e + s1e * (sign * (kRoadHalfM + w1e));
                if (k)
                    quad(i0, i1, o1, o0, QVector3D(1, 1, 1), Asphalt, 0, 0, 0, 0);
                else
                    quad(o0, o1, i1, i0, QVector3D(1, 1, 1), Asphalt, 0, 0, 0, 0);
                // the verge beyond the curve, down to the terrain (not on the other road)
                if (q0 > kRoadHalfM + .6 && q1 > kRoadHalfM + .6) {
                    const float r0 = kRoadHalfM + w0e + (kVergeEdgeM - kGravelEdgeM), r1 = kRoadHalfM + w1e + (kVergeEdgeM - kGravelEdgeM);
                    QVector3D g0 = below(c0e + s0e * (sign * r0)), g1 = below(c1e + s1e * (sign * r1));
                    g0.setY(g0.y() + .03f);
                    g1.setY(g1.y() + .03f);
                    QVector3D e0 = o0, e1 = o1;
                    e0.setY(e0.y() - .025f);
                    e1.setY(e1.y() - .025f);
                    quad(e0, e1, g1, g0, QVector3D(1, 1, 1), Verge, 0, 0, 1, 1);
                }
            }
        }
        a.setY(a.y() + .02f);
        b.setY(b.y() + .02f);
        if (!lined)
            continue;
        for (int k = 0; k < 2; ++k) {
            if (join[k])
                continue;   // no edge line across the join
            const float sign = k ? 1.f : -1.f;
            const QVector3D la = k ? saR : saL, lb = k ? sbR : sbL;
            quad(a + la * (sign * 2.75f - .04f), b + lb * (sign * 2.75f - .04f), b + lb * (sign * 2.75f + .04f),
                 a + la * (sign * 2.75f + .04f), QVector3D(.87f, .88f, .79f), Paint);
        }
        if (i % 3 == 0)
            quad(a - sa * .06f, b - sb * .06f, b + sb * .06f, a + sa * .06f, QVector3D(.9f, .88f, .73f), Paint);
    }
    double cx = tileX * 32 + 16, cz = tileZ * 32 + 16;
    QVector3D shadowDir = QVector3D(-m_sun.x(), 0, -m_sun.z()).normalized();
    auto ground = [&](QVector3D q) { return float(m_terrain.surface(q.x(), q.z())) + .06f; };
    // Houses and farms (trees avoid their footprints and gardens).
    // (fewer in the desert, the outback, the icy north: whole sites left out)
    const auto buildings = Settlement::near(m_terrain, cx, cz, 380, m_profile.buildings);
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
    // The countryside (countryside.h): its objects keep trees, rocks and
    // flowers off their spots.
    struct Spot { double x, z, r; };
    std::vector<Spot> taken;
    auto takenAt = [&](double x, double z, double reach) {
        return std::any_of(taken.begin(), taken.end(),
                           [&](const Spot &s) { return std::hypot(x - s.x, z - s.z) < s.r + reach; });
    };
    auto contact = [&](double x, double z, QVector3D fwd, float rx, float rz, float strength) {
        shadowDisc(m_buildDecals, QVector3D(float(x), 0, float(z)) + shadowDir * .3f, fwd, rx, rz, strength, ground);
    };
    // animals: the nearest kMaxAnimals (a tile in the middle of a big herd stays cheap)
    constexpr size_t kMaxAnimals = 150;
    auto animals = m_profile.animals ? Animals::scatter(m_terrain, cx, cz, 260, &buildings) : std::vector<AnimalSpec>();
    if (animals.size() > kMaxAnimals) {
        auto byDistance = [&](const AnimalSpec &p, const AnimalSpec &q) {
            return std::hypot(p.x - cx, p.z - cz) < std::hypot(q.x - cx, q.z - cz);
        };
        std::nth_element(animals.begin(), animals.begin() + kMaxAnimals, animals.end(), byDistance);
        animals.resize(kMaxAnimals);
    }
    for (const auto &a : animals) {
        const double dist = std::hypot(a.x - cx, a.z - cz);
        animal(a, dist > 130, dist > 180);
        const float big = a.kind == Sheep ? .6f : 1.f;
        taken.push_back({ a.x, a.z, 1.4 * big });
        if (dist < 120)
            contact(a.x, a.z, QVector3D(std::cos(float(a.heading)), 0, std::sin(float(a.heading))), .45f * big, 1.15f * big, .32f);
    }
    for (const auto &b : m_profile.bales ? Bales::scatter(m_terrain, cx, cz, 260, &buildings) : std::vector<BaleSpec>()) {
        bale(b);
        taken.push_back({ b.x, b.z, 1.1 });
        if (std::hypot(b.x - cx, b.z - cz) < 120)
            contact(b.x, b.z, QVector3D(std::cos(float(b.heading)), 0, std::sin(float(b.heading))), .75f, .85f, .38f);
    }
    for (const auto &t : m_profile.tractors ? Tractors::scatter(m_terrain, cx, cz, 300, &buildings) : std::vector<TractorSpec>()) {
        const double dist = std::hypot(t.x - cx, t.z - cz);
        tractor(t, dist > 130);
        taken.push_back({ t.x, t.z, 3.0 });
        if (dist < 150)
            contact(t.x, t.z, QVector3D(std::cos(float(t.heading)), 0, std::sin(float(t.heading))), 1.3f, 2.2f, .40f);
    }
    for (const auto &t : m_profile.turbines ? Turbines::scatter(m_terrain, cx, cz, 430, &buildings) : std::vector<TurbineSpec>()) {
        turbine(t, std::hypot(t.x - cx, t.z - cz) > 250);
        taken.push_back({ t.x, t.z, 6.0 });
    }
    for (const auto &b : m_benches) {
        if (std::hypot(b.x - cx, b.z - cz) > 230)
            continue;
        bench(b);
        taken.push_back({ b.x, b.z, 1.4 });
    }
    for (const auto &l : m_terrain.lakesNear(cx, cz, 330)) {
        const JettySpec j = LakeLife::jetty(m_terrain, l);
        if (j.valid && std::hypot(j.x0 - cx, j.z0 - cz) < 320)
            jetty(j);
        if (std::hypot(l.x - cx, l.z - cz) < 170 + l.radius)
            for (const auto &r : LakeLife::reeds(m_terrain, l, j))
                if (std::hypot(r.x - cx, r.z - cz) < 170)
                    reeds(r);
        if (j.valid)
            taken.push_back({ (j.x0 + j.x1) * .5, (j.z0 + j.z1) * .5, std::hypot(j.x1 - j.x0, j.z1 - j.z0) * .5 + 1.5 });
    }
    // Fences along the road where a pasture borders it (posts every second
    // road point, 5 m; now and then a field gate), only near the camera.
    for (const uint32_t idx : nearby) {
        const size_t i = idx;
        if (!m_profile.fences || i % 2 || i + 2 >= route.points.size() || route.isShared(i))
            continue;
        const auto &p = route.points[i];
        if (std::hypot(p.x - cx, p.z - cz) > 170)
            continue;
        const double d = p.distance, e = route.points[i + 2].distance;
        const QVector3D sa = side(d), sb = side(e), a = v(p), b = v(route.points[i + 2]);
        for (float s : { -1.f, 1.f }) {
            constexpr float lateral = 11.5f;
            const QVector3D field = a + sa * (s * (lateral + 12));
            if (Landuse::pasture(field.x(), field.z()) < .5 || Landuse::forest(field.x(), field.z()) > .2)
                continue;
            QVector3D pa = a + sa * (s * lateral), pb = b + sb * (s * lateral);
            if (m_terrain.nearest(pa.x(), pa.z()).distance < lateral - .5 || m_terrain.wet(pa.x(), pa.z(), 1.0)
                || Settlement::blocked(buildings, pa.x(), pa.z(), .3, 1))
                continue;
            pa.setY(float(m_terrain.surface(pa.x(), pa.z())));
            pb.setY(float(m_terrain.surface(pb.x(), pb.z())));
            const int stretch = int(std::floor(d / 400));
            const float pick = rnd(WorldNoise::hash(stretch, s > 0 ? 1 : 2, 1301), 1);
            const int type = pick < .5f ? 0 : pick < .8f ? 1 : 2;
            const bool gate = WorldNoise::hash(int(d / 5), s > 0 ? 3 : 4, 1303) % 31 == 0;
            fenceSegment(pa, pb, sa * s, type, gate);
        }
    }
    // The display bike's spot stays clear (no tree, rock or flowers on it).
    const DisplaySpot &bike = m_bikeSpot;
    auto atBike = [&](double x, double z, double reach) {
        return bike.valid && std::hypot(x - bike.pos.x(), z - bike.pos.z()) < reach;
    };
    if (bike.valid && std::hypot(bike.pos.x() - cx, bike.pos.z() - cz) < 170) {
        const QVector3D c = bike.pos + shadowDir * .25f;
        shadowDisc(m_buildDecals, c, -bike.x, .42f, 1.05f, .40f, [&](QVector3D q) { return bike.groundAt(q) + .05f; });
    }
    // Trees: detailed models near the road, simple ones further out (fog hides the rest).
    std::vector<TreeSpec> drawnTrees;
    for (const auto &t : Vegetation::scatter(m_terrain, cx, cz, 300, &buildings, &m_profile)) {
        const bool small = t.kind == Bush || t.kind == Cactus;
        if (atBike(t.x, t.z, 2.5 + t.radius) || takenAt(t.x, t.z, t.radius * (small ? .6 : .3)))
            continue;
        drawnTrees.push_back(t);
        double dist = std::hypot(t.x - cx, t.z - cz);
        bool far = dist > 115;
        tree(t, far);
        if (dist < 170) {
            // (a cactus or a palm casts a slim shadow, an acacia a wide one)
            const float spread = t.kind == Cactus ? .45f : t.kind == Palm ? .8f : t.kind == Bush ? 1.1f : 1.25f;
            QVector3D c = QVector3D(float(t.x), 0, float(t.z)) + shadowDir * float(t.height * (t.kind == Bush ? .15 : .22));
            float r = float(t.radius) * spread;
            shadowDisc(m_buildDecals, c, shadowDir, r, r * 1.25f, t.kind == Bush ? .30f : .42f, ground);
        }
    }
    // the little birds' treetops
    m_buildPerches.clear();
    perches(drawnTrees, m_terrain, cx, cz, m_profile, m_buildPerches);
    // Rocks and boulders (simpler further out), wild flowers close by (they are small).
    for (const auto &s : Rocks::scatter(m_terrain, cx, cz, 240, &buildings, m_profile.rocks)) {
        if (atBike(s.x, s.z, 2.0 + 1.5 * s.size) || takenAt(s.x, s.z, s.size))
            continue;
        const double dist = std::hypot(s.x - cx, s.z - cz);
        rock(s, dist > 90);
        if (dist < 120 && s.size > .35) {
            QVector3D c = QVector3D(float(s.x), 0, float(s.z)) + shadowDir * float(s.size * .25);
            const float r = float(s.size) * 1.05f;
            shadowDisc(m_buildDecals, c, shadowDir, r, r * 1.2f, .32f, ground);
        }
    }
    for (const auto &f : Flowers::scatter(m_terrain, cx, cz, 75, &buildings, m_profile.flowers))
        if (!atBike(f.x, f.z, 1.6) && !takenAt(f.x, f.z, .4))
            flowers(f);
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
    glClearColor(m_profile.horizon.r, m_profile.horizon.g, m_profile.horizon.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    if (!m_route || m_route->points.size() < 2)
        return;   // just the haze colour until a route is set
    const Route &route = *m_route;
    distance = std::clamp(distance, 0.0, route.length());
    scene(distance);
    if (m_ringDirty) {   // a route in another biome: its distant hills
        m_ringDirty = false;
        m_ringBuffer.bind();
        m_ringBuffer.allocate(m_ring.data(), int(m_ring.size() * sizeof(Vertex)));
        m_ringBuffer.release();
    }

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
    // near plane .5 m (the closest thing in view is the road a few metres
    // ahead and the rider 3 m ahead): the depth buffer's precision grows with
    // it, so faces just in front of others (windows, doors, plates) do not flicker
    projection.perspective(66, float(pixelSize.width()) / float(std::max(1, pixelSize.height())), kNearPlaneM, 2500);
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
    if (m_biomeUniformsDirty)   // (per route: the programs keep them)
        setBiomeUniforms(m_skyShader, true);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_skyShader.release();
    m_skyVao.release();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // 2. Opaque landscape.
    m_vao.bind();
    m_buffer.bind();
    for (QOpenGLShaderProgram *program : { &m_cutoutShader, &m_shader }) {
        program->bind();
        program->setUniformValue("mvp", projection * view);
        program->setUniformValue("eye", eye);
        program->setUniformValue("uvOrigin", uvOrigin);
        program->setUniformValue("sunDir", m_sun);
        program->setUniformValue("time", time);
        program->setUniformValue("offset", QVector3D());
        program->setUniformValue("opacity", 1.0f);
        program->setUniformValue("asphaltMap", 0);
        program->setUniformValue("grassMap", 1);
        program->setUniformValue("gravelMap", 2);
        program->setUniformValue("noiseMap", 3);
        program->setUniformValue("bannerMap", 4);
        if (m_biomeUniformsDirty)
            setBiomeUniforms(*program, false);
    }
    m_biomeUniformsDirty = false;
    attributes();
    glDrawArrays(GL_TRIANGLES, 0, m_meshCount);
    // 3. Distant hills, centred on the camera like a skybox.
    m_ringBuffer.bind();
    attributes();
    m_shader.setUniformValue("offset", QVector3D(eye.x(), eye.y(), eye.z()));
    glDrawArrays(GL_TRIANGLES, 0, int(m_ring.size()));
    m_shader.setUniformValue("offset", QVector3D());
    // the birds (moving: rebuilt every frame)
    m_birdVertices.clear();
    birds(distance, eye, time, m_birdVertices);
    if (!m_birdVertices.empty()) {
        m_birdBuffer.bind();
        m_birdBuffer.allocate(m_birdVertices.data(), int(m_birdVertices.size() * sizeof(Vertex)));
        attributes();
        glDrawArrays(GL_TRIANGLES, 0, int(m_birdVertices.size()));
    }
    // the arches, signs, road text, km posts and the bike (cut-out letters):
    // after the other opaque geometry, with the discarding program
    m_cutoutShader.bind();
    drawGates(distance);
    m_shader.bind();
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
