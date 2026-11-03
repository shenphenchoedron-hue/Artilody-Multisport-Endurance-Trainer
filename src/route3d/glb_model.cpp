#include "glb_model.h"

#include <QFile>
#include <QGenericMatrix>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector2D>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>

namespace route3d {

namespace {

constexpr quint32 kMagic = 0x46546C67;      // "glTF"
constexpr quint32 kChunkJson = 0x4E4F534A;  // "JSON"
constexpr quint32 kChunkBin = 0x004E4942;   // "BIN\0"

bool fail(QString* error, const QString& text)
{
    if (error)
        *error = text;
    return false;
}

int componentCount(const QString& type)
{
    if (type == QLatin1String("SCALAR")) return 1;
    if (type == QLatin1String("VEC2")) return 2;
    if (type == QLatin1String("VEC3")) return 3;
    if (type == QLatin1String("VEC4")) return 4;
    if (type == QLatin1String("MAT4")) return 16;
    return 0;
}

int componentSize(int ct)
{
    switch (ct) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
    }
}

// Reads an accessor as floats (normalised integers are scaled to 0..1).
// Every offset, stride and count from the file is checked against the BIN
// chunk before it is read (the file may be any .glb).
struct Reader {
    QJsonObject json;
    QByteArray bin;
    static constexpr qint64 kMaxCount = 1 << 24;   // elements per accessor

    // The accessor's start and stride in bin; false unless all `count`
    // elements of `element` bytes lie inside its buffer view and the BIN chunk.
    bool view(const QJsonObject& a, qint64 element, qint64 count, qint64& offset, qint64& stride) const
    {
        const QJsonArray views = json.value(QStringLiteral("bufferViews")).toArray();
        const int vi = a.value(QStringLiteral("bufferView")).toInt(-1);
        if (vi < 0 || vi >= views.size())
            return false;
        const QJsonObject bv = views.at(vi).toObject();
        if (bv.value(QStringLiteral("buffer")).toInt() != 0)
            return false;
        const qint64 viewOffset = bv.value(QStringLiteral("byteOffset")).toInteger();
        const qint64 viewLength = bv.value(QStringLiteral("byteLength")).toInteger(-1);
        const qint64 accOffset = a.value(QStringLiteral("byteOffset")).toInteger();
        stride = bv.contains(QStringLiteral("byteStride")) ? bv.value(QStringLiteral("byteStride")).toInteger() : element;
        if (viewOffset < 0 || viewLength < 0 || accOffset < 0 || viewOffset > bin.size()
            || viewLength > bin.size() - viewOffset || stride < element || stride > 256)
            return false;
        offset = viewOffset + accOffset;
        // all values below 2^31 (bin) and 2^24 * 256: no overflow
        if (count > 0 && accOffset + (count - 1) * stride + element > viewLength)
            return false;
        return true;
    }

    bool read(int index, std::vector<float>& out, int* comps = nullptr) const
    {
        const QJsonArray accessors = json.value(QStringLiteral("accessors")).toArray();
        if (index < 0 || index >= accessors.size())
            return false;
        const QJsonObject a = accessors.at(index).toObject();
        const int n = componentCount(a.value(QStringLiteral("type")).toString());
        const int ct = a.value(QStringLiteral("componentType")).toInt();
        const int cs = componentSize(ct);
        const qint64 count = a.value(QStringLiteral("count")).toInteger();
        if (n == 0 || cs == 0 || count < 0 || count > kMaxCount)
            return false;
        if (comps)
            *comps = n;
        if (!a.contains(QStringLiteral("bufferView"))) {
            out.assign(size_t(count) * size_t(n), 0.f);
            return true;   // all zeros (sparse accessors are not supported)
        }
        qint64 offset = 0, stride = 0;
        if (!view(a, qint64(n) * cs, count, offset, stride))
            return false;
        out.assign(size_t(count) * size_t(n), 0.f);
        const bool normalized = a.value(QStringLiteral("normalized")).toBool();
        const char* base = bin.constData() + offset;
        for (qint64 i = 0; i < count; ++i) {
            const char* e = base + i * stride;
            for (int c = 0; c < n; ++c) {
                const char* p = e + c * cs;
                float v = 0;
                switch (ct) {
                case 5126: { float f; std::memcpy(&f, p, 4); v = qFromLittleEndian(f); break; }
                case 5121: v = float(quint8(*p)); if (normalized) v /= 255.f; break;
                case 5120: v = float(qint8(*p)); if (normalized) v = std::max(-1.f, v / 127.f); break;
                case 5123: v = float(qFromLittleEndian<quint16>(p)); if (normalized) v /= 65535.f; break;
                case 5122: v = float(qFromLittleEndian<qint16>(p)); if (normalized) v = std::max(-1.f, v / 32767.f); break;
                case 5125: v = float(qFromLittleEndian<quint32>(p)); break;
                }
                out[size_t(i) * size_t(n) + size_t(c)] = v;
            }
        }
        return true;
    }

    bool readIndices(int index, std::vector<uint32_t>& out) const
    {
        // (read as integers: float conversion would lose precision above 2^24)
        const QJsonArray accessors = json.value(QStringLiteral("accessors")).toArray();
        if (index < 0 || index >= accessors.size())
            return false;
        const QJsonObject a = accessors.at(index).toObject();
        const int ct = a.value(QStringLiteral("componentType")).toInt();
        const int cs = componentSize(ct);
        const qint64 count = a.value(QStringLiteral("count")).toInteger();
        if ((ct != 5121 && ct != 5123 && ct != 5125) || !a.contains(QStringLiteral("bufferView")) || count < 0
            || count > kMaxCount)
            return false;
        qint64 offset = 0, stride = 0;
        if (!view(a, cs, count, offset, stride))
            return false;
        out.resize(size_t(count));
        for (qint64 i = 0; i < count; ++i) {
            const char* p = bin.constData() + offset + i * stride;
            out[size_t(i)] = ct == 5121 ? quint8(*p) : ct == 5123 ? qFromLittleEndian<quint16>(p) : qFromLittleEndian<quint32>(p);
        }
        return true;
    }
};

// Splits a .glb into its JSON and BIN chunks.
bool parseGlb(const QByteArray& glb, Reader& r, QString* error)
{
    if (glb.size() < 20 || qFromLittleEndian<quint32>(glb.constData()) != kMagic)
        return fail(error, QStringLiteral("not a binary glTF (.glb) file"));
    if (qFromLittleEndian<quint32>(glb.constData() + 4) != 2)
        return fail(error, QStringLiteral("only glTF 2.0 is supported"));
    qint64 pos = 12;
    while (pos + 8 <= glb.size()) {
        const quint32 len = qFromLittleEndian<quint32>(glb.constData() + pos);
        const quint32 type = qFromLittleEndian<quint32>(glb.constData() + pos + 4);
        if (pos + 8 + qint64(len) > glb.size())
            return fail(error, QStringLiteral("truncated .glb chunk"));
        const QByteArray data = glb.mid(int(pos + 8), int(len));
        if (type == kChunkJson)
            r.json = QJsonDocument::fromJson(data).object();
        else if (type == kChunkBin && r.bin.isEmpty())
            r.bin = data;
        pos += 8 + qint64(len);
    }
    if (r.json.isEmpty())
        return fail(error, QStringLiteral("no glTF JSON chunk"));
    return true;
}

QString shortName(QString n)
{
    const int colon = n.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0)
        n = n.mid(colon + 1);
    return n.toLower();
}

} // namespace

bool GlbModel::loadFile(const QString& path, QString* error, Outfit outfit)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot open %1").arg(path));
    return load(f.readAll(), error, outfit);
}

