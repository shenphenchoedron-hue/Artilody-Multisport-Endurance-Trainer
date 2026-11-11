#pragma once
#include "ghost_vertex.h"

#include <QByteArray>
#include <QImage>
#include <QMatrix4x4>
#include <QVector2D>

#include <memory>
#include <QQuaternion>
#include <QString>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <QMutex>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>

// A skinned, animated glTF 2.0 binary model (.glb) for the 3D view's figure:
// the Treadmill Run's runner (e.g. a Mixamo rig with a "Running" in-place
// clip). Geometry only (no GL): the figure is skinned on the CPU every frame
// and drawn through the same vertex / index path as the procedural figures.
//
// Supported: one skin, triangle meshes with POSITION / NORMAL / JOINTS_0 /
// WEIGHTS_0 (+ indices), node TRS hierarchies, and the first animation
// (LINEAR / STEP samplers for translation, rotation and scale). The Runner /
// Cyclist / Rower outfits ignore materials and textures: the figure is
// coloured per body part (clothes, skin, shoes) from the skin's joint names
// (Mixamo naming), so any rig works. The Avatar outfit instead shows the
// model's own clothes: the base colour texture (TEXCOORD_0, PNG / JPEG
// embedded in the .glb) sampled per vertex, with the jersey, the shorts, the
// helmet and the shoes found in it so they can be recoloured (colorsFor()).
namespace route3d {

class GlbModel {
public:
    // How the figure is coloured: a runner (shirt, shorts, shoes) or a road
    // cyclist (jersey, black bib shorts to above the knee, white socks / shoes).
    // ...or a sculler in a sleeveless unisuit to above the knee.
    // Avatar: a textured cyclist with its own clothes and helmet (e.g. made in
    // Tripo, rigged in Mixamo) - the texture's colours, no painted helmet.
    // (Without a usable texture it is coloured like the Cyclist.)
    // RunnerAvatar: the same for a textured runner (no helmet: the hair is
    // found over the whole head; without a texture coloured like the Runner).
    // RowerAvatar: a textured sculler (bare-headed, barefoot: no helmet, no
    // shoes; without a texture coloured like the Rower).
    enum class Outfit { Runner, Cyclist, Rower, Avatar, RunnerAvatar, RowerAvatar };

    // The recolourable parts of an Avatar's clothes (and its hair).
    enum Garment : uint8_t { NoGarment, Jersey, Shorts, Helmet, Shoes, Hair, GarmentCount };
    // Display colours (0..1) per garment; a negative x = keep the texture's colour.
    struct GarmentColors {
        QVector3D color[GarmentCount] = { { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 } };
    };
    // The figure's vertex colours with the garments recoloured: the chosen
    // colour, keeping the texture's light and shade (folds, seams) on it.
    // Index i matches the vertices poseOnBike() / poseRowing() / pose() write.
    // Empty when nothing is to change (no colour chosen, not textured): the
    // figure's own colours.
    std::vector<QVector3D> colorsFor(const GarmentColors& colors) const;
    // its own vertex colours (as colorsFor() indexes them)
    const std::vector<QVector3D>& ownColors() const { return m_color; }
    // An Avatar whose texture was used (its garments can be recoloured).
    bool textured() const { return m_textured; }
    // per vertex: the garment it belongs to (Avatar; else empty)
    const std::vector<uint8_t>& garments() const { return m_garment; }
    // the garment's main colour in the texture (display colours; x < 0: not found)
    QVector3D garmentColor(Garment g) const { return m_garmentMain[g]; }

