#pragma once
#include "ghost_vertex.h"

#include <QByteArray>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

// A skinned, animated glTF 2.0 binary model (.glb) for the 3D view's figure:
// the Treadmill Run's runner (e.g. a Mixamo rig with a "Running" in-place
// clip). Geometry only (no GL): the figure is skinned on the CPU every frame
// and drawn through the same vertex / index path as the procedural figures.
//
// Supported: one skin, triangle meshes with POSITION / NORMAL / JOINTS_0 /
// WEIGHTS_0 (+ indices), node TRS hierarchies, and the first animation
// (LINEAR / STEP samplers for translation, rotation and scale). Materials and
// textures are ignored: the figure is coloured per body part (clothes, skin,
// shoes) from the skin's joint names (Mixamo naming), so any rig works.
namespace route3d {

class GlbModel {
public:
    // How the figure is coloured: a runner (shirt, shorts, shoes) or a road
    // cyclist (jersey, black bib shorts to above the knee, white socks / shoes).
    enum class Outfit { Runner, Cyclist };

    // Loads and prepares the model. False (and *error) when the file is not
    // a usable skinned, animated .glb.
    bool load(const QByteArray& glb, QString* error = nullptr, Outfit outfit = Outfit::Runner);
    bool loadFile(const QString& path, QString* error = nullptr, Outfit outfit = Outfit::Runner);

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
    void colorize();   // colours per body part, triangles split on the cuts

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

    // on the bike: bind pose in the bike frame (x right, y up, z forward, m)
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
    QVector3D m_helmetOffset, m_helmetRadii;    // relative to the head joint, bind pose

    // normalisation (from the skinned model): local = m_fix * (skinned - root offset)
    QMatrix4x4 m_fix;                           // yaw (face +z) + uniform scale + ground
    std::vector<QVector3D> m_rootXZ;            // per sampled phase: horizontal root drift
};

} // namespace route3d