bool GlbModel::load(const QByteArray& glb, QString* error, Outfit outfit)
{
    *this = GlbModel();
    m_outfit = outfit;
    Reader r;
    if (!parseGlb(glb, r, error))
        return false;

    // nodes
    const QJsonArray nodes = r.json.value(QStringLiteral("nodes")).toArray();
    m_nodes.resize(size_t(nodes.size()));
    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonObject n = nodes.at(i).toObject();
        Node& node = m_nodes[size_t(i)];
        node.name = n.value(QStringLiteral("name")).toString();
        const QJsonArray t = n.value(QStringLiteral("translation")).toArray();
        if (t.size() == 3)
            node.t = QVector3D(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
        const QJsonArray q = n.value(QStringLiteral("rotation")).toArray();
        if (q.size() == 4)
            node.r = QQuaternion(float(q[3].toDouble()), float(q[0].toDouble()), float(q[1].toDouble()), float(q[2].toDouble()));
        const QJsonArray s = n.value(QStringLiteral("scale")).toArray();
        if (s.size() == 3)
            node.s = QVector3D(float(s[0].toDouble()), float(s[1].toDouble()), float(s[2].toDouble()));
        const QJsonArray m = n.value(QStringLiteral("matrix")).toArray();
        if (m.size() == 16) {
            float v[16];
            for (int k = 0; k < 16; ++k)
                v[k] = float(m[k].toDouble());
            node.matrix = QMatrix4x4(v).transposed();   // glTF: column-major
            node.hasMatrix = true;
        }
    }
    for (int i = 0; i < nodes.size(); ++i) {
        for (const QJsonValue c : nodes.at(i).toObject().value(QStringLiteral("children")).toArray()) {
            const int ci = c.toInt(-1);
            if (ci < 0 || ci >= int(m_nodes.size()) || m_nodes[size_t(ci)].parent >= 0 || ci == i)
                return fail(error, QStringLiteral("invalid node hierarchy"));
            m_nodes[size_t(ci)].parent = i;
        }
    }
    // parents before children
    std::vector<int> state(m_nodes.size(), 0);
    std::function<bool(int)> visit = [&](int i) {
        if (state[size_t(i)] == 2)
            return true;
        if (state[size_t(i)] == 1)
            return false;   // cycle
        state[size_t(i)] = 1;
        const int p = m_nodes[size_t(i)].parent;
        if (p >= 0 && !visit(p))
            return false;
        state[size_t(i)] = 2;
        m_order.push_back(i);
        return true;
    };
    for (int i = 0; i < int(m_nodes.size()); ++i)
        if (!visit(i))
            return fail(error, QStringLiteral("cyclic node hierarchy"));

    // skin
    const QJsonArray skins = r.json.value(QStringLiteral("skins")).toArray();
    if (skins.isEmpty())
        return fail(error, QStringLiteral("the model has no skeleton (skin) - it cannot be animated"));
    const QJsonObject skin = skins.at(0).toObject();
    for (const QJsonValue j : skin.value(QStringLiteral("joints")).toArray()) {
        const int ji = j.toInt(-1);
        if (ji < 0 || ji >= int(m_nodes.size()))
            return fail(error, QStringLiteral("invalid skin joint"));
        m_joints.push_back(ji);
    }
    if (m_joints.empty() || m_joints.size() > 255)
        return fail(error, QStringLiteral("unsupported number of skin joints"));
    m_ibm.assign(m_joints.size(), QMatrix4x4());
    if (skin.contains(QStringLiteral("inverseBindMatrices"))) {
        std::vector<float> ibm;
        int comps = 0;
        if (!r.read(skin.value(QStringLiteral("inverseBindMatrices")).toInt(), ibm, &comps) || comps != 16
            || ibm.size() < m_joints.size() * 16)
            return fail(error, QStringLiteral("invalid inverse bind matrices"));
        for (size_t j = 0; j < m_joints.size(); ++j)
            m_ibm[j] = QMatrix4x4(&ibm[j * 16]).transposed();
    }
    for (int j : m_joints) {
        const QString n = shortName(m_nodes[size_t(j)].name);
        if (m_hips < 0 && n.contains(QLatin1String("hips")))
            m_hips = j;
    }
    if (m_hips < 0)
        m_hips = m_joints.front();

    // meshes of the skinned nodes
    const QJsonArray meshes = r.json.value(QStringLiteral("meshes")).toArray();
    for (const QJsonValue nv : nodes) {
        const QJsonObject n = nv.toObject();
        if (!n.contains(QStringLiteral("mesh")) || n.value(QStringLiteral("skin")).toInt(-1) != 0)
            continue;
        const QJsonObject mesh = meshes.at(n.value(QStringLiteral("mesh")).toInt()).toObject();
        for (const QJsonValue pv : mesh.value(QStringLiteral("primitives")).toArray()) {
            const QJsonObject p = pv.toObject();
            if (p.value(QStringLiteral("mode")).toInt(4) != 4)
                continue;
            const QJsonObject a = p.value(QStringLiteral("attributes")).toObject();
            std::vector<float> P, N, J, W;
            int pc = 0, nc = 0, jc = 0, wc = 0;
            if (!r.read(a.value(QStringLiteral("POSITION")).toInt(-1), P, &pc) || pc != 3)
                return fail(error, QStringLiteral("mesh without positions"));
            if (!r.read(a.value(QStringLiteral("JOINTS_0")).toInt(-1), J, &jc) || jc != 4
                || !r.read(a.value(QStringLiteral("WEIGHTS_0")).toInt(-1), W, &wc) || wc != 4)
                return fail(error, QStringLiteral("mesh without skin weights (JOINTS_0 / WEIGHTS_0)"));
            const size_t count = P.size() / 3;
            if (!r.read(a.value(QStringLiteral("NORMAL")).toInt(-1), N, &nc) || nc != 3 || N.size() != P.size())
                N.assign(P.size(), 0.f);   // computed below
            if (J.size() != count * 4 || W.size() != count * 4)
                return fail(error, QStringLiteral("inconsistent vertex attributes"));
            const uint32_t base = uint32_t(m_pos.size());
            std::vector<uint32_t> idx;
            if (p.contains(QStringLiteral("indices"))) {
                if (!r.readIndices(p.value(QStringLiteral("indices")).toInt(), idx))
                    return fail(error, QStringLiteral("invalid indices"));
            } else {
                idx.resize(count);
                for (size_t i = 0; i < count; ++i)
                    idx[i] = uint32_t(i);
            }
            for (size_t i = 0; i < count; ++i) {
                m_pos.emplace_back(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]);
                m_nrm.emplace_back(N[i * 3], N[i * 3 + 1], N[i * 3 + 2]);
                std::array<uint8_t, 4> jj {};
                std::array<float, 4> ww {};
                float sum = 0;
                for (int k = 0; k < 4; ++k) {
                    const int ji = int(J[i * 4 + size_t(k)]);
                    jj[size_t(k)] = uint8_t(ji >= 0 && ji < int(m_joints.size()) ? ji : 0);
                    ww[size_t(k)] = std::max(0.f, W[i * 4 + size_t(k)]);
                    sum += ww[size_t(k)];
                }
                if (sum <= 1e-6f) { ww = { 1, 0, 0, 0 }; sum = 1; }
                for (float& w : ww)
                    w /= sum;
                m_jnt.push_back(jj);
                m_wgt.push_back(ww);
            }
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                if (idx[i] >= count || idx[i + 1] >= count || idx[i + 2] >= count)
                    return fail(error, QStringLiteral("index out of range"));
                m_indices.push_back(base + idx[i]);
                m_indices.push_back(base + idx[i + 1]);
                m_indices.push_back(base + idx[i + 2]);
            }
            if (nc != 3) {
                // flat-averaged normals for meshes without NORMAL
                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    const QVector3D& A = m_pos[base + idx[i]];
                    const QVector3D n3 = QVector3D::crossProduct(m_pos[base + idx[i + 1]] - A, m_pos[base + idx[i + 2]] - A);
                    for (int k = 0; k < 3; ++k)
                        m_nrm[base + idx[i + size_t(k)]] += n3;
                }
            }
        }
    }
    if (m_pos.empty() || m_indices.empty())
        return fail(error, QStringLiteral("the model has no skinned triangle mesh"));
    for (QVector3D& n : m_nrm)
        n.normalize();

    // first animation
    const QJsonArray anims = r.json.value(QStringLiteral("animations")).toArray();
    if (anims.isEmpty())
        return fail(error, QStringLiteral("the model has no animation (e.g. a running cycle)"));
    const QJsonObject anim = anims.at(0).toObject();
    const QJsonArray samplers = anim.value(QStringLiteral("samplers")).toArray();
    float t0 = 1e9f, t1 = -1e9f;
    for (const QJsonValue cv : anim.value(QStringLiteral("channels")).toArray()) {
        const QJsonObject c = cv.toObject();
        const QJsonObject target = c.value(QStringLiteral("target")).toObject();
        const QString path = target.value(QStringLiteral("path")).toString();
        Channel ch;
        ch.node = target.value(QStringLiteral("node")).toInt(-1);
        ch.path = path == QLatin1String("translation") ? 0 : path == QLatin1String("rotation") ? 1
                : path == QLatin1String("scale") ? 2 : -1;
        if (ch.node < 0 || ch.node >= int(m_nodes.size()) || ch.path < 0)
            continue;   // (weights / morph targets are not supported)
        const QJsonObject s = samplers.at(c.value(QStringLiteral("sampler")).toInt()).toObject();
        const QString interp = s.value(QStringLiteral("interpolation")).toString(QStringLiteral("LINEAR"));
        if (interp == QLatin1String("CUBICSPLINE"))
            return fail(error, QStringLiteral("cubic spline animation is not supported - export with linear keys"));
        ch.step = interp == QLatin1String("STEP");
        int ic = 0, oc = 0;
        if (!r.read(s.value(QStringLiteral("input")).toInt(-1), ch.times, &ic) || ic != 1
            || !r.read(s.value(QStringLiteral("output")).toInt(-1), ch.values, &oc) || oc != (ch.path == 1 ? 4 : 3)
            || ch.times.empty() || ch.values.size() != ch.times.size() * size_t(oc))
            return fail(error, QStringLiteral("invalid animation sampler"));
        t0 = std::min(t0, ch.times.front());
        t1 = std::max(t1, ch.times.back());
        m_channels.push_back(std::move(ch));
    }
    if (m_channels.empty() || !(t1 > t0))
        return fail(error, QStringLiteral("the animation has no usable keys"));
    for (Channel& ch : m_channels)
        for (float& t : ch.times)
            t -= t0;
    m_duration = t1 - t0;

    prepare();
    prepareRider();
    return true;
}