    // Loads and prepares the model. False (and *error) when the file is not
    // a usable skinned, animated .glb.
    // motion: another Mixamo figure whose animation is used instead of the
    // model's own (moved onto this skeleton by joint name: retargetFrom())
    bool load(const QByteArray& glb, QString* error = nullptr, Outfit outfit = Outfit::Runner,
              const GlbModel* motion = nullptr);
    bool loadFile(const QString& path, QString* error = nullptr, Outfit outfit = Outfit::Runner,
                  const GlbModel* motion = nullptr);
    // its animation is another figure's (load(): motion)
    bool retargeted() const { return m_retargeted; }
    // The same figure (mesh, colours, body adjustments) moving as `motion`
    // (another Mixamo figure, e.g. the classic walker): a copy, its animation
    // replaced - no second load. Null when the motion does not fit.
    std::shared_ptr<GlbModel> withMotion(const GlbModel& motion) const;
    // Narrower (< 1) shoulders: the shoulder bones (clavicles) that much
    // shorter, the arms their size - for a broad-shouldered figure. Set after
    // load(), before it is shared (not thread-safe).
    void setShoulderWidth(float scale);
    // A slimmer upper body: the flesh of the shoulders, the arms, the upper
    // chest and the neck that fraction (0..0.5) in towards the bones -
    // weighted by the skin, so it blends into the rest. After load(), before
    // it is shared; the animated figure only (pose()).
    void slimUpperBody(float amount);
    // The legs turned in at the hips (degrees, 0..10): the feet nearer the
    // line under the body - for a broad-hipped figure. Before load(): the
    // load sets the turn up (a textured runner).
    void setLegsIn(float deg) { m_legInDeg = std::clamp(deg, 0.f, 10.f); }
    // A shorter neck (scale of the neck bone, 0.5..1): the head lower on the
    // shoulders - for a long-necked figure. Before load().
    void setNeckLength(float scale) { m_neckScale = std::clamp(scale, .5f, 1.f); }
    // A thicker (> 1) or thinner neck (its width, 0.8..1.5). Before load().
    void setNeckWidth(float scale) { m_neckWidth = std::clamp(scale, .8f, 1.5f); }

    // Duration of the animation cycle (s) - one stride (a left and a right step).
    double duration() const { return m_duration; }
    // the mesh's vertices plus the points added on the colour cuts
    size_t vertexCount() const { return m_pos.size() + m_extra.size(); }
    const std::vector<uint32_t>& indices() const { return m_indices; }
    // Standing height of the figure in metres after normalisation (1.78).
    static constexpr float kHeightM = 1.78f;

    // The figure at cycle phase `phase` (0..1) in its local frame: x = the
    // figure's right, y = up (feet on y = 0 at their lowest), z = forward,
    // metres, in place (no root motion). Writes positions + normals + colours.
    void pose(double phase, std::vector<GhostVertex>& out) const;

    // --- on the bike (GhostRider) -------------------------------------------
    // The figure is not animated on the bike: its skeleton is posed from the
    // bind (T) pose every frame - the hips on the saddle, the feet on the
    // pedals (two-bone IK from the crank angle), the hands on the brake
    // hoods with the fingers curled, the back leaning so the arms reach.
    // Frame of the bike: x = rider's right, y = up, z = forward, metres,
    // ground at y = 0. The figure is scaled to kRiderLegM legs.
    static constexpr float kRiderLegM = .87f;
    // side-to-side rock of the hips / torso with the pedal stroke (degrees)
    static constexpr double kSwayDeg = 2.5;
    // on climbs the rocking grows (he works harder): from 6 % to 10 % grade it
    // rises smoothly to kClimbSwayDeg
    static constexpr double kClimbSwayDeg = 6.0;
    static double swayForGrade(double grade)
    {
        const double t = std::clamp((grade - .06) / .04, 0.0, 1.0);
        return kSwayDeg + (kClimbSwayDeg - kSwayDeg) * t * t * (3 - 2 * t);
    }
    struct BikeFit {
        QVector3D bottomBracket;
        float crankLength = .17f;
        float pedalX = .12f;          // pedal distance from the frame's centre
        double crank = 0;             // right crank angle (rad); the left is + pi
        double swayDeg = kSwayDeg;    // side-to-side rock (swayForGrade())
        QVector3D saddleTop;
        QVector3D hood[2];            // brake hoods: [0] left (x < 0), [1] right
    };
    // The head after posing: the helmet to draw around it (bike frame).
    struct HeadPose {
        QVector3D helmetCenter, helmetRadii, up, forward;
    };
    // True when the skeleton has the joints posing needs (Mixamo names).
    bool canRide() const { return m_canRide; }
    void poseOnBike(const BikeFit& fit, std::vector<GhostVertex>& out, HeadPose* head = nullptr) const;
    // How far the saddle must come up (metres, >= 0) for the seat to rest on
    // it: a textured avatar's full bottom hangs above the saddle the bike was
    // set up for (the hips' height follows the legs). GhostRider draws the
    // saddle and the seatpost that much higher and poses with fit.saddleTop
    // raised. 0 for the other outfits.
    float saddleRaise(const BikeFit& fit) const;