void GlbModel::nodeLocal(const std::vector<Node>& nodes, double t, std::vector<QMatrix4x4>& local) const
{
    std::vector<Node> n = nodes;
    if (t >= 0) {
        for (const Channel& ch : m_channels) {
            const auto& T = ch.times;
            size_t k = 0;
            float f = 0;
            if (t <= T.front()) {
                k = 0;
            } else if (t >= T.back()) {
                k = T.size() - 1;
            } else {
                k = size_t(std::upper_bound(T.begin(), T.end(), float(t)) - T.begin()) - 1;
                if (!ch.step && k + 1 < T.size())
                    f = float((t - T[k]) / std::max(1e-6f, T[k + 1] - T[k]));
            }
            const size_t k1 = std::min(k + 1, T.size() - 1);
            Node& node = n[size_t(ch.node)];
            node.hasMatrix = false;
            if (ch.path == 1) {
                const float* a = &ch.values[k * 4];
                const float* b = &ch.values[k1 * 4];
                const QQuaternion qa(a[3], a[0], a[1], a[2]), qb(b[3], b[0], b[1], b[2]);
                node.r = f > 0 ? QQuaternion::slerp(qa, qb, f).normalized() : qa.normalized();
            } else {
                const float* a = &ch.values[k * 3];
                const float* b = &ch.values[k1 * 3];
                const QVector3D v(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f);
                (ch.path == 0 ? node.t : node.s) = v;
            }
        }
    }
    local.resize(n.size());
    for (size_t i = 0; i < n.size(); ++i) {
        if (n[i].hasMatrix) {
            local[i] = n[i].matrix;
        } else {
            QMatrix4x4 m;
            m.translate(n[i].t);
            m.rotate(n[i].r);
            m.scale(n[i].s);
            local[i] = m;
        }
    }
}

void GlbModel::worldMatrices(double t, std::vector<QMatrix4x4>& world) const
{
    std::vector<QMatrix4x4> local;
    nodeLocal(m_nodes, t, local);
    world.resize(local.size());
    for (int i : m_order) {
        const int p = m_nodes[size_t(i)].parent;
        world[size_t(i)] = p >= 0 ? world[size_t(p)] * local[size_t(i)] : local[size_t(i)];
    }
}

void GlbModel::skin(double t, std::vector<QVector3D>& pos, std::vector<QVector3D>* nrm) const
{
    std::vector<QMatrix4x4> world;
    worldMatrices(t, world);
    std::vector<QMatrix4x4> jm(m_joints.size());
    for (size_t j = 0; j < m_joints.size(); ++j)
        jm[j] = world[size_t(m_joints[j])] * m_ibm[j];
    pos.resize(m_pos.size());
    if (nrm)
        nrm->resize(m_pos.size());
    for (size_t i = 0; i < m_pos.size(); ++i) {
        QVector3D p(0, 0, 0), n(0, 0, 0);
        for (int k = 0; k < 4; ++k) {
            const float w = m_wgt[i][size_t(k)];
            if (w <= 0)
                continue;
            const QMatrix4x4& m = jm[m_jnt[i][size_t(k)]];
            p += w * m.map(m_pos[i]);
            if (nrm)
                n += w * m.mapVector(m_nrm[i]);
        }
        pos[i] = p;
        if (nrm)
            (*nrm)[i] = n;
    }
}

void GlbModel::prepare()
{
    // rest pose: standing height and facing
    std::vector<QVector3D> rest;
    skin(-1, rest, nullptr);
    float minY = 1e30f, maxY = -1e30f;
    for (const QVector3D& p : rest) {
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    const float height = std::max(1e-6f, maxY - minY);
    std::vector<QMatrix4x4> world;
    worldMatrices(-1, world);
    auto jointPos = [&](const char* name) -> std::optional<QVector3D> {
        for (int j : m_joints)
            if (shortName(m_nodes[size_t(j)].name) == QLatin1String(name))
                return world[size_t(j)].map(QVector3D(0, 0, 0));
        return std::nullopt;
    };
    // toes point forward
    QVector3D fwd(0, 0, 1);
    QVector3D sum(0, 0, 0);
    for (const auto& pair : { std::pair("lefttoe_end", "leftfoot"), std::pair("righttoe_end", "rightfoot"),
                              std::pair("lefttoebase", "leftfoot"), std::pair("righttoebase", "rightfoot") }) {
        const auto a = jointPos(pair.first), b = jointPos(pair.second);
        if (a && b)
            sum += *a - *b;
    }
    if (QVector2D(sum.x(), sum.z()).length() > height * 0.01f)
        fwd = QVector3D(sum.x(), 0, sum.z()).normalized();
    const float yaw = std::atan2(fwd.x(), fwd.z()) * 180.f / float(M_PI);   // rotate -yaw about y -> +z

    // the cycle: ground (lowest point) and root drift
    constexpr int kSamples = 24;
    float ground = 1e30f;
    QVector3D rootSum(0, 0, 0);
    std::vector<QVector3D> sp;
    for (int s = 0; s < kSamples; ++s) {
        const double t = m_duration * s / kSamples;
        skin(t, sp, nullptr);
        for (const QVector3D& p : sp)
            ground = std::min(ground, p.y());
        worldMatrices(t, world);
        rootSum += world[size_t(m_hips)].map(QVector3D(0, 0, 0));
    }
    worldMatrices(0, world);
    const QVector3D rootStart = world[size_t(m_hips)].map(QVector3D(0, 0, 0));
    worldMatrices(m_duration, world);
    const QVector3D rootEnd = world[size_t(m_hips)].map(QVector3D(0, 0, 0));
    const QVector3D rootMean = rootSum / float(kSamples);
    // horizontal drift over the cycle (root motion), removed linearly
    m_rootXZ = { QVector3D(rootMean.x(), 0, rootMean.z()), QVector3D(rootEnd.x() - rootStart.x(), 0, rootEnd.z() - rootStart.z()) };

    const float scale = kHeightM / height;
    m_fix.setToIdentity();
    m_fix.scale(scale);
    m_fix.rotate(-yaw, 0, 1, 0);
    m_fix.translate(0, -ground, 0);

    colorize();
}

namespace {
enum Region { Shirt, Shorts, Skin, Hair, Sock, Shoe, Vent };
enum Group { Torso, Legs, ArmLeft, ArmRight, Head };
const QVector3D kPalette[] = { { .13f, .38f, .85f },   // shirt
                               { .07f, .08f, .12f },   // shorts
                               { .84f, .64f, .51f },   // skin
                               { .84f, .64f, .51f },   // hair: skin coloured (blends in)
                               { .93f, .94f, .95f },   // socks
                               { .15f, .55f, .95f },   // shoes
                               { .84f, .64f, .51f } }; // (vents: cyclist only)
// road cyclist: blue jersey, black bib shorts, white socks and shoes
const QVector3D kCyclistPalette[] = { { .13f, .40f, .80f },   // jersey
                                      { .05f, .05f, .06f },   // bib shorts
                                      { .84f, .64f, .51f },   // skin
                                      { .80f, .08f, .07f },   // hair: the helmet (red), painted on the head
                                      { .95f, .95f, .96f },   // socks
                                      { .88f, .89f, .91f },   // shoes
                                      { .06f, .06f, .07f } }; // helmet vents
// sculler: a blue sleeveless unisuit to above the knee, white socks, grey shoes
const QVector3D kRowerPalette[] = { { .10f, .33f, .78f },   // unisuit (top)
                                    { .10f, .33f, .78f },   // unisuit (legs)
                                    { .84f, .64f, .51f },   // skin
                                    { .84f, .64f, .51f },   // hair: skin coloured (no cap line across the face)
                                    { .93f, .94f, .95f },   // socks
                                    { .30f, .31f, .33f },   // shoes
                                    { .84f, .64f, .51f } };
} // namespace

void GlbModel::colorize()
{
    // Colours per body part. The coarse part (torso, legs, an arm, the head)
    // comes from a vertex's dominant joint; the visible cuts (waist band,
    // shorts hem, socks, shoes, sleeves, hair) are planes / positions along a
    // bone in the BIND pose (raw vertex positions and the joints' bind
    // positions, inverse(IBM)). Triangles a cut runs through are split exactly
    // on the cut, so the edges of the clothes are straight lines instead of
    // following the zigzag of the mesh's triangle edges.
    auto bindPos = [&](const char* name) -> std::optional<QVector3D> {
        for (size_t j = 0; j < m_joints.size(); ++j)
            if (shortName(m_nodes[size_t(m_joints[j])].name) == QLatin1String(name))
                return m_ibm[j].inverted().map(QVector3D(0, 0, 0));
        return std::nullopt;
    };
    const auto hips = bindPos("hips"), spine = bindPos("spine"), neck = bindPos("neck");
    const auto head = bindPos("head"), headTop = bindPos("headtop_end");
    const std::optional<QVector3D> hip[2] = { bindPos("leftupleg"), bindPos("rightupleg") };
    const std::optional<QVector3D> knee[2] = { bindPos("leftleg"), bindPos("rightleg") };
    const std::optional<QVector3D> ankle[2] = { bindPos("leftfoot"), bindPos("rightfoot") };
    const std::optional<QVector3D> shoulder[2] = { bindPos("leftarm"), bindPos("rightarm") };
    const std::optional<QVector3D> elbow[2] = { bindPos("leftforearm"), bindPos("rightforearm") };

    // bind-pose axes: up (hips -> head), side (right hip -> left hip)
    QVector3D up(0, 1, 0), side(1, 0, 0);
    const QVector3D root = hips ? *hips : QVector3D(0, 0, 0);
    if (hips && (head || neck))
        up = ((head ? *head : *neck) - *hips).normalized();
    if (hip[0] && hip[1])
        side = (*hip[0] - *hip[1]).normalized();
    const QVector3D fwdAxis = QVector3D::crossProduct(side, up).normalized();
    // waist band: half way from the hips joint to the spine joint
    const float waist = spine ? QVector3D::dotProduct(*spine - root, up) * 0.5f : 0.0f;

    // where the clothes end: shorts hem (along the thigh), sock top (along the
    // shin), sleeve (along the upper arm)
    const bool cyclist = m_outfit == Outfit::Cyclist, rower = m_outfit == Outfit::Rower;
    const float hem = cyclist ? .74f : rower ? .62f : .42f, sock = cyclist ? .80f : rower ? .86f : .82f;
    const float sleeve = cyclist ? .50f : rower ? -1.f : .95f;   // (rower: bare arms)
    const float hairCut = cyclist ? .46f : .62f;   // cyclist: the helmet's line (centre)
    const float headR = (neck && (headTop || head)) ? std::max(1e-6f, ((headTop ? *headTop : *head) - *neck).length() * .5f) : 1.f;
    const QVector3D* palette = cyclist ? kCyclistPalette : rower ? kRowerPalette : kPalette;
    auto along = [](const QVector3D& p, const std::optional<QVector3D>& a, const std::optional<QVector3D>& b) {
        if (!a || !b)
            return 0.5f;
        const QVector3D d = *b - *a;
        return QVector3D::dotProduct(p - *a, d) / std::max(1e-9f, d.lengthSquared());
    };
    auto regionAt = [&](int group, const QVector3D& p) -> int {
        const float h = QVector3D::dotProduct(p - root, up);
        switch (group) {
        case Legs: {
            if (h >= waist)
                return Shirt;
            const int s = QVector3D::dotProduct(p - root, side) >= 0 ? 0 : 1;   // 0 = left leg
            if (along(p, hip[s], knee[s]) < hem)
                return Shorts;
            const float shin = along(p, knee[s], ankle[s]);
            return shin >= .98f ? Shoe : shin > sock ? Sock : Skin;
        }
        case ArmLeft:
        case ArmRight: {
            const int s = group == ArmLeft ? 0 : 1;
            return along(p, shoulder[s], elbow[s]) < sleeve ? Shirt : Skin;
        }
        case Head: {
            const float a = along(p, neck, headTop ? headTop : head);
            // cyclist: the helmet's rim is one straight, slanted line - at the
            // brow in front, low over the nape at the back
            float cut = hairCut;
            if (cyclist && head) {
                const float f = std::clamp(QVector3D::dotProduct(p - *head, fwdAxis) / headR, -1.f, 1.f);
                // ...and rising over the ears (they stay skin)
                const float lat = std::clamp(std::abs(QVector3D::dotProduct(p - *head, side)) / headR, 0.f, 1.f);
                cut = hairCut + .13f * f + .26f * lat * lat * std::max(0.f, 1.f - .6f * std::abs(f));
            }
            if (a <= cut)
                return Skin;
            // cyclist: black vent stripes over the top of the helmet
            if (cyclist && a > hairCut + .18f) {
                const QVector3D d = p - (head ? *head : root);
                const float ang = std::atan2(QVector3D::dotProduct(d, side), QVector3D::dotProduct(d, fwdAxis));
                if (std::abs(std::sin(ang * 5.f)) < .22f)
                    return Vent;
            }
            return Hair;
        }
        default:
            return h < waist ? Shorts : Shirt;
        }
    };

    // the coarse part of every vertex
    std::vector<uint8_t> group(m_pos.size());
    for (size_t i = 0; i < m_pos.size(); ++i) {
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (m_wgt[i][size_t(k)] > m_wgt[i][size_t(best)])
                best = k;
        const QString n = shortName(m_nodes[size_t(m_joints[m_jnt[i][size_t(best)]])].name);
        Group g = Torso;
        if (n == QLatin1String("hips") || n.contains(QLatin1String("leg")) || n.contains(QLatin1String("foot"))
            || n.contains(QLatin1String("toe")))
            g = Legs;
        else if (n.contains(QLatin1String("arm")) || n.contains(QLatin1String("hand")))
            g = n.startsWith(QLatin1String("right")) ? ArmRight : ArmLeft;
        else if (n.contains(QLatin1String("head")) || n == QLatin1String("neck"))
            g = Head;
        group[i] = uint8_t(g);
    }
    std::vector<int> region(m_pos.size());
    m_color.resize(m_pos.size());
    for (size_t i = 0; i < m_pos.size(); ++i) {
        region[i] = regionAt(group[i], m_pos[i]);
        m_color[i] = palette[region[i]];
    }
    // the helmet (cyclist): its vertices stand a little off the head
    m_helmet.assign(m_pos.size(), 0);
    if (cyclist)
        for (size_t i = 0; i < m_pos.size(); ++i)
            m_helmet[i] = region[i] == Hair || region[i] == Vent;

    // --- split the triangles the cuts run through ---------------------------
    // A point of a triangle (a, b, c): barycentric weights. Its part is the
    // part of the corner with the largest weight.
    using Bary = QVector3D;
    std::vector<uint32_t> out;
    out.reserve(m_indices.size() + m_indices.size() / 4);
    m_extra.clear();
    const uint32_t base = uint32_t(m_pos.size());
    for (size_t t = 0; t + 2 < m_indices.size(); t += 3) {
        const uint32_t v[3] = { m_indices[t], m_indices[t + 1], m_indices[t + 2] };
        if (region[v[0]] == region[v[1]] && region[v[1]] == region[v[2]]) {
            out.insert(out.end(), v, v + 3);
            continue;
        }
        auto regionOf = [&](const Bary& w) {
            const int k = w[0] >= w[1] && w[0] >= w[2] ? 0 : w[1] >= w[2] ? 1 : 2;
            const QVector3D p = m_pos[v[0]] * w[0] + m_pos[v[1]] * w[1] + m_pos[v[2]] * w[2];
            return regionAt(group[v[k]], p);
        };
        // a vertex of the output: an original corner (its own colour) or a new
        // point with the colour of the side it belongs to
        auto vertex = [&](const Bary& w, int reg) -> uint32_t {
            for (int k = 0; k < 3; ++k)
                if (w[k] > 1.f - 1e-6f && region[v[k]] == reg)
                    return v[k];
            m_extra.push_back({ { v[0], v[1], v[2] }, { w[0], w[1], w[2] }, palette[reg] });
            return base + uint32_t(m_extra.size() - 1);
        };
        // where the region changes between two points (bisection)
        auto cut = [&](const Bary& a, const Bary& b, int ra) {
            float lo = 0, hi = 1;
            for (int it = 0; it < 18; ++it) {
                const float mid = (lo + hi) * .5f;
                (regionOf(a * (1 - mid) + b * mid) == ra ? lo : hi) = mid;
            }
            const float m = (lo + hi) * .5f;
            return a * (1 - m) + b * m;
        };
        std::function<void(const Bary&, const Bary&, const Bary&, int)> split =
            [&](const Bary& a, const Bary& b, const Bary& c, int depth) {
            const int ra = regionOf(a), rb = regionOf(b), rc = regionOf(c);
            if (ra == rb && rb == rc) {
                out.push_back(vertex(a, ra)); out.push_back(vertex(b, ra)); out.push_back(vertex(c, ra));
                return;
            }
            if (ra != rb && rb != rc && ra != rc) {
                // three parts meet here: smaller triangles
                if (depth < 4) {
                    const Bary ab = (a + b) * .5f, bc = (b + c) * .5f, ca = (c + a) * .5f;
                    split(a, ab, ca, depth + 1); split(ab, b, bc, depth + 1);
                    split(ca, bc, c, depth + 1); split(ab, bc, ca, depth + 1);
                } else {
                    const int r = regionOf((a + b + c) / 3.f);
                    out.push_back(vertex(a, r)); out.push_back(vertex(b, r)); out.push_back(vertex(c, r));
                }
                return;
            }
            // two parts: the corner on its own (o) against the other two (p, q),
            // kept in the triangle's winding order
            const Bary* o = &a; const Bary* p = &b; const Bary* q = &c;
            if (rb != ra && rb != rc) { o = &b; p = &c; q = &a; }
            else if (rc != ra && rc != rb) { o = &c; p = &a; q = &b; }
            const int ro = regionOf(*o), rp = regionOf(*p);
            const Bary cp = cut(*o, *p, ro), cq = cut(*o, *q, ro);
            out.push_back(vertex(*o, ro)); out.push_back(vertex(cp, ro)); out.push_back(vertex(cq, ro));
            out.push_back(vertex(cp, rp)); out.push_back(vertex(*p, rp)); out.push_back(vertex(*q, rp));
            out.push_back(vertex(cp, rp)); out.push_back(vertex(*q, rp)); out.push_back(vertex(cq, rp));
        };
        split(Bary(1, 0, 0), Bary(0, 1, 0), Bary(0, 0, 1), 0);
    }
    m_indices.swap(out);
}

void GlbModel::pose(double phase, std::vector<GhostVertex>& out) const
{
    phase -= std::floor(phase);
    const double t = phase * m_duration;
    std::vector<QVector3D> pos, nrm;
    skin(t, pos, &nrm);
    QMatrix4x4 m = m_fix;
    const QVector3D drift = m_rootXZ.size() == 2 ? m_rootXZ[0] + m_rootXZ[1] * float(phase - 0.5) : QVector3D();
    m.translate(-drift);
    out.resize(pos.size() + m_extra.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        QVector3D n = m.mapVector(nrm[i]);
        out[i] = { m.map(pos[i]), n.lengthSquared() > 0 ? n.normalized() : QVector3D(0, 1, 0), m_color[i] };
    }
    // the points on the colour cuts: blends of their triangle's corners
    for (size_t e = 0; e < m_extra.size(); ++e) {
        const Extra& x = m_extra[e];
        const GhostVertex& a = out[x.v[0]]; const GhostVertex& b = out[x.v[1]]; const GhostVertex& c = out[x.v[2]];
        const QVector3D n = a.normal * x.w[0] + b.normal * x.w[1] + c.normal * x.w[2];
        out[pos.size() + e] = { a.position * x.w[0] + b.position * x.w[1] + c.position * x.w[2],
                                n.lengthSquared() > 0 ? n.normalized() : a.normal, x.color };
    }
}


// ---------------------------------------------------------------------------
// On the bike
// ---------------------------------------------------------------------------
namespace {
// Two-bone IK: the middle joint between root and end, bending towards pole.
QVector3D ikJoint(const QVector3D& root, const QVector3D& end, float upper, float lower, const QVector3D& pole)
{
    const QVector3D delta = end - root;
    const float d = std::clamp(delta.length(), .01f, upper + lower - .001f);
    const QVector3D along = delta.normalized();
    const float x = (upper * upper - lower * lower + d * d) / (2 * d);
    const float h = std::sqrt(std::max(0.f, upper * upper - x * x));
    QVector3D out = pole - along * QVector3D::dotProduct(pole, along);
    if (out.lengthSquared() < 1e-6f)
        out = QVector3D(0, 0, 1);
    return root + along * x + out.normalized() * h;
}
QQuaternion pitch(double deg) { return QQuaternion::fromAxisAndAngle(1, 0, 0, float(deg)); }
} // namespace

void GlbModel::prepareRider()
{
    m_canRide = false;
    const size_t J = m_joints.size();
    if (J == 0 || m_pos.empty())
        return;
    std::vector<int> nodeToJoint(m_nodes.size(), -1);
    for (size_t j = 0; j < J; ++j)
        nodeToJoint[size_t(m_joints[j])] = int(j);
    m_jparent.assign(J, -1);
    for (size_t j = 0; j < J; ++j) {
        int n = m_nodes[size_t(m_joints[j])].parent;
        while (n >= 0 && nodeToJoint[size_t(n)] < 0)
            n = m_nodes[size_t(n)].parent;
        m_jparent[j] = n >= 0 ? nodeToJoint[size_t(n)] : -1;
    }
    m_jorder.clear();
    for (int n : m_order)
        if (nodeToJoint[size_t(n)] >= 0)
            m_jorder.push_back(nodeToJoint[size_t(n)]);

    // names -> roles
    std::vector<QVector3D> bind(J);
    std::vector<QString> names(J);
    for (size_t j = 0; j < J; ++j) {
        bind[j] = m_ibm[j].inverted().map(QVector3D(0, 0, 0));
        names[j] = shortName(m_nodes[size_t(m_joints[j])].name);
    }
    auto find = [&](const QString& name) {
        for (size_t j = 0; j < J; ++j)
            if (names[j] == name)
                return int(j);
        return -1;
    };
    m_jHips = find(QStringLiteral("hips"));
    m_jHead = find(QStringLiteral("head"));
    const int neck = find(QStringLiteral("neck"));
    const char* sideName[2] = { "left", "right" };
    int middle[2];
    for (int s = 0; s < 2; ++s) {
        const QString pre = QLatin1String(sideName[s]);
        m_jUpLeg[s] = find(pre + QLatin1String("upleg"));
        m_jLeg[s] = find(pre + QLatin1String("leg"));
        m_jFoot[s] = find(pre + QLatin1String("foot"));
        m_jToe[s] = find(pre + QLatin1String("toebase"));
        m_jArm[s] = find(pre + QLatin1String("arm"));
        m_jForeArm[s] = find(pre + QLatin1String("forearm"));
        m_jHand[s] = find(pre + QLatin1String("hand"));
        middle[s] = find(pre + QLatin1String("handmiddle1"));
        for (int j : { m_jUpLeg[s], m_jLeg[s], m_jFoot[s], m_jToe[s], m_jArm[s], m_jForeArm[s], m_jHand[s] })
            if (j < 0)
                return;
    }
    if (m_jHips < 0 || m_jHead < 0)
        return;

    // the bike frame: x = right, y = up, z = forward; hips at the origin;
    // scaled to kRiderLegM legs
    const QVector3D hips = bind[size_t(m_jHips)];
    const QVector3D up = (bind[size_t(neck >= 0 ? neck : m_jHead)] - hips).normalized();
    QVector3D right = bind[size_t(m_jUpLeg[1])] - bind[size_t(m_jUpLeg[0])];
    right = (right - up * QVector3D::dotProduct(right, up)).normalized();
    const QVector3D fwd = QVector3D::crossProduct(up, right).normalized();
    float legBind = 0;
    for (int s = 0; s < 2; ++s)
        legBind += (bind[size_t(m_jLeg[s])] - bind[size_t(m_jUpLeg[s])]).length()
                   + (bind[size_t(m_jFoot[s])] - bind[size_t(m_jLeg[s])]).length();
    legBind *= .5f;
    if (legBind <= 0 || up.lengthSquared() < .5f || right.lengthSquared() < .5f)
        return;
    const float scale = kRiderLegM / legBind;
    auto toBike = [&](const QVector3D& v) {
        const QVector3D d = v - hips;
        return QVector3D(QVector3D::dotProduct(d, right), QVector3D::dotProduct(d, up), QVector3D::dotProduct(d, fwd)) * scale;
    };
    auto dirToBike = [&](const QVector3D& n) {
        return QVector3D(QVector3D::dotProduct(n, right), QVector3D::dotProduct(n, up), QVector3D::dotProduct(n, fwd));
    };
    m_bf.resize(m_pos.size());
    m_nf.resize(m_pos.size());
    m_soleY = 1e30f;
    for (size_t i = 0; i < m_pos.size(); ++i) {
        m_bf[i] = toBike(m_pos[i]);
        m_nf[i] = dirToBike(i < m_nrm.size() ? m_nrm[i] : QVector3D(0, 1, 0));
        m_soleY = std::min(m_soleY, m_bf[i].y());
    }
    // a thin helmet shell: helmet vertices 8 mm out along their normals
    for (size_t i = 0; i < m_bf.size() && i < m_helmet.size(); ++i)
        if (m_helmet[i] && m_nf[i].lengthSquared() > 0)
            m_bf[i] += m_nf[i].normalized() * .008f;
    m_jf.resize(J);
    for (size_t j = 0; j < J; ++j)
        m_jf[j] = toBike(bind[j]);

    // roles and bone directions
    m_jinfo.assign(J, {});
    m_jdir.assign(J, QVector3D(0, 1, 0));
    std::vector<int> firstChild(J, -1);
    for (int j : m_jorder)
        if (m_jparent[size_t(j)] >= 0 && firstChild[size_t(m_jparent[size_t(j)])] < 0)
            firstChild[size_t(m_jparent[size_t(j)])] = j;
    for (size_t j = 0; j < J; ++j) {
        const QString& n = names[j];
        JointInfo& info = m_jinfo[j];
        info.side = n.startsWith(QLatin1String("right")) ? 1 : 0;
        if (int(j) == m_jHips) info.role = Role::Hips;
        else if (n == QLatin1String("spine")) info.role = Role::Spine;
        else if (n == QLatin1String("spine1")) info.role = Role::Spine1;
        else if (n == QLatin1String("spine2")) info.role = Role::Spine2;
        else if (int(j) == neck) info.role = Role::Neck;
        else if (int(j) == m_jHead) info.role = Role::Head;
        else if (int(j) == m_jUpLeg[info.side]) info.role = Role::UpLeg;
        else if (int(j) == m_jLeg[info.side]) info.role = Role::Leg;
        else if (int(j) == m_jFoot[info.side]) info.role = Role::Foot;
        else if (int(j) == m_jArm[info.side]) info.role = Role::Arm;
        else if (int(j) == m_jForeArm[info.side]) info.role = Role::ForeArm;
        else if (int(j) == m_jHand[info.side]) info.role = Role::Hand;
        else if (n.contains(QLatin1String("hand")) && !n.isEmpty() && n.back().isDigit()) {
            const int digit = n.back().digitValue();
            if (digit >= 1 && digit <= 3) {
                info.role = Role::Finger;
                info.digit = digit;
                info.thumb = n.contains(QLatin1String("thumb"));
            }
        }
        int child = firstChild[j];
        if (info.role == Role::Hand && middle[info.side] >= 0)
            child = middle[info.side];
        if (child >= 0 && (m_jf[size_t(child)] - m_jf[j]).lengthSquared() > 1e-10f)
            m_jdir[j] = (m_jf[size_t(child)] - m_jf[j]).normalized();
    }

    // the helmet: around the head's vertices (dominant joint: the head)
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    int headVerts = 0;
    for (size_t i = 0; i < m_pos.size(); ++i) {
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (m_wgt[i][size_t(k)] > m_wgt[i][size_t(best)])
                best = k;
        const int j = m_jnt[i][size_t(best)];
        if (j != m_jHead && !names[size_t(j)].startsWith(QLatin1String("headtop")))
            continue;
        ++headVerts;
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], m_bf[i][a]);
            hi[a] = std::max(hi[a], m_bf[i][a]);
        }
    }
    if (headVerts < 20)
        return;
    const QVector3D half = (hi - lo) * .5f, center = (hi + lo) * .5f;
    // (an aero road helmet: wider and longer than the head, sitting high)
    // (kept close to the head: a big shell stands out against the sky at the back)
    m_helmetRadii = QVector3D(half.x() * 1.12f, half.y() * .80f, half.z() * 1.18f);
    m_helmetOffset = center + QVector3D(0, half.y() * .36f, -half.z() * .06f) - m_jf[size_t(m_jHead)];
    m_canRide = true;
}