    // --- in a rowing boat (Rowing3DItem) -----------------------------------------
    // Posed from the bind pose like on the bike, in the same frame (x = the
    // rower's right, y = up, z = forward: the way he faces, the boat's
    // stern), metres, the seat's top at y = 0. The caller gives the stroke's
    // positions: the hips on the sliding seat, the feet on the foot stretcher,
    // the hands on the oar handles, the back's lean (+ forward at the catch,
    // - back at the finish). The back leans further when the hands are out of reach.
    struct RowFit {
        QVector3D hipCenter;
        QVector3D ankle[2];           // [0] left, [1] right
        double footPitchDeg = -55;    // toes up on the stretcher
        QVector3D wrist[2];
        double torsoDeg = 0;
        double headDeg = 4;           // the head (absolute): looking level, over the stern
    };
    void poseRowing(const RowFit& fit, std::vector<GhostVertex>& out) const;

private:
    struct Node {
        QVector3D t{0, 0, 0};
        QQuaternion r;
        QVector3D s{1, 1, 1};
        bool hasMatrix = false;
        QMatrix4x4 matrix;
        int parent = -1;
        QString name;
    };
    struct Channel {
        int node = -1;
        int path = 0;           // 0 translation, 1 rotation, 2 scale
        bool step = false;
        std::vector<float> times;
        std::vector<float> values;   // 3 or 4 per key
        // a rotation's swing about `mean` (1 = as in the clip; prepare())
        float swing = 1.f;
        QQuaternion mean;
    };
    void nodeLocal(const std::vector<Node>& nodes, double t, std::vector<QMatrix4x4>& local) const;
    void worldMatrices(double t, std::vector<QMatrix4x4>& world) const;
    void skin(double t, std::vector<QVector3D>& pos, std::vector<QVector3D>* nrm) const;
    void prepare();
    void prepareRider();   // bind data in the bike frame, joint roles (canRide())
    // posed joints (bike frame): positions and rotations from the bind pose
    void solveBike(const BikeFit& fit, double torsoDeg, std::vector<QVector3D>& P,
                   std::vector<QQuaternion>& Q, const QVector3D ankle[2], const QQuaternion foot[2],
                   const QVector3D wrist[2], const QVector3D& hipCenter) const;
    void solveRow(const RowFit& fit, double torsoDeg, std::vector<QVector3D>& P, std::vector<QQuaternion>& Q) const;
    // skinning from posed joints (positions P, rotations Q from the bind pose)
    // (saddleTop: a seated avatar - its seat flattened onto the saddle)
    void skinPosed(const std::vector<QVector3D>& P, const std::vector<QQuaternion>& Q, std::vector<GhostVertex>& out,
                   const QVector3D* saddleTop = nullptr) const;
    static float saddleSurface(float x, float z);
    QVector3D skinOne(const std::vector<QVector3D>& P, const std::vector<QQuaternion>& Q, size_t i) const;
    // the saddle's top the seat rests on (bike frame, about saddleTop): half
    // width, back / front of saddleTop.z; the seat sinks into it this much
    // (padding, the shorts' chamois); at most this much lower / higher
    // (the back of the saddle, where the sit bones rest: kSeatBack .. kSeatRearEnd behind saddleTop)
    static constexpr float kSeatHalfWidth = .068f, kSeatBack = .12f, kSeatRearEnd = .01f;
    static constexpr float kSeatSink = .006f, kSeatMaxRaise = .15f;
    void colorize();   // colours per body part, triangles split on the cuts
    // Avatar: colours from the texture, the garments found in it (false: no
    // usable texture - colorize() is used instead)
    bool colorizeFromTexture();