void GlbModel::solveBike(const BikeFit& fit, double torsoDeg, std::vector<QVector3D>& P, std::vector<QQuaternion>& Q,
                         const QVector3D ankle[2], const QQuaternion foot[2], const QVector3D wrist[2],
                         const QVector3D& hipCenter) const
{
    const size_t J = m_jf.size();
    P.assign(J, QVector3D());
    Q.assign(J, QQuaternion());
    const double headDeg = 22.0;   // the head stays up, looking down the road
    // a slight rock from side to side with the pedal stroke: towards the leg
    // pushing down (the right one at crank pi/2); the head stays level
    const float sway = float(-fit.swayDeg * std::sin(fit.crank));
    auto lean = [&](double deg, float part) {
        return QQuaternion::fromAxisAndAngle(0, 0, 1, sway * part) * pitch(deg);
    };
    const QQuaternion hipsQ = lean(torsoDeg * .80, 1.f);
    const QVector3D hipBind = (m_jf[size_t(m_jUpLeg[0])] + m_jf[size_t(m_jUpLeg[1])]) * .5f;
    for (int j : m_jorder) {
        const int p = m_jparent[size_t(j)];
        const QQuaternion qp = p >= 0 ? Q[size_t(p)] : QQuaternion();
        const JointInfo& info = m_jinfo[size_t(j)];
        if (info.role == Role::Hips)
            P[size_t(j)] = hipCenter - hipsQ.rotatedVector(hipBind - m_jf[size_t(j)]);
        else
            P[size_t(j)] = p >= 0 ? P[size_t(p)] + qp.rotatedVector(m_jf[size_t(j)] - m_jf[size_t(p)]) : m_jf[size_t(j)];
        const QVector3D& pj = P[size_t(j)];
        const int s = info.side;
        const float side = s == 0 ? -1.f : 1.f;
        auto aim = [&](const QVector3D& target) {
            const QVector3D want = target - pj;
            if (want.lengthSquared() < 1e-10f)
                return qp;
            return QQuaternion::rotationTo(qp.rotatedVector(m_jdir[size_t(j)]), want.normalized()) * qp;
        };
        auto length = [&](int a, int b) { return (m_jf[size_t(b)] - m_jf[size_t(a)]).length(); };
        // roll about the bone so the palm (down in the T pose) faces the
        // centre of the bike, a little down: the grip on the brake hoods
        // (thumb up, little finger down). fraction: part of the roll (the
        // forearm takes half, like a real forearm turning)
        auto twist = [&](const QQuaternion& q0, float fraction) {
            const QVector3D axis = q0.rotatedVector(m_jdir[size_t(j)]).normalized();
            auto flat = [&](QVector3D v) { return (v - axis * QVector3D::dotProduct(v, axis)); };
            const QVector3D have = flat(q0.rotatedVector(QVector3D(0, -1, 0)));
            const QVector3D want = flat(QVector3D(-side * .45f, -1.f, 0));   // palm on the hood
            if (have.lengthSquared() < 1e-6f || want.lengthSquared() < 1e-6f)
                return q0;
            const QVector3D h = have.normalized(), w = want.normalized();
            const float ang = std::atan2(QVector3D::dotProduct(QVector3D::crossProduct(h, w), axis),
                                         QVector3D::dotProduct(h, w)) * 180.f / float(M_PI);
            return QQuaternion::fromAxisAndAngle(axis, ang * fraction) * q0;
        };
        QQuaternion q = qp;
        switch (info.role) {
        case Role::Hips: q = hipsQ; break;
        case Role::Spine: q = lean(torsoDeg * .90, .85f); break;
        case Role::Spine1: q = lean(torsoDeg * .96, .65f); break;
        case Role::Spine2: q = lean(torsoDeg, .45f); break;
        case Role::Neck: q = lean((torsoDeg + headDeg) * .5, .2f); break;
        case Role::Head: q = pitch(headDeg); break;
        case Role::UpLeg: {
            const QVector3D knee = ikJoint(pj, ankle[s], length(m_jUpLeg[s], m_jLeg[s]), length(m_jLeg[s], m_jFoot[s]),
                                           QVector3D(side * .08f, 0, 1));
            q = aim(knee);
            break;
        }
        case Role::Leg: q = aim(ankle[s]); break;
        case Role::Foot: q = foot[s]; break;
        case Role::Arm: {
            const QVector3D elbow = ikJoint(pj, wrist[s], length(m_jArm[s], m_jForeArm[s]),
                                            length(m_jForeArm[s], m_jHand[s]), QVector3D(side * .20f, -.45f, -.25f));
            q = aim(elbow);
            break;
        }
        case Role::ForeArm: q = twist(aim(wrist[s]), .5f); break;
        case Role::Hand: q = twist(aim(fit.hood[s] + QVector3D(0, -.012f, .040f)), 1.f); break;
        case Role::Finger: {
            // curl towards the palm (palms face down in the T pose)
            const QVector3D axis = QVector3D::crossProduct(m_jdir[size_t(j)], QVector3D(0, -1, 0));
            if (axis.lengthSquared() > 1e-6f) {
                // (fingers wrap round the hood; the thumb round its inside)
                const float deg = info.thumb ? (info.digit == 1 ? 55.f : 45.f)
                                             : (info.digit == 1 ? 75.f : info.digit == 2 ? 80.f : 50.f);
                q = QQuaternion::fromAxisAndAngle(qp.rotatedVector(axis.normalized()), deg) * qp;
            }
            break;
        }
        default: break;
        }
        Q[size_t(j)] = q;
    }
}

void GlbModel::poseOnBike(const BikeFit& fit, std::vector<GhostVertex>& out, HeadPose* head) const
{
    if (!m_canRide)
        return;
    // the feet on the pedals: the ball of the foot (toe base) over the axle,
    // heels dropping a little at the bottom of the stroke
    auto pedalAt = [&](int s, double phase, QVector3D* ankle, QQuaternion* footQ) {
        const float side = s == 0 ? -1.f : 1.f;
        const QVector3D pedal = fit.bottomBracket
                                + QVector3D(side * fit.pedalX, float(fit.crankLength * std::cos(phase)),
                                            float(fit.crankLength * std::sin(phase)));
        const double ankling = .12 - .10 * std::cos(phase + .6);   // rad, toes down at the back
        *footQ = pitch(ankling * 180.0 / M_PI);
        const QVector3D toe = m_jf[size_t(m_jToe[s])], foot = m_jf[size_t(m_jFoot[s])];
        *ankle = pedal + QVector3D(0, .012f + (toe.y() - m_soleY), 0) + footQ->rotatedVector(foot - toe);
    };
    QVector3D ankle[2];
    QQuaternion footQ[2];
    for (int s = 0; s < 2; ++s)
        pedalAt(s, fit.crank + (s == 0 ? M_PI : 0.0), &ankle[s], &footQ[s]);
    // the hips: as high as the legs reach at the bottom of the stroke (the
    // knee still a little bent), above the saddle
    QVector3D bottom;
    QQuaternion unused;
    pedalAt(1, M_PI, &bottom, &unused);
    const float hipZ = fit.saddleTop.z() + .01f;
    const float reach = kRiderLegM * .95f, dz = hipZ - bottom.z();
    const float hipY = bottom.y() + std::sqrt(std::max(.01f, reach * reach - dz * dz));
    const QVector3D hipCenter(0, std::max(hipY, fit.saddleTop.y() + .02f), hipZ);
    // the grip on the brake hoods: the palm lies on top of the hood (the
    // wrist behind it), the knuckles just past its front and the fingers
    // curl down around it
    QVector3D wrist[2];
    for (int s = 0; s < 2; ++s)
        wrist[s] = fit.hood[s] + QVector3D(0, .000f, -.070f);   // (the hood may pass through the hand)

    // the back leans until the arms reach the hoods comfortably (elbows bent)
    std::vector<QVector3D> P;
    std::vector<QQuaternion> Q;
    const float arm = (m_jf[size_t(m_jForeArm[0])] - m_jf[size_t(m_jArm[0])]).length()
                      + (m_jf[size_t(m_jHand[0])] - m_jf[size_t(m_jForeArm[0])]).length();
    double best = 45, bestErr = 1e30;
    for (double deg = 20; deg <= 92; deg += 2) {
        solveBike(fit, deg, P, Q, ankle, footQ, wrist, hipCenter);
        double err = 0;
        for (int s = 0; s < 2; ++s)
            err += std::abs((P[size_t(m_jArm[s])] - wrist[s]).length() - arm * .97f);   // arms nearly straight (this figure has short arms)
        if (err < bestErr) {
            bestErr = err;
            best = deg;
        }
    }
    solveBike(fit, best, P, Q, ankle, footQ, wrist, hipCenter);

    skinPosed(P, Q, out);
    if (head) {
        const QQuaternion& qh = Q[size_t(m_jHead)];
        head->helmetCenter = P[size_t(m_jHead)] + qh.rotatedVector(m_helmetOffset);
        head->helmetRadii = m_helmetRadii;
        head->up = qh.rotatedVector(QVector3D(0, 1, 0));
        head->forward = qh.rotatedVector(QVector3D(0, 0, 1));
    }
}