    std::vector<Node> m_nodes;
    std::vector<int> m_order;                  // parents before children
    std::vector<int> m_joints;                 // skin joint -> node
    std::vector<QMatrix4x4> m_ibm;
    std::vector<Channel> m_channels;
    double m_duration = 0;
    int m_hips = -1;                            // node index of the root joint

    std::vector<QVector3D> m_pos, m_nrm, m_color;
    std::vector<std::array<uint8_t, 4>> m_jnt;
    std::vector<std::array<float, 4>> m_wgt;
    std::vector<uint32_t> m_indices;
    // a point on a colour cut: weights of the corners of its triangle
    struct Extra { uint32_t v[3]; float w[3]; QVector3D color; };
    std::vector<Extra> m_extra;
    Outfit m_outfit = Outfit::Runner;
    std::vector<uint8_t> m_helmet;              // cyclist: vertex is part of the helmet
    // Avatar: per vertex the texture coordinate and its texture (-1: the
    // material's plain colour, m_baseColor), as loaded
    std::vector<QVector2D> m_uv;
    std::vector<int> m_vertexTexture;
    std::vector<QVector3D> m_baseColor;         // per vertex: the material's base colour factor (display)
    std::vector<QImage> m_textures;
    bool m_textured = false;
    std::vector<uint8_t> m_garment;             // per vertex: Garment
    std::vector<float> m_shade;                 // per vertex: lightness relative to its garment's main colour (OKLab L)
    QVector3D m_garmentMain[GarmentCount] = { { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 } };
    // vertices that skin identically to an earlier one (the same point on a
    // texture seam): the index of that one, else their own (skinned once)
    std::vector<uint32_t> m_same;

    // on the bike: bind pose in the bike frame (x right, y up, z forward, m)
    int m_jShoulder[2] = { -1, -1 };            // the clavicles (optional)
    static constexpr float kRowShoulderUpDeg = 12.f;   // a textured rower's shoulders raised (solveRow())
    enum class Role : uint8_t { Other, Hips, Spine, Spine1, Spine2, Neck, Head, UpLeg, Leg, Foot,
                                Arm, ForeArm, Hand, Finger };
    struct JointInfo { Role role = Role::Other; int side = 0; int digit = 0; bool thumb = false; };
    bool m_canRide = false;
    std::vector<QVector3D> m_bf, m_nf;          // vertices / normals, bind pose
    std::vector<QVector3D> m_jf;                // joints, bind pose
    std::vector<int> m_jparent, m_jorder;       // parent joint, parents-first order
    std::vector<JointInfo> m_jinfo;
    std::vector<QVector3D> m_jdir;              // bind direction to the joint's "child" (bone)
    int m_jHips = -1, m_jHead = -1;
    int m_jUpLeg[2] = { -1, -1 }, m_jLeg[2] = { -1, -1 }, m_jFoot[2] = { -1, -1 }, m_jToe[2] = { -1, -1 };
    int m_jArm[2] = { -1, -1 }, m_jForeArm[2] = { -1, -1 }, m_jHand[2] = { -1, -1 };
    float m_soleY = 0;
    std::vector<uint32_t> m_seatVerts;          // vertices that can rest on the saddle
    std::vector<uint8_t> m_seatMask;            // per vertex: one of them
    std::vector<uint32_t> m_buttockVerts;       // those following the pelvis (not a thigh)
    // how far the saddle comes up to a seated avatar (saddleRaise(): measured
    // once per bike - the same every frame; keyed by the bike's saddle and
    // bottom bracket; any thread)
    struct SeatDrop {
        QMutex mutex;
        bool valid = false;
        QVector3D saddleTop, bottomBracket;
        float raise = 0;
        SeatDrop() = default;
        SeatDrop(const SeatDrop&) {}
        SeatDrop& operator=(const SeatDrop&) { valid = false; return *this; }
    };
    mutable SeatDrop m_seatDrop;
    QVector3D m_helmetOffset, m_helmetRadii;    // relative to the head joint, bind pose

    // normalisation (from the skinned model): local = m_fix * (skinned - root offset)
    QMatrix4x4 m_fix;                           // yaw (face +z) + uniform scale + ground
    QQuaternion m_up;                           // stands a model stored lying up (applied to the root nodes)
    // textures in a .glb: this many pixels a side at most, this many in all
    static constexpr int kMaxTexturePx = 8192;
    static constexpr qint64 kMaxTexturePixels = qint64(4096) * 4096 * 2;
    bool retargetFrom(const GlbModel& src);
    void prepareMotion();   // (prepare(): what depends on the animation)
    void prepareBounce();
    static constexpr float kHasBounceM = .03f;   // the clip bounces this much already: none added
    bool m_retargeted = false;
    // a textured runner's upper arms turned in (worldMatrices(); prepare())
    static constexpr float kArmInDeg = 12.f;
    int m_armNode[2] = { -1, -1 };
    int m_shoulderNode[2] = { -1, -1 };
    int m_legNode[2] = { -1, -1 };
    QQuaternion m_legIn[2];
    float m_legInDeg = 0.f;                     // (setLegsIn())
    float m_neckScale = 1.f;                    // (setNeckLength())
    float m_neckWidth = 1.f;                    // (setNeckWidth())
    void shortenNeck();
    void widenNeck();
    float m_shoulderScale = 1.f;
    QQuaternion m_armIn[2];
    // a textured face's skin takes the arms' tone (colorizeFromTexture()), lighter by this much (OKLab L)
    static constexpr float kFaceLift = .07f;
    static constexpr float kFaceChroma = 1.25f;      // ...and this much more colourful
    static constexpr float kThighSwing = .75f;    // a textured runner's thigh swing (of its clip's)
    // a textured runner's bounce, put back when its clip has none (pose(); prepareBounce())
    static constexpr float kRunBounceM = .06f;   // rise from midstance to the flight
    static constexpr float kFootRegionM = .3f;   // the feet: this low on average over the cycle
    static constexpr int kBounceSamples = 96;
    std::vector<float> m_bounce;                // per sampled phase: the lift (may be < 0; empty: none)
    std::vector<QVector3D> m_rootXZ;            // per sampled phase: horizontal root drift
};

// A static (not skinned, not animated) glTF 2.0 binary model: scenery such as
// the display bike by the START arch. The node hierarchy is flattened into
// plain triangles in the model's frame (metres, y up, as authored).
//
// Supported: triangle meshes with POSITION (+ NORMAL, COLOR_0, indices), node
// TRS / matrix transforms, a material's baseColorFactor. Colours are linear in
// glTF and converted to the display (sRGB-like) colours the 3D view uses.
// Textures are ignored. Decals: node "extras": {"decals": [{name, size [w,h],
// at, normal, up}]} - a place for a printed label, e.g. a brand name.
class StaticModel {
public:
    struct Vertex { QVector3D position, normal, color; };
    struct Decal {
        float width = 0, height = 0;   // metres
        QVector3D at, normal, up;      // model frame (unit normal / up)
    };
    bool load(const QByteArray& glb, QString* error = nullptr);
    bool loadFile(const QString& path, QString* error = nullptr);
    // three vertices per triangle
    const std::vector<Vertex>& triangles() const { return m_triangles; }
    const std::vector<Decal>& decals() const { return m_decals; }

private:
    std::vector<Vertex> m_triangles;
    std::vector<Decal> m_decals;
};

} // namespace route3d