void GlbModel::skinPosed(const std::vector<QVector3D>& P, const std::vector<QQuaternion>& Q, std::vector<GhostVertex>& out) const
{
    // skinning: v = sum w * (P_j + Q_j (v_bind - joint_bind))
    std::vector<QMatrix3x3> R(Q.size());
    for (size_t j = 0; j < Q.size(); ++j)
        R[j] = Q[j].toRotationMatrix();
    auto rot = [](const QMatrix3x3& m, const QVector3D& v) {
        return QVector3D(m(0, 0) * v.x() + m(0, 1) * v.y() + m(0, 2) * v.z(),
                         m(1, 0) * v.x() + m(1, 1) * v.y() + m(1, 2) * v.z(),
                         m(2, 0) * v.x() + m(2, 1) * v.y() + m(2, 2) * v.z());
    };
    out.resize(m_bf.size() + m_extra.size());
    for (size_t i = 0; i < m_bf.size(); ++i) {
        QVector3D p(0, 0, 0), n(0, 0, 0);
        for (int k = 0; k < 4; ++k) {
            const float w = m_wgt[i][size_t(k)];
            if (w <= 0)
                continue;
            const size_t j = m_jnt[i][size_t(k)];
            p += w * (P[j] + rot(R[j], m_bf[i] - m_jf[j]));
            n += w * rot(R[j], m_nf[i]);
        }
        out[i] = { p, n.lengthSquared() > 0 ? n.normalized() : QVector3D(0, 1, 0), m_color[i] };
    }
    for (size_t e = 0; e < m_extra.size(); ++e) {
        const Extra& x = m_extra[e];
        const GhostVertex& a = out[x.v[0]]; const GhostVertex& b = out[x.v[1]]; const GhostVertex& c = out[x.v[2]];
        const QVector3D n = a.normal * x.w[0] + b.normal * x.w[1] + c.normal * x.w[2];
        out[m_bf.size() + e] = { a.position * x.w[0] + b.position * x.w[1] + c.position * x.w[2],
                                 n.lengthSquared() > 0 ? n.normalized() : a.normal, x.color };
    }
}

// --- in a rowing boat ---------------------------------------------------------------------------

void GlbModel::solveRow(const RowFit& fit, double torsoDeg, std::vector<QVector3D>& P, std::vector<QQuaternion>& Q) const
{
    const size_t J = m_jf.size();
    P.assign(J, QVector3D());
    Q.assign(J, QQuaternion());
    // a long, straight back: the pelvis turns with the trunk (a little more,
    // flattening the model's hollow back), so the lower back does not arch
    // and the belly does not push out towards the knees
    const QQuaternion hipsQ = pitch(torsoDeg - 8);
    const QVector3D hipBind = (m_jf[size_t(m_jUpLeg[0])] + m_jf[size_t(m_jUpLeg[1])]) * .5f;
    for (int j : m_jorder) {
        const int p = m_jparent[size_t(j)];
        const QQuaternion qp = p >= 0 ? Q[size_t(p)] : QQuaternion();
        const JointInfo& info = m_jinfo[size_t(j)];
        if (info.role == Role::Hips)
            P[size_t(j)] = fit.hipCenter - hipsQ.rotatedVector(hipBind - m_jf[size_t(j)]);
        else
            P[size_t(j)] = p >= 0 ? P[size_t(p)] + qp.rotatedVector(m_jf[size_t(j)] - m_jf[size_t(p)]) : m_jf[size_t(j)];
        const QVector3D& pj = P[size_t(j)];
        const int s = info.side;
        const float side = s == 0 ? -1.f : 1.f;
        auto aim = [&](const QVector3D& target) {
            const QVector3D want = target - pj;
            if (want.lengthSquared() < 1e-10f)
                return qp;
            return QQuaternion::rotationTo(qp.rotatedVector(m_jdir[size_t(j)]), want.normalized()) * qp;
        };
        auto length = [&](int a, int b) { return (m_jf[size_t(b)] - m_jf[size_t(a)]).length(); };
        // overhand grip: the palm down on the handle (rolled about the bone)
        auto palmDown = [&](const QQuaternion& q0, float fraction) {
            const QVector3D axis = q0.rotatedVector(m_jdir[size_t(j)]).normalized();
            auto flat = [&](QVector3D v) { return v - axis * QVector3D::dotProduct(v, axis); };
            const QVector3D have = flat(q0.rotatedVector(QVector3D(0, -1, 0)));
            const QVector3D want = flat(QVector3D(0, -1, .15f));
            if (have.lengthSquared() < 1e-6f || want.lengthSquared() < 1e-6f)
                return q0;
            const QVector3D h = have.normalized(), w = want.normalized();
            const float ang = std::atan2(QVector3D::dotProduct(QVector3D::crossProduct(h, w), axis),
                                         QVector3D::dotProduct(h, w)) * 180.f / float(M_PI);
            return QQuaternion::fromAxisAndAngle(axis, ang * fraction) * q0;
        };
        QQuaternion q = qp;
        switch (info.role) {
        case Role::Hips: q = hipsQ; break;
        case Role::Spine: q = pitch(torsoDeg - 5); break;
        case Role::Spine1: q = pitch(torsoDeg - 2); break;
        case Role::Spine2: q = pitch(torsoDeg); break;
        case Role::Neck: q = pitch((torsoDeg + fit.headDeg) * .5); break;
        case Role::Head: q = pitch(fit.headDeg); break;
        case Role::UpLeg: {
            // knees up (and a little out) between the hips and the feet
            const QVector3D knee = ikJoint(pj, fit.ankle[s], length(m_jUpLeg[s], m_jLeg[s]), length(m_jLeg[s], m_jFoot[s]),
                                           QVector3D(side * .22f, 1, .25f));   // (knees up, a little apart)
            q = aim(knee);
            break;
        }
        case Role::Leg: q = aim(fit.ankle[s]); break;
        case Role::Foot: q = pitch(fit.footPitchDeg); break;
        case Role::Arm: {
            // elbows well out to the sides (past the body at the finish, not through it)
            const QVector3D elbow = ikJoint(pj, fit.wrist[s], length(m_jArm[s], m_jForeArm[s]),
                                            length(m_jForeArm[s], m_jHand[s]), QVector3D(side * 1.f, -.35f, -.25f));
            q = aim(elbow);
            break;
        }
        case Role::ForeArm: q = palmDown(aim(fit.wrist[s]), .5f); break;
        case Role::Hand: q = palmDown(aim(fit.wrist[s] + QVector3D(-side * .10f, 0, .02f)), 1.f); break;
        case Role::Finger: {
            const QVector3D axis = QVector3D::crossProduct(m_jdir[size_t(j)], QVector3D(0, -1, 0));
            if (axis.lengthSquared() > 1e-6f) {
                const float deg = info.thumb ? 50.f : (info.digit == 1 ? 70.f : info.digit == 2 ? 80.f : 55.f);
                q = QQuaternion::fromAxisAndAngle(qp.rotatedVector(axis.normalized()), deg) * qp;
            }
            break;
        }
        default: break;
        }
        Q[size_t(j)] = q;
    }
}

void GlbModel::poseRowing(const RowFit& fit, std::vector<GhostVertex>& out) const
{
    if (!m_canRide)
        return;
    std::vector<QVector3D> P;
    std::vector<QQuaternion> Q;
    // the back leans forward until the hands reach the handles (arms nearly straight at most)
    const float arm = (m_jf[size_t(m_jForeArm[0])] - m_jf[size_t(m_jArm[0])]).length()
                      + (m_jf[size_t(m_jHand[0])] - m_jf[size_t(m_jForeArm[0])]).length();
    double lean = fit.torsoDeg;
    for (int k = 0; k < 4; ++k) {   // (at most a little: the thighs meet the chest)
        solveRow(fit, lean, P, Q);
        bool reach = true;
        for (int s = 0; s < 2; ++s)
            reach = reach && (P[size_t(m_jArm[s])] - fit.wrist[s]).length() <= arm * .99f;
        if (reach)
            break;
        lean += 3;
    }
    skinPosed(P, Q, out);
}

// --- StaticModel -------------------------------------------------------------------------------

namespace {

QVector3D vec3(const QJsonValue& v, QVector3D fallback = QVector3D())
{
    const QJsonArray a = v.toArray();
    if (a.size() < 3)
        return fallback;
    return QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}

// linear (glTF) -> the display colours the 3D view's shader works in
float toDisplay(float c)
{
    return std::pow(std::clamp(c, 0.f, 1.f), 1.f / 2.2f);
}

} // namespace

bool StaticModel::loadFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot open %1").arg(path));
    return load(f.readAll(), error);
}

bool StaticModel::load(const QByteArray& glb, QString* error)
{
    *this = StaticModel();
    Reader r;
    if (!parseGlb(glb, r, error))
        return false;

    const QJsonArray nodes = r.json.value(QStringLiteral("nodes")).toArray();
    const QJsonArray meshes = r.json.value(QStringLiteral("meshes")).toArray();
    const QJsonArray materials = r.json.value(QStringLiteral("materials")).toArray();
    // the scene's root nodes (else every node without a parent)
    std::vector<int> roots;
    {
        const QJsonArray scenes = r.json.value(QStringLiteral("scenes")).toArray();
        const int scene = r.json.value(QStringLiteral("scene")).toInt(0);
        if (scene >= 0 && scene < scenes.size())
            for (const QJsonValue v : scenes.at(scene).toObject().value(QStringLiteral("nodes")).toArray())
                roots.push_back(v.toInt(-1));
        if (roots.empty()) {
            std::vector<bool> child(size_t(nodes.size()), false);
            for (const QJsonValue n : nodes)
                for (const QJsonValue c : n.toObject().value(QStringLiteral("children")).toArray())
                    if (c.toInt(-1) >= 0 && c.toInt(-1) < nodes.size())
                        child[size_t(c.toInt())] = true;
            for (int i = 0; i < nodes.size(); ++i)
                if (!child[size_t(i)])
                    roots.push_back(i);
        }
    }
    auto local = [&](const QJsonObject& n) {
        QMatrix4x4 m;
        const QJsonArray a = n.value(QStringLiteral("matrix")).toArray();
        if (a.size() == 16) {
            float v[16];
            for (int k = 0; k < 16; ++k)
                v[k] = float(a[k].toDouble());
            return QMatrix4x4(v).transposed();   // glTF: column-major
        }
        m.translate(vec3(n.value(QStringLiteral("translation"))));
        const QJsonArray q = n.value(QStringLiteral("rotation")).toArray();
        if (q.size() == 4)
            m.rotate(QQuaternion(float(q[3].toDouble()), float(q[0].toDouble()), float(q[1].toDouble()), float(q[2].toDouble())));
        m.scale(vec3(n.value(QStringLiteral("scale")), QVector3D(1, 1, 1)));
        return m;
    };
    QString problem;
    std::vector<int> visited(size_t(nodes.size()), 0);
    constexpr int kMaxDepth = 64;   // scenery is shallow; bounds the recursion
    std::function<void(int, const QMatrix4x4&, int)> walk = [&](int index, const QMatrix4x4& parent, int depth) {
        if (index < 0 || index >= nodes.size() || visited[size_t(index)]++ || depth > kMaxDepth) {
            problem = QStringLiteral("invalid node hierarchy");
            return;
        }
        const QJsonObject n = nodes.at(index).toObject();
        const QMatrix4x4 world = parent * local(n);
        const QMatrix4x4 normalMatrix = world.inverted().transposed();
        const bool mirrored = world.determinant() < 0;
        for (const QJsonValue d : n.value(QStringLiteral("extras")).toObject().value(QStringLiteral("decals")).toArray()) {
            const QJsonObject o = d.toObject();
            const QJsonArray size = o.value(QStringLiteral("size")).toArray();
            Decal decal;
            decal.width = size.size() >= 2 ? float(size[0].toDouble()) : 0.f;
            decal.height = size.size() >= 2 ? float(size[1].toDouble()) : 0.f;
            decal.at = world.map(vec3(o.value(QStringLiteral("at"))));
            decal.normal = normalMatrix.mapVector(vec3(o.value(QStringLiteral("normal")), QVector3D(0, 0, 1))).normalized();
            decal.up = world.mapVector(vec3(o.value(QStringLiteral("up")), QVector3D(0, 1, 0))).normalized();
            if (decal.width > 0 && decal.height > 0)
                m_decals.push_back(decal);
        }
        const int meshIndex = n.value(QStringLiteral("mesh")).toInt(-1);
        if (meshIndex >= 0 && meshIndex < meshes.size()) {
            for (const QJsonValue pv : meshes.at(meshIndex).toObject().value(QStringLiteral("primitives")).toArray()) {
                const QJsonObject prim = pv.toObject();
                if (prim.value(QStringLiteral("mode")).toInt(4) != 4)
                    continue;   // triangles only
                const QJsonObject attr = prim.value(QStringLiteral("attributes")).toObject();
                std::vector<float> P, N, C;
                int pc = 0, nc = 0, cc = 0;
                if (!r.read(attr.value(QStringLiteral("POSITION")).toInt(-1), P, &pc) || pc != 3) {
                    problem = QStringLiteral("a mesh without positions");
                    continue;
                }
                const size_t count = P.size() / 3;
                const bool hasN = attr.contains(QStringLiteral("NORMAL"))
                                  && r.read(attr.value(QStringLiteral("NORMAL")).toInt(), N, &nc) && nc == 3 && N.size() == P.size();
                const bool hasC = attr.contains(QStringLiteral("COLOR_0"))
                                  && r.read(attr.value(QStringLiteral("COLOR_0")).toInt(), C, &cc) && (cc == 3 || cc == 4)
                                  && C.size() == count * size_t(cc);
                QVector3D factor(1, 1, 1);
                const int mi = prim.value(QStringLiteral("material")).toInt(-1);
                if (mi >= 0 && mi < materials.size()) {
                    const QJsonArray f = materials.at(mi).toObject().value(QStringLiteral("pbrMetallicRoughness"))
                                             .toObject().value(QStringLiteral("baseColorFactor")).toArray();
                    if (f.size() >= 3)
                        factor = QVector3D(float(f[0].toDouble()), float(f[1].toDouble()), float(f[2].toDouble()));
                }
                std::vector<uint32_t> idx;
                if (prim.contains(QStringLiteral("indices"))) {
                    if (!r.readIndices(prim.value(QStringLiteral("indices")).toInt(), idx)) {
                        problem = QStringLiteral("unreadable indices");
                        continue;
                    }
                } else {
                    idx.resize(count);
                    for (size_t i = 0; i < count; ++i)
                        idx[i] = uint32_t(i);
                }
                for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                    uint32_t k[3] = { idx[t], idx[t + 1], idx[t + 2] };
                    if (k[0] >= count || k[1] >= count || k[2] >= count)
                        continue;
                    if (mirrored)
                        std::swap(k[1], k[2]);
                    Vertex v[3];
                    for (int c = 0; c < 3; ++c) {
                        const size_t i = k[c];
                        v[c].position = world.map(QVector3D(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]));
                        if (hasN)
                            v[c].normal = normalMatrix.mapVector(QVector3D(N[i * 3], N[i * 3 + 1], N[i * 3 + 2])).normalized();
                        QVector3D col = factor;
                        if (hasC)
                            col *= QVector3D(C[i * cc], C[i * cc + 1], C[i * cc + 2]);
                        v[c].color = QVector3D(toDisplay(col.x()), toDisplay(col.y()), toDisplay(col.z()));
                    }
                    if (!hasN) {
                        const QVector3D fn = QVector3D::normal(v[0].position, v[1].position, v[2].position);
                        for (auto& x : v)
                            x.normal = fn;
                    }
                    for (const auto& x : v)
                        m_triangles.push_back(x);
                }
            }
        }
        for (const QJsonValue c : n.value(QStringLiteral("children")).toArray())
            if (problem.isEmpty())
                walk(c.toInt(-1), world, depth + 1);
    };
    for (int root : roots)
        if (problem.isEmpty())
            walk(root, QMatrix4x4(), 0);
    if (!problem.isEmpty())
        return fail(error, problem);
    if (m_triangles.empty())
        return fail(error, QStringLiteral("no triangles"));
    return true;
}

} // namespace route3d
