#include "glb_model.h"

#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QGenericMatrix>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector2D>
#include <QVector4D>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>

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

bool GlbModel::loadFile(const QString& path, QString* error, Outfit outfit, const GlbModel* motion)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot open %1").arg(path));
    return load(f.readAll(), error, outfit, motion);
}

bool GlbModel::load(const QByteArray& glb, QString* error, Outfit outfit, const GlbModel* motion)
{
    const float legsIn = m_legInDeg, neck = m_neckScale, neckWidth = m_neckWidth;   // (set before the load)
    *this = GlbModel();
    m_legInDeg = legsIn;
    m_neckScale = neck;
    m_neckWidth = neckWidth;
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

    // Avatar: the materials' base colour textures (glTF image index -> m_textures slot)
    const bool avatar = m_outfit == Outfit::Avatar || m_outfit == Outfit::RunnerAvatar || m_outfit == Outfit::RowerAvatar;
    std::vector<int> imageSlot;
    qint64 texturePixels = 0;
    auto textureOf = [&](const QJsonObject& textureInfo) -> int {
        const QJsonArray textures = r.json.value(QStringLiteral("textures")).toArray();
        const QJsonArray images = r.json.value(QStringLiteral("images")).toArray();
        const int ti = textureInfo.value(QStringLiteral("index")).toInt(-1);
        if (ti < 0 || ti >= textures.size())
            return -1;
        const int ii = textures.at(ti).toObject().value(QStringLiteral("source")).toInt(-1);
        if (ii < 0 || ii >= images.size())
            return -1;
        imageSlot.resize(size_t(images.size()), -2);   // -2: not tried yet
        int& slot = imageSlot[size_t(ii)];
        if (slot != -2)
            return slot;
        slot = -1;
        const QJsonObject img = images.at(ii).toObject();
        const QJsonArray views = r.json.value(QStringLiteral("bufferViews")).toArray();
        const int vi = img.value(QStringLiteral("bufferView")).toInt(-1);   // (external files are not supported)
        if (vi < 0 || vi >= views.size())
            return slot;
        const QJsonObject bv = views.at(vi).toObject();
        const qint64 off = bv.value(QStringLiteral("byteOffset")).toInteger(), len = bv.value(QStringLiteral("byteLength")).toInteger(-1);
        if (bv.value(QStringLiteral("buffer")).toInt() != 0 || off < 0 || len <= 0 || off > r.bin.size() || len > r.bin.size() - off)
            return slot;
        // (a broken or hostile file must not allocate without bound: the size
        // is read from the header before anything is decoded - kMaxTexturePx
        // a side, kMaxTexturePixels in all textures together)
        QByteArray bytes = QByteArray::fromRawData(r.bin.constData() + off, qsizetype(len));
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::ReadOnly))
            return slot;
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > kMaxTexturePx || size.height() > kMaxTexturePx
            || texturePixels + qint64(size.width()) * size.height() > kMaxTexturePixels)
            return slot;
        reader.setAllocationLimit(int(qint64(kMaxTexturePx) * kMaxTexturePx * 4 / (1024 * 1024)) + 1);   // MB
        const QImage image = reader.read();
        if (image.isNull() || image.size() != size)
            return slot;
        texturePixels += qint64(size.width()) * size.height();
        m_textures.push_back(image.convertToFormat(QImage::Format_RGB32));
        slot = int(m_textures.size()) - 1;
        return slot;
    };
    // glTF colours are linear: to the display colours the figures use
    auto display = [](double c) { return float(std::pow(std::clamp(c, 0.0, 1.0), 1.0 / 2.2)); };

    // meshes of the skinned nodes
    const QJsonArray meshes = r.json.value(QStringLiteral("meshes")).toArray();
    const QJsonArray materials = r.json.value(QStringLiteral("materials")).toArray();
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
            if (avatar) {
                // the material's base colour (factor x texture at TEXCOORD_n)
                const QJsonObject pbr = materials.at(p.value(QStringLiteral("material")).toInt(-1)).toObject()
                                            .value(QStringLiteral("pbrMetallicRoughness")).toObject();
                const QJsonArray f = pbr.value(QStringLiteral("baseColorFactor")).toArray();
                const QVector3D factor = f.size() >= 3 ? QVector3D(display(f[0].toDouble()), display(f[1].toDouble()), display(f[2].toDouble()))
                                                       : QVector3D(1, 1, 1);
                int tex = -1;
                std::vector<float> T;
                int tc = 0;
                if (pbr.contains(QStringLiteral("baseColorTexture"))) {
                    const QJsonObject info = pbr.value(QStringLiteral("baseColorTexture")).toObject();
                    const QString uvName = QStringLiteral("TEXCOORD_%1").arg(info.value(QStringLiteral("texCoord")).toInt(0));
                    if (r.read(a.value(uvName).toInt(-1), T, &tc) && tc == 2 && T.size() == count * 2)
                        tex = textureOf(info);
                }
                for (size_t i = 0; i < count; ++i) {
                    m_uv.emplace_back(tex >= 0 ? QVector2D(T[i * 2], T[i * 2 + 1]) : QVector2D());
                    m_vertexTexture.push_back(tex);
                    m_baseColor.push_back(factor);
                }
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

    // The animation: the first that keeps the figure upright. (A Mixamo
    // character exported from Blender can carry several clips made for
    // different armature orientations - one of them lays the figure down.)
    const QJsonArray anims = r.json.value(QStringLiteral("animations")).toArray();
    if (anims.isEmpty())
        return fail(error, QStringLiteral("the model has no animation (e.g. a running cycle)"));
    auto readAnimation = [&](const QJsonObject& anim, QString* why) -> bool {
        m_channels.clear();
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
                return fail(why, QStringLiteral("cubic spline animation is not supported - export with linear keys"));
            ch.step = interp == QLatin1String("STEP");
            int ic = 0, oc = 0;
            if (!r.read(s.value(QStringLiteral("input")).toInt(-1), ch.times, &ic) || ic != 1
                || !r.read(s.value(QStringLiteral("output")).toInt(-1), ch.values, &oc) || oc != (ch.path == 1 ? 4 : 3)
                || ch.times.empty() || ch.values.size() != ch.times.size() * size_t(oc))
                return fail(why, QStringLiteral("invalid animation sampler"));
            t0 = std::min(t0, ch.times.front());
            t1 = std::max(t1, ch.times.back());
            m_channels.push_back(std::move(ch));
        }
        if (m_channels.empty() || !(t1 > t0))
            return fail(why, QStringLiteral("the animation has no usable keys"));
        for (Channel& ch : m_channels)
            for (float& t : ch.times)
                t -= t0;
        m_duration = t1 - t0;
        return true;
    };
    // upright: the hips -> head direction in the animation as at rest
    auto upright = [&]() {
        auto spineAt = [&](double t) {
            std::vector<QMatrix4x4> w;
            worldMatrices(t, w);
            QVector3D hips, head;
            bool hasHead = false;
            for (int j : m_joints) {
                const QString n = shortName(m_nodes[size_t(j)].name);
                if (n == QLatin1String("hips")) hips = w[size_t(j)].map(QVector3D());
                if (n == QLatin1String("head")) { head = w[size_t(j)].map(QVector3D()); hasHead = true; }
            }
            return hasHead ? (head - hips).normalized() : QVector3D(0, 1, 0);
        };
        return QVector3D::dotProduct(spineAt(m_duration * .25), spineAt(-1)) > .7f;
    };
    QString firstError;
    bool found = false;
    for (int k = 0; k < anims.size() && !found; ++k) {
        QString why;
        if (!readAnimation(anims.at(k).toObject(), &why)) {
            if (firstError.isEmpty())
                firstError = why;
            continue;
        }
        found = anims.size() == 1 || upright();
    }
    if (!found) {
        if (!firstError.isEmpty() && m_channels.empty())
            return fail(error, firstError);
        if (!readAnimation(anims.at(0).toObject(), error))   // (none upright: the first, as before)
            return false;
    }
    if (m_neckScale < 1.f)
        shortenNeck();
    if (m_neckWidth != 1.f)
        widenNeck();
    if (motion && !retargetFrom(*motion))
        qWarning("GlbModel: the motion could not be moved onto this skeleton - its own animation is used");

    prepare();
    prepareRider();
    return true;
}

namespace {
// the rotation of a (rotation x uniform scale x translation) matrix
QQuaternion rotationOf(const QMatrix4x4& m)
{
    QMatrix3x3 r = m.toGenericMatrix<3, 3>();
    const float s = std::sqrt(r(0, 0) * r(0, 0) + r(1, 0) * r(1, 0) + r(2, 0) * r(2, 0));
    if (s > 0)
        r *= 1.f / s;
    return QQuaternion::fromRotationMatrix(r).normalized();
}
} // namespace

// Another Mixamo figure's motion on this skeleton (retargeting by joint
// name): each joint turns in the world as the source's turns from its rest
// pose (both T-poses), the hips move as the source's, scaled to this
// figure's size. The source's world is turned onto this one's (up: hips ->
// head, right: the thighs) - one may be stored lying.
std::shared_ptr<GlbModel> GlbModel::withMotion(const GlbModel& motion) const
{
    auto g = std::make_shared<GlbModel>(*this);
    if (!g->retargetFrom(motion))
        return {};
    g->prepareMotion();
    g->prepareBounce();
    return g;
}

void GlbModel::setShoulderWidth(float scale)
{
    m_shoulderScale = std::clamp(scale, .6f, 1.2f);
    for (int s = 0; s < 2; ++s) {
        m_shoulderNode[s] = -1;
        for (int j : m_joints)
            if (shortName(m_nodes[size_t(j)].name) == QLatin1String(s == 0 ? "leftshoulder" : "rightshoulder"))
                m_shoulderNode[s] = j;
        if (m_armNode[s] < 0)   // (the arm too: its size kept)
            for (int j : m_joints)
                if (shortName(m_nodes[size_t(j)].name) == QLatin1String(s == 0 ? "leftarm" : "rightarm"))
                    m_armNode[s] = j;
    }
}

// A shorter neck: the head (and all below it in the skeleton) comes down
// along the neck towards the neck joint, the neck's skin squeezed evenly
// between them - the bind pose itself, so every pose has the shorter neck.
void GlbModel::shortenNeck()
{
    auto at = [&](const char* name) -> int {
        for (size_t j = 0; j < m_joints.size(); ++j)
            if (shortName(m_nodes[size_t(m_joints[j])].name) == QLatin1String(name))
                return int(j);
        return -1;
    };
    const int neck = at("neck"), head = at("head");
    if (neck < 0 || head < 0)
        return;
    const QVector3D n = m_ibm[size_t(neck)].inverted().map(QVector3D()), h = m_ibm[size_t(head)].inverted().map(QVector3D());
    const float len = (h - n).length();
    if (len <= 0)
        return;
    const QVector3D dir = (h - n) / len;
    const QVector3D delta = dir * (len * (1.f - m_neckScale));   // the head's move (down the neck)
    // the head's node and all below it: their bind poses moved with it
    const int headNode = m_joints[size_t(head)];
    auto below = [&](int node) {
        for (int k = node; k >= 0; k = m_nodes[size_t(k)].parent)
            if (k == headNode)
                return true;
        return false;
    };
    QMatrix4x4 back;
    back.translate(delta);
    for (size_t j = 0; j < m_joints.size(); ++j)
        if (below(m_joints[j]))
            m_ibm[j] = m_ibm[j] * back;
    // (a clip keying the head's offset would undo the shorter neck: those keys go)
    m_channels.erase(std::remove_if(m_channels.begin(), m_channels.end(),
                                    [&](const Channel& c) { return c.node == headNode && c.path == 0; }),
                     m_channels.end());
    Node& hn = m_nodes[size_t(headNode)];
    if (!hn.hasMatrix)
        hn.t *= m_neckScale;   // (its offset from the neck)
    // the skin: down by as much as it is far up the neck (all of it from the head up)
    for (QVector3D& p : m_pos) {
        const float f = std::clamp(QVector3D::dotProduct(p - n, dir) / len, 0.f, 1.f);
        p -= delta * f;
    }
}

// A thicker (> 1) neck: its skin pushed out from the neck's line - all of it
// in the middle of the neck, fading out towards the shoulders and the jaw.
// Only the neck itself: what is further out (a ponytail, the shoulders) stays.
void GlbModel::widenNeck()
{
    auto at = [&](const char* name) -> int {
        for (size_t j = 0; j < m_joints.size(); ++j)
            if (shortName(m_nodes[size_t(m_joints[j])].name) == QLatin1String(name))
                return int(j);
        return -1;
    };
    const int neck = at("neck"), head = at("head");
    if (neck < 0 || head < 0)
        return;
    const QVector3D n = m_ibm[size_t(neck)].inverted().map(QVector3D()), h = m_ibm[size_t(head)].inverted().map(QVector3D());
    const float len = (h - n).length();
    if (len <= 0)
        return;
    const QVector3D dir = (h - n) / len;
    auto along = [&](const QVector3D& p) { return QVector3D::dotProduct(p - n, dir) / len; };
    auto out = [&](const QVector3D& p) { const QVector3D d = p - n; return d - dir * QVector3D::dotProduct(d, dir); };
    // the neck's radius: the median distance from the line in its middle
    std::vector<float> r;
    for (const QVector3D& p : m_pos) {
        const float t = along(p);
        if (t > .3f && t < .7f)
            r.push_back(out(p).length());
    }
    if (r.size() < 20)
        return;
    std::nth_element(r.begin(), r.begin() + long(r.size() / 2), r.end());
    const float radius = r[r.size() / 2];
    for (QVector3D& p : m_pos) {
        const float t = along(p);
        // along the neck: 0 at the shoulders' line and below the jaw, 1 between
        // (not up to the jaw: its underside pushed out would crease the throat)
        const float f = std::clamp((t + .35f) / .45f, 0.f, 1.f) * std::clamp((.8f - t) / .4f, 0.f, 1.f);
        const QVector3D o = out(p);
        const float d = o.length();
        // across: the neck only (fading out beyond its radius)
        const float g = std::clamp((radius * 1.6f - d) / (radius * .5f), 0.f, 1.f);
        if (f > 0 && g > 0 && d > 1e-9f)
            p += o * ((m_neckWidth - 1.f) * f * g);
    }
}

void GlbModel::slimUpperBody(float amount)
{
    amount = std::clamp(amount, 0.f, .5f);
    if (amount <= 0)
        return;
    // per joint: how much its flesh comes in towards the bone (of `amount`)
    struct Part { const char* joint; const char* child; float share; };
    static const Part parts[] = {
        { "leftshoulder", "leftarm", 1.f }, { "rightshoulder", "rightarm", 1.f },
        { "leftarm", "leftforearm", 1.f }, { "rightarm", "rightforearm", 1.f },
        { "leftforearm", "lefthand", .5f }, { "rightforearm", "righthand", .5f },
        { "spine2", "neck", .45f }, { "neck", "head", .5f },
    };
    auto bind = [&](const char* name) -> std::optional<QVector3D> {
        for (size_t j = 0; j < m_joints.size(); ++j)
            if (shortName(m_nodes[size_t(m_joints[j])].name) == QLatin1String(name))
                return m_ibm[j].inverted().map(QVector3D());
        return std::nullopt;
    };
    std::vector<int> part(m_joints.size(), -1);
    std::vector<QVector3D> a(std::size(parts)), b(std::size(parts));
    for (size_t k = 0; k < std::size(parts); ++k) {
        const auto pa = bind(parts[k].joint), pb = bind(parts[k].child);
        if (!pa || !pb)
            continue;
        a[k] = *pa;
        b[k] = *pb;
        for (size_t j = 0; j < m_joints.size(); ++j)
            if (shortName(m_nodes[size_t(m_joints[j])].name) == QLatin1String(parts[k].joint))
                part[j] = int(k);
    }
    for (size_t i = 0; i < m_pos.size(); ++i) {
        QVector3D move;
        for (int w = 0; w < 4; ++w) {
            const float wt = m_wgt[i][size_t(w)];
            const int k = wt > 0 ? part[m_jnt[i][size_t(w)]] : -1;
            if (k < 0)
                continue;
            const QVector3D d = b[size_t(k)] - a[size_t(k)];
            const float t = std::clamp(QVector3D::dotProduct(m_pos[i] - a[size_t(k)], d) / std::max(1e-12f, d.lengthSquared()), 0.f, 1.f);
            move += (a[size_t(k)] + d * t - m_pos[i]) * (wt * amount * parts[k].share);
        }
        m_pos[i] += move;
    }
}

bool GlbModel::retargetFrom(const GlbModel& src)
{
    auto findIn = [](const GlbModel& g, const char* name) -> int {
        for (int j : g.m_joints)
            if (shortName(g.m_nodes[size_t(j)].name) == QLatin1String(name))
                return j;
        return -1;
    };
    std::vector<QMatrix4x4> sRest, dRest, sW;
    src.worldMatrices(-1, sRest);
    worldMatrices(-1, dRest);
    auto pos = [](const std::vector<QMatrix4x4>& w, int j) { return w[size_t(j)].map(QVector3D()); };
    const int sh = findIn(src, "hips"), sd = findIn(src, "head"), sl = findIn(src, "leftupleg"), sr = findIn(src, "rightupleg");
    const int dh = findIn(*this, "hips"), dd = findIn(*this, "head"), dl = findIn(*this, "leftupleg"), dr = findIn(*this, "rightupleg");
    if (sh < 0 || sd < 0 || sl < 0 || sr < 0 || dh < 0 || dd < 0 || dl < 0 || dr < 0 || src.m_duration <= 0)
        return false;
    auto basis = [&](const std::vector<QMatrix4x4>& w, int h, int d, int l, int r) {
        const QVector3D up = (pos(w, d) - pos(w, h)).normalized();
        QVector3D right = pos(w, r) - pos(w, l);
        right = (right - up * QVector3D::dotProduct(right, up)).normalized();
        return QQuaternion::fromAxes(right, up, QVector3D::crossProduct(right, up));
    };
    const QQuaternion F = basis(dRest, dh, dd, dl, dr) * basis(sRest, sh, sd, sl, sr).conjugated();   // src world -> this world
    const float size = (pos(dRest, dd) - pos(dRest, dh)).length() / std::max(1e-9f, (pos(sRest, sd) - pos(sRest, sh)).length());

    // this node -> the source's (same joint name)
    std::vector<int> map(m_nodes.size(), -1);
    for (int j : m_joints)
        for (int k : src.m_joints)
            if (shortName(src.m_nodes[size_t(k)].name) == shortName(m_nodes[size_t(j)].name))
                map[size_t(j)] = k;

    constexpr double kFps = 30;
    const int n = std::max(2, int(std::ceil(src.m_duration * kFps)) + 1);
    std::vector<Channel> rot(m_nodes.size());
    Channel hipsT;
    hipsT.node = dh;
    hipsT.path = 0;
    std::vector<QQuaternion> target(m_nodes.size());   // this figure's world rotations at the sample
    std::vector<QVector3D> targetPos(m_nodes.size());
    for (int i = 0; i < n; ++i) {
        const double t = src.m_duration * i / (n - 1);
        src.worldMatrices(t, sW);
        for (int j : m_order) {
            const int p = m_nodes[size_t(j)].parent;
            const Node& nd = m_nodes[size_t(j)];
            const QQuaternion parentRot = p >= 0 ? target[size_t(p)] : QQuaternion();
            if (map[size_t(j)] >= 0) {
                const int k = map[size_t(j)];
                const QQuaternion delta = rotationOf(sW[size_t(k)]) * rotationOf(sRest[size_t(k)]).conjugated();
                target[size_t(j)] = (F * delta * F.conjugated() * rotationOf(dRest[size_t(j)])).normalized();
                const QQuaternion local = (parentRot.conjugated() * target[size_t(j)]).normalized();
                Channel& ch = rot[size_t(j)];
                ch.node = j;
                ch.path = 1;
                ch.times.push_back(float(t));
                // (the same hemisphere as the key before: slerp the short way)
                QQuaternion q = local;
                if (ch.values.size() >= 4) {
                    const float* b = &ch.values[ch.values.size() - 4];
                    if (QQuaternion::dotProduct(q, QQuaternion(b[3], b[0], b[1], b[2])) < 0)
                        q = -q;
                }
                ch.values.insert(ch.values.end(), { q.x(), q.y(), q.z(), q.scalar() });
                if (j == dh) {
                    // the hips' move from rest, in this world, this size -> the parent's frame
                    const QVector3D move = F.rotatedVector(pos(sW, k) - pos(sRest, k)) * size;
                    const QMatrix4x4 parent = p >= 0 ? dRest[size_t(p)] : QMatrix4x4();
                    const QVector3D lt = parent.inverted().map(pos(dRest, j) + move);
                    hipsT.times.push_back(float(t));
                    hipsT.values.insert(hipsT.values.end(), { lt.x(), lt.y(), lt.z() });
                }
            } else {
                target[size_t(j)] = (parentRot * (p >= 0 ? nd.r : rotationOf(dRest[size_t(j)]))).normalized();
            }
        }
    }
    m_channels.clear();
    for (Channel& ch : rot)
        if (ch.node >= 0)
            m_channels.push_back(std::move(ch));
    if (!hipsT.times.empty())
        m_channels.push_back(std::move(hipsT));
    m_duration = src.m_duration;
    m_retargeted = true;
    return !m_channels.empty();
}

void GlbModel::nodeLocal(const std::vector<Node>& nodes, double t, std::vector<QMatrix4x4>& local) const
{
    // the nodes' transforms at t (scratch reused per thread: no per-frame
    // allocation, the names not copied)
    struct Trs { QVector3D t, s; QQuaternion r; bool hasMatrix; const QMatrix4x4* matrix; };
    static thread_local std::vector<Trs> n;
    n.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        n[i] = { nodes[i].t, nodes[i].s, nodes[i].r, nodes[i].hasMatrix, &nodes[i].matrix };
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
            Trs& node = n[size_t(ch.node)];
            node.hasMatrix = false;
            if (ch.path == 1) {
                const float* a = &ch.values[k * 4];
                const float* b = &ch.values[k1 * 4];
                const QQuaternion qa(a[3], a[0], a[1], a[2]), qb(b[3], b[0], b[1], b[2]);
                node.r = f > 0 ? QQuaternion::slerp(qa, qb, f).normalized() : qa.normalized();
                if (ch.swing < 1.f)   // (a damped swing: about ch.mean)
                    node.r = QQuaternion::slerp(ch.mean, node.r, ch.swing).normalized();
            } else {
                const float* a = &ch.values[k * 3];
                const float* b = &ch.values[k1 * 3];
                const QVector3D v(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f);
                (ch.path == 0 ? node.t : node.s) = v;
            }
        }
    }
    // narrower shoulders (setShoulderWidth()): the shoulder bones shorter,
    // the arms kept their size
    if (m_shoulderScale != 1.f)
        for (int s = 0; s < 2; ++s) {
            if (m_shoulderNode[s] >= 0)
                n[size_t(m_shoulderNode[s])].s *= m_shoulderScale;
            if (m_armNode[s] >= 0)
                n[size_t(m_armNode[s])].s /= m_shoulderScale;
        }
    local.resize(n.size());
    for (size_t i = 0; i < n.size(); ++i) {
        if (n[i].hasMatrix) {
            local[i] = *n[i].matrix;
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
    static thread_local std::vector<QMatrix4x4> local;   // (scratch: no per-frame allocation)
    nodeLocal(m_nodes, t, local);
    world.resize(local.size());
    QMatrix4x4 up;   // (a model stored lying: stood up, prepare())
    up.rotate(m_up);
    for (int i : m_order) {
        const int p = m_nodes[size_t(i)].parent;
        world[size_t(i)] = p >= 0 ? world[size_t(p)] * local[size_t(i)] : up * local[size_t(i)];
        // the upper arms / thighs turned in towards the body about the
        // shoulder / hip (prepare())
        for (int s = 0; s < 2; ++s)
            for (const auto& [node, in] : { std::pair(m_armNode[s], m_armIn[s]), std::pair(m_legNode[s], m_legIn[s]) })
                if (i == node && t >= 0) {
                    const QVector3D pivot = world[size_t(i)].map(QVector3D());
                    QMatrix4x4 r;
                    r.translate(pivot);
                    r.rotate(in);
                    r.translate(-pivot);
                    world[size_t(i)] = r * world[size_t(i)];
                }
    }
}

void GlbModel::skin(double t, std::vector<QVector3D>& pos, std::vector<QVector3D>* nrm) const
{
    static thread_local std::vector<QMatrix4x4> world, jm;   // (scratch: no per-frame allocation)
    worldMatrices(t, world);
    jm.resize(m_joints.size());
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
    // which way is up: hips -> head at rest (a model can be stored lying -
    // z up - with the armature's rotation undone by its skin)
    m_up = QQuaternion();
    {
        std::vector<QMatrix4x4> w;
        worldMatrices(-1, w);
        std::optional<QVector3D> hips, head;
        for (int j : m_joints) {
            const QString n = shortName(m_nodes[size_t(j)].name);
            if (n == QLatin1String("hips")) hips = w[size_t(j)].map(QVector3D());
            if (n == QLatin1String("head")) head = w[size_t(j)].map(QVector3D());
        }
        if (hips && head && (*head - *hips).lengthSquared() > 1e-12f) {
            const QVector3D up = (*head - *hips).normalized();
            if (up.y() < .8f)   // (not already y up)
                m_up = QQuaternion::rotationTo(up, QVector3D(0, 1, 0));
        }
    }
    prepareMotion();
    // an Avatar without a usable texture: coloured (and helmeted) like the
    // cyclist (a runner: like the runner)
    if (m_outfit == Outfit::Avatar && !colorizeFromTexture())
        m_outfit = Outfit::Cyclist;
    if (m_outfit == Outfit::RunnerAvatar && !colorizeFromTexture())
        m_outfit = Outfit::Runner;
    if (m_outfit == Outfit::RowerAvatar && !colorizeFromTexture())
        m_outfit = Outfit::Rower;
    if (m_outfit != Outfit::Avatar && m_outfit != Outfit::RunnerAvatar && m_outfit != Outfit::RowerAvatar)
        colorize();
    // (the texture data is not needed any more)
    m_textures.clear();
    m_textures.shrink_to_fit();
    m_uv = {};
    m_vertexTexture = {};
    m_baseColor = {};
    prepareBounce();
}

// What depends on the animation (prepare(); withMotion()): a textured
// runner's thigh / arm / leg adjustments, the scale, facing and ground.
void GlbModel::prepareMotion()
{
    // A textured runner's knees come less far forward than in its clip (a
    // Mixamo running clip drives them high): each thigh's swing is scaled
    // towards its rearmost position - the push-off behind keeps its reach,
    // the knee drive in front is cut.
    if (m_outfit == Outfit::RunnerAvatar && !m_retargeted) {
        std::vector<QMatrix4x4> w;
        auto at = [&](const char* name) -> int {
            for (int j : m_joints)
                if (shortName(m_nodes[size_t(j)].name) == QLatin1String(name))
                    return j;
            return -1;
        };
        // forward: the toes' direction at rest
        worldMatrices(-1, w);
        QVector3D fwd;
        for (const auto& pair : { std::pair("lefttoebase", "leftfoot"), std::pair("righttoebase", "rightfoot") }) {
            const int t = at(pair.first), f = at(pair.second);
            if (t >= 0 && f >= 0)
                fwd += w[size_t(t)].map(QVector3D()) - w[size_t(f)].map(QVector3D());
        }
        const char* legs[2][2] = { { "leftupleg", "leftleg" }, { "rightupleg", "rightleg" } };
        for (Channel& ch : m_channels) {
            if (ch.path != 1 || fwd.lengthSquared() < 1e-12f)
                continue;
            const QString n = shortName(m_nodes[size_t(ch.node)].name);
            int knee = -1;
            for (const auto& l : legs)
                if (n == QLatin1String(l[0]))
                    knee = at(l[1]);
            if (knee < 0)
                continue;
            // the key with the knee furthest back of the hip
            float rear = 1e30f;
            size_t rk = 0;
            for (size_t k = 0; k < ch.times.size(); ++k) {
                worldMatrices(ch.times[k], w);
                const float d = QVector3D::dotProduct(w[size_t(knee)].map(QVector3D()) - w[size_t(ch.node)].map(QVector3D()), fwd);
                if (d < rear) {
                    rear = d;
                    rk = k;
                }
            }
            const float* v = &ch.values[rk * 4];
            ch.mean = QQuaternion(v[3], v[0], v[1], v[2]).normalized();
            ch.swing = kThighSwing;
        }
    }

    // A textured runner's elbows closer to the body: a Mixamo running clip
    // retargeted to a broad Tripo figure holds the upper arms out to the
    // sides (seen from behind) - turned in about the shoulder, about the
    // forward axis, each the way that brings its elbow in.
    m_armNode[0] = m_armNode[1] = -1;
    m_legNode[0] = m_legNode[1] = -1;
    if (m_outfit == Outfit::RunnerAvatar) {
        auto at = [&](const char* name) -> int {
            for (int j : m_joints)
                if (shortName(m_nodes[size_t(j)].name) == QLatin1String(name))
                    return j;
            return -1;
        };
        std::vector<QMatrix4x4> w;
        worldMatrices(-1, w);
        QVector3D fwd;
        for (const auto& pair : { std::pair("lefttoebase", "leftfoot"), std::pair("righttoebase", "rightfoot") }) {
            const int a = at(pair.first), b = at(pair.second);
            if (a >= 0 && b >= 0)
                fwd += w[size_t(a)].map(QVector3D()) - w[size_t(b)].map(QVector3D());
        }
        const int hips = at("hips"), neck = at("neck");
        const char* arms[2][2] = { { "leftarm", "leftforearm" }, { "rightarm", "rightforearm" } };
        if (fwd.lengthSquared() > 1e-12f && hips >= 0 && neck >= 0) {
            fwd.normalize();
            worldMatrices(m_duration * .25, w);
            const QVector3D h = w[size_t(hips)].map(QVector3D()), n = w[size_t(neck)].map(QVector3D());
            const QVector3D spine = (n - h).normalized();
            auto offAxis = [&](const QVector3D& p) {   // distance from the spine's line
                const QVector3D d = p - h;
                return (d - spine * QVector3D::dotProduct(d, spine)).length();
            };
            for (int s = 0; s < 2; ++s) {
                const int arm = at(arms[s][0]), fore = at(arms[s][1]);
                if (arm < 0 || fore < 0)
                    continue;
                const QVector3D sh = w[size_t(arm)].map(QVector3D()), el = w[size_t(fore)].map(QVector3D());
                const QQuaternion q = QQuaternion::fromAxisAndAngle(fwd, kArmInDeg);
                const bool plus = offAxis(sh + q.rotatedVector(el - sh)) < offAxis(sh + q.conjugated().rotatedVector(el - sh));
                m_armIn[s] = plus ? q : q.conjugated();
                m_armNode[s] = arm;
            }
            // the feet nearer the line under the body: a broad-hipped figure
            // with the classic runner's motion runs with its legs apart
            const char* legs[2][2] = { { "leftupleg", "leftfoot" }, { "rightupleg", "rightfoot" } };
            for (int s = 0; s < 2 && m_legInDeg > 0; ++s) {
                const int hip = at(legs[s][0]), foot = at(legs[s][1]);
                if (hip < 0 || foot < 0)
                    continue;
                const QVector3D hp = w[size_t(hip)].map(QVector3D()), ft = w[size_t(foot)].map(QVector3D());
                const QQuaternion q = QQuaternion::fromAxisAndAngle(fwd, m_legInDeg);
                const bool plus = offAxis(hp + q.rotatedVector(ft - hp)) < offAxis(hp + q.conjugated().rotatedVector(ft - hp));
                m_legIn[s] = plus ? q : q.conjugated();
                m_legNode[s] = hip;
            }
        }
    }

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

}

void GlbModel::prepareBounce()
{
    // The bounce: a running clip made "in place" can lose the body's rise and
    // fall with each step (the figure seems to glide) and float its feet
    // over the ground. Put back for a textured runner whose clip barely
    // bounces, as in running: lowest at midstance (the stance foot under the
    // body, the knee bent), highest in the flight - two per cycle - with the
    // stance foot on the ground and no foot below it. (Posed one sample at
    // a time: nothing of the cycle is kept.)
    m_bounce.clear();
    if (m_outfit != Outfit::RunnerAvatar)
        return;
    constexpr int S = kBounceSamples;
    const size_t V = m_pos.size();
    std::vector<GhostVertex> v;
    // first: does it bounce already? (and the feet: the lowest parts on average, by side)
    std::vector<float> meanY(V, 0.f), meanX(V, 0.f);
    float topLo = 1e30f, topHi = -1e30f;
    for (int k = 0; k < S; ++k) {
        pose(double(k) / S, v);
        float top = -1e30f;
        for (size_t i = 0; i < V; ++i) {
            meanY[i] += v[i].position.y() / S;
            meanX[i] += v[i].position.x() / S;
            top = std::max(top, v[i].position.y());
        }
        topLo = std::min(topLo, top);
        topHi = std::max(topHi, top);
    }
    if (topHi - topLo >= kHasBounceM)
        return;
    std::vector<float> footMin(S, 1e30f), footZ[2] = { std::vector<float>(S, 0.f), std::vector<float>(S, 0.f) };
    std::vector<float> sideMin[2] = { std::vector<float>(S, 1e30f), std::vector<float>(S, 1e30f) };
    for (int k = 0; k < S; ++k) {
        pose(double(k) / S, v);
        int n[2] = { 0, 0 };
        for (size_t i = 0; i < V; ++i) {
            if (meanY[i] > kFootRegionM)
                continue;
            const QVector3D& p = v[i].position;
            const int sd = meanX[i] < 0 ? 0 : 1;
            sideMin[sd][size_t(k)] = std::min(sideMin[sd][size_t(k)], p.y());
            footZ[sd][size_t(k)] += p.z();
            ++n[sd];
        }
        for (int sd = 0; sd < 2; ++sd)
            footZ[sd][size_t(k)] /= std::max(1, n[sd]);
        footMin[size_t(k)] = std::min(sideMin[0][size_t(k)], sideMin[1][size_t(k)]);
    }
    // a midstance: the left foot the lower one and under the body
    int mid = -1;
    float best = 1e30f;
    for (int k = 0; k < S; ++k)
        if (sideMin[0][size_t(k)] <= sideMin[1][size_t(k)] && std::abs(footZ[0][size_t(k)]) < best) {
            best = std::abs(footZ[0][size_t(k)]);
            mid = k;
        }
    if (mid < 0 || footMin[size_t(mid)] >= 1e29f)
        return;
    // the stance feet down on the ground at both midstances (the same for
    // both: the wave stays even), then a pure wave - smooth, the same for
    // every step (no kinks)
    const int mid2 = (mid + S / 2) % S;
    const float base = -.5f * (footMin[size_t(mid)] + footMin[size_t(mid2)]);
    m_bounce.assign(S, 0.f);
    for (int k = 0; k < S; ++k) {
        const double t = double(k - mid) / S;
        m_bounce[size_t(k)] = base + kRunBounceM * .5f * float(1 - std::cos(4 * M_PI * t));
    }
    // no foot through the ground: all of it a little higher (one shift - still smooth)
    float sink = 0;
    for (int k = 0; k < S; ++k)
        sink = std::max(sink, -(footMin[size_t(k)] + m_bounce[size_t(k)]));
    for (float& b : m_bounce)
        b += sink;
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

// the sculler's unisuit (fractions of the shoulders' half width / the neck's height above them)
constexpr float kNeckR = .40f, kNeckCut = .35f, kScoop = 1.2f, kStrapOuter = .70f, kSideW = .95f, kArmpitDepth = 2.2f;

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
    // the sculler's unisuit: its armholes and neckline are planes in the bind
    // pose, the same whichever joint a vertex follows - straight edges, not
    // the zigzag of the boundary between the torso's and the arms' vertices
    const float shoulderH = shoulder[0] ? QVector3D::dotProduct(*shoulder[0] - root, up) : 0.f;
    const float neckH = neck ? QVector3D::dotProduct(*neck - root, up) : shoulderH;
    // (in the bind pose, across the body: W = the shoulder joints' half width)
    const float W = shoulder[0] && shoulder[1] ? .5f * (std::abs(QVector3D::dotProduct(*shoulder[0] - root, side))
                                                        + std::abs(QVector3D::dotProduct(*shoulder[1] - root, side)))
                                               : 1.f;
    const float neckDy = std::max(1e-6f, neckH - shoulderH);
    auto unisuit = [&](const QVector3D& p) -> int {
        const float h = QVector3D::dotProduct(p - root, up);
        const float lat = std::abs(QVector3D::dotProduct(p - root, side));
        const int arm = QVector3D::dotProduct(p - root, side) >= 0 ? 0 : 1;   // 0 = left
        // the arm beyond the shoulder joint: bare
        if (along(p, shoulder[arm], elbow[arm]) > .04f)
            return Skin;
        // (the underside of the arm next to the armpit: out past the body's side)
        if (lat > kSideW * W && h > waist)
            return Skin;
        // the head and the neck: bare (the neck: inside the neckline's circle,
        // above its cut - lower at the front, a round scoop)
        if (h > neckH + .5f * neckDy)
            return Skin;
        const float fwd = QVector3D::dotProduct(p - *neck, fwdAxis);
        const float neckR = kNeckR * W;
        // the front: a round U (deepest in the middle), the back a little higher
        const float u = std::min(1.f, lat / neckR);
        const float depth = fwd > 0 ? kScoop * neckDy * std::sqrt(std::max(0.f, 1.f - u * u)) * std::min(1.f, fwd / (.5f * neckR))
                                    : 0.f;
        if (lat < neckR && h > shoulderH + kNeckCut * neckDy - depth)
            return Skin;
        // the armholes: the strap's outer edge at the top, curving down and
        // out to the armpit (a quarter circle)
        const float strapOuter = kStrapOuter * W, sideW = kSideW * W;
        const float armpitH = shoulderH - kArmpitDepth * neckDy;
        if (h > armpitH) {
            const float t = std::clamp((shoulderH + .3f * neckDy - h) / (shoulderH + .3f * neckDy - armpitH), 0.f, 1.f);
            const float outer = strapOuter + (sideW - strapOuter) * (1.f - std::sqrt(std::max(0.f, 1.f - t * t)));
            if (lat > outer)
                return Skin;
        }
        return Shirt;
    };
    auto regionAt = [&](int group, const QVector3D& p) -> int {
        const float h = QVector3D::dotProduct(p - root, up);
        if (rower && group != Legs && shoulder[0] && shoulder[1] && elbow[0] && elbow[1] && neck)
            return unisuit(p);
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

// --- Avatar: the model's own (textured) clothes --------------------------------------------------

namespace {
// OKLab (Björn Ottosson): a perceptual colour space - L lightness, a / b the
// hue and chroma. Display (sRGB) colours 0..1 in and out.
float toLinear(float c) { return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); }
float toSrgb(float c)
{
    c = std::clamp(c, 0.f, 1.f);
    return c <= .0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - .055f;
}
QVector3D toOklab(const QVector3D& srgb)
{
    const float r = toLinear(srgb.x()), g = toLinear(srgb.y()), b = toLinear(srgb.z());
    const float l = std::cbrt(.4122214708f * r + .5363325363f * g + .0514459929f * b);
    const float m = std::cbrt(.2119034982f * r + .6806995451f * g + .1073969566f * b);
    const float s = std::cbrt(.0883024619f * r + .2817188376f * g + .6299787005f * b);
    return { .2104542553f * l + .7936177850f * m - .0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + .4505937099f * s,
             .0259040371f * l + .7827717662f * m - .8086757660f * s };
}
QVector3D fromOklab(const QVector3D& lab)
{
    const float l = lab.x() + .3963377774f * lab.y() + .2158037573f * lab.z();
    const float m = lab.x() - .1055613458f * lab.y() - .0638541728f * lab.z();
    const float s = lab.x() - .0894841775f * lab.y() - 1.2914855480f * lab.z();
    const float L = l * l * l, M = m * m * m, S = s * s * s;
    return { toSrgb(4.0767416621f * L - 3.3077115913f * M + .2309699292f * S),
             toSrgb(-1.2684380046f * L + 2.6097574011f * M - .3413193965f * S),
             toSrgb(-.0041960863f * L - .7034186147f * M + 1.7076147010f * S) };
}
float chroma(const QVector3D& lab) { return std::hypot(lab.y(), lab.z()); }
// below this chroma a colour counts as grey (black, white): matched by lightness only
constexpr float kGreyChroma = .045f;
// The colour class of a garment's main colour: 12 hues, or dark / mid / light grey.
int colorClass(const QVector3D& lab)
{
    if (chroma(lab) < kGreyChroma)
        return 12 + std::clamp(int(lab.x() * 3.f), 0, 2);
    const float hue = std::atan2(lab.z(), lab.y());   // -pi..pi
    return std::clamp(int((hue + float(M_PI)) / (2 * float(M_PI)) * 12.f), 0, 11);
}
// How far a colour is from a garment's main colour; < 0 = not the garment.
// The texture's baked light and shade make a garment's colour vary a lot in
// lightness (folds), less in hue: hue is what is compared.
float garmentDistance(const QVector3D& c, const QVector3D& main)
{
    const float cm = chroma(main), cc = chroma(c);
    const float dL = std::abs(c.x() - main.x());
    if (cm < kGreyChroma) {   // black / grey / white
        if (cc > kGreyChroma * 1.6f || dL > .22f)
            return -1;
        return dL + cc;
    }
    if (cc < std::max(.025f, cm * .35f) || dL > .42f)
        return -1;
    const float cosHue = (c.y() * main.y() + c.z() * main.z()) / std::max(1e-6f, cc * cm);
    if (cosHue < std::cos(38.f * float(M_PI) / 180.f))
        return -1;
    return (1.f - cosHue) * 4.f + dL * .5f + std::abs(cc - cm);
}
} // namespace

bool GlbModel::colorizeFromTexture()
{
    const size_t V = m_pos.size();
    if (m_textures.empty() || m_uv.size() != V || m_vertexTexture.size() != V)
        return false;
    // The texture's colour at each vertex: sampled a little way INTO each of
    // its triangles (in texture space) and averaged - a vertex on a texture
    // seam lies on the edge of a patch, and sampling right there would pick
    // up the neighbouring patch's colour (specks of the jersey in the shorts).
    size_t textured = 0;
    auto texel = [](const QImage& img, int x, int y) {
        x = std::clamp(x, 0, img.width() - 1);
        y = std::clamp(y, 0, img.height() - 1);
        const QRgb c = reinterpret_cast<const QRgb*>(img.constScanLine(y))[x];
        return QVector3D(qRed(c), qGreen(c), qBlue(c)) / 255.f;
    };
    auto sample = [&](const QImage& img, QVector2D uv) {   // bilinear (glTF: v down the image)
        uv = QVector2D(uv.x() - std::floor(uv.x()), uv.y() - std::floor(uv.y()));
        const float fx = uv.x() * img.width() - .5f, fy = uv.y() * img.height() - .5f;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tx = fx - float(x0), ty = fy - float(y0);
        return (texel(img, x0, y0) * (1 - tx) + texel(img, x0 + 1, y0) * tx) * (1 - ty)
               + (texel(img, x0, y0 + 1) * (1 - tx) + texel(img, x0 + 1, y0 + 1) * tx) * ty;
    };
    m_color.assign(V, QVector3D());
    std::vector<float> weight(V, 0.f);
    for (size_t t = 0; t + 2 < m_indices.size(); t += 3) {
        const uint32_t v[3] = { m_indices[t], m_indices[t + 1], m_indices[t + 2] };
        const int tex = m_vertexTexture[v[0]];
        if (tex < 0 || tex >= int(m_textures.size()) || m_vertexTexture[v[1]] != tex || m_vertexTexture[v[2]] != tex)
            continue;
        const QImage& img = m_textures[size_t(tex)];
        for (int k = 0; k < 3; ++k) {
            const QVector2D uv = m_uv[v[k]] * .6f + m_uv[v[(k + 1) % 3]] * .2f + m_uv[v[(k + 2) % 3]] * .2f;
            m_color[v[k]] += sample(img, uv);
            weight[v[k]] += 1.f;
        }
    }
    for (size_t i = 0; i < V; ++i) {
        if (weight[i] > 0) {
            m_color[i] = m_color[i] / weight[i] * m_baseColor[i];
            ++textured;
        } else {
            m_color[i] = m_baseColor[i];
        }
    }
    if (textured < V / 2)
        return false;

    // the same point stored once per texture patch it borders (a seam): one
    // colour for all its copies, so the seams do not show as hard edges
    std::vector<uint32_t> weld(V);
    {
        std::unordered_map<quint64, uint32_t> first;
        first.reserve(V);
        for (size_t i = 0; i < V; ++i) {
            quint64 h = 1469598103934665603ull;
            for (int a = 0; a < 3; ++a)
                h = (h ^ quint64(qint64(std::lround(m_pos[i][a] * 1e5f)))) * 1099511628211ull;
            const auto [it, added] = first.try_emplace(h, uint32_t(i));
            // (the same point - checked: a hash collision must not merge two)
            weld[i] = !added && (m_pos[it->second] - m_pos[i]).lengthSquared() < 1e-10f ? it->second : uint32_t(i);
        }
        std::vector<QVector3D> sum(V);
        std::vector<int> n(V, 0);
        for (size_t i = 0; i < V; ++i) {
            sum[weld[i]] += m_color[i];
            ++n[weld[i]];
        }
        for (size_t i = 0; i < V; ++i)
            m_color[i] = sum[weld[i]] / float(n[weld[i]]);
    }

    // --- the garments ------------------------------------------------------
    // Where on the body (the dominant joint, Mixamo names) and the colour
    // decide: e.g. a jersey is the chest's main colour, wherever it is on the
    // torso, the arms and the hips. Details in other colours (a zip, a logo,
    // straps) and the skin keep the texture's colour.
    auto bindPos = [&](const char* name) -> std::optional<QVector3D> {
        for (size_t j = 0; j < m_joints.size(); ++j)
            if (shortName(m_nodes[size_t(m_joints[j])].name) == QLatin1String(name))
                return m_ibm[j].inverted().map(QVector3D(0, 0, 0));
        return std::nullopt;
    };
    const auto neck = bindPos("neck"), head = bindPos("head"), headTop = bindPos("headtop_end");
    const std::optional<QVector3D> hip[2] = { bindPos("leftupleg"), bindPos("rightupleg") };
    const std::optional<QVector3D> knee[2] = { bindPos("leftleg"), bindPos("rightleg") };
    const std::optional<QVector3D> ankle[2] = { bindPos("leftfoot"), bindPos("rightfoot") };
    auto along = [](const QVector3D& p, const std::optional<QVector3D>& a, const std::optional<QVector3D>& b) {
        if (!a || !b)
            return 0.5f;
        const QVector3D d = *b - *a;
        return QVector3D::dotProduct(p - *a, d) / std::max(1e-9f, d.lengthSquared());
    };
    // the top of the head: past the head joint towards its top (a guess without headtop_end)
    std::optional<QVector3D> top = headTop;
    if (!top && head && neck)
        top = *head + (*head - *neck) * 1.6f;

    enum Zone : uint8_t { ZOther, ZHead, ZNeck, ZTorso, ZUpperArm, ZForeArm, ZHips, ZUpLeg, ZShin, ZFoot };
    std::vector<uint8_t> zone(V, ZOther);
    std::vector<uint8_t> side(V, 0);   // 0 = left
    for (size_t i = 0; i < V; ++i) {
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (m_wgt[i][size_t(k)] > m_wgt[i][size_t(best)])
                best = k;
        const QString n = shortName(m_nodes[size_t(m_joints[m_jnt[i][size_t(best)]])].name);
        side[i] = n.startsWith(QLatin1String("right")) ? 1 : 0;
        Zone z = ZOther;
        if (n.contains(QLatin1String("head"))) z = ZHead;
        else if (n == QLatin1String("neck")) z = ZNeck;
        else if (n.startsWith(QLatin1String("spine")) || n.endsWith(QLatin1String("shoulder"))) z = ZTorso;
        else if (n.endsWith(QLatin1String("forearm"))) z = ZForeArm;
        else if (n.endsWith(QLatin1String("arm"))) z = ZUpperArm;
        else if (n == QLatin1String("hips")) z = ZHips;
        else if (n.endsWith(QLatin1String("upleg"))) z = ZUpLeg;
        else if (n.endsWith(QLatin1String("leg"))) z = ZShin;
        else if (n.endsWith(QLatin1String("foot")) || n.contains(QLatin1String("toe"))) z = ZFoot;
        zone[i] = uint8_t(z);
    }
    std::vector<QVector3D> lab(V);
    for (size_t i = 0; i < V; ++i)
        lab[i] = toOklab(m_color[i]);

    // each garment's main colour: the commonest colour class where only it can be
    auto mainColor = [&](const std::function<bool(size_t)>& core) -> QVector3D {
        int count[15] = {};
        for (size_t i = 0; i < V; ++i)
            if (core(i))
                ++count[colorClass(lab[i])];
        const int cls = int(std::max_element(count, count + 15) - count);
        if (count[cls] < 30)
            return QVector3D(-1, 0, 0);
        QVector3D sum;
        int n = 0;
        for (size_t i = 0; i < V; ++i)
            if (core(i) && colorClass(lab[i]) == cls) {
                sum += lab[i];
                ++n;
            }
        return sum / float(n);
    };
    QVector3D mainLab[GarmentCount];
    for (QVector3D& m : mainLab)
        m = QVector3D(-1, 0, 0);
    mainLab[Jersey] = mainColor([&](size_t i) { return zone[i] == ZTorso; });
    mainLab[Shorts] = mainColor([&](size_t i) {
        if (zone[i] != ZUpLeg)
            return false;
        const float a = along(m_pos[i], hip[side[i]], knee[side[i]]);
        return a > .15f && a < .55f;
    });
    const bool helmeted = m_outfit == Outfit::Avatar;   // (a runner: bare-headed - the top of the head is hair)
    if (helmeted)
        mainLab[Helmet] = mainColor([&](size_t i) { return zone[i] == ZHead && along(m_pos[i], neck, top) > .72f; });
    mainLab[Shoes] = mainColor([&](size_t i) {
        return zone[i] == ZFoot && along(m_pos[i], knee[side[i]], ankle[side[i]]) > 1.02f;
    });
    // the hair: the back of the head below the helmet (and a ponytail on the
    // neck), whatever is not the skin's colour (the face's) nor the helmet's
    const auto hipsJ = bindPos("hips");
    QVector3D fwd(0, 0, 1);
    if (hipsJ && head && hip[0] && hip[1]) {
        const QVector3D up = (*head - *hipsJ).normalized();
        fwd = QVector3D::crossProduct((*hip[0] - *hip[1]).normalized(), up).normalized();
    }
    const float headR = head && top ? std::max(1e-6f, (*top - *head).length()) : 1.f;
    constexpr float kHairFront = -.05f;   // (fractions of the head's height in front of the head joint)
    constexpr float kHairSkinL = .2f;     // hair darker (or lighter) than the skin by more: not skin
    auto facing = [&](size_t i) { return head ? QVector3D::dotProduct(m_pos[i] - *head, fwd) / headR : 0.f; };
    const QVector3D skinLab = mainColor([&](size_t i) {
        const float a = along(m_pos[i], neck, top);
        return zone[i] == ZHead && facing(i) > .3f && a > .25f && a < .6f;
    });
    // (skin in light and shade: the same hue, but not as dark as brown hair)
    auto skinLike = [&](const QVector3D& c) {
        return skinLab.x() >= 0 && garmentDistance(c, skinLab) >= 0 && std::abs(c.x() - skinLab.x()) < kHairSkinL;
    };
    auto notSkinOrHelmet = [&](size_t i) {
        return !skinLike(lab[i])
               && (mainLab[Helmet].x() < 0 || garmentDistance(lab[i], mainLab[Helmet]) < 0);
    };
    mainLab[Hair] = mainColor([&](size_t i) {
        return ((zone[i] == ZHead && (!helmeted || along(m_pos[i], neck, top) < .72f)) || zone[i] == ZNeck)
               && facing(i) < -.1f && notSkinOrHelmet(i);
    });
    if (mainLab[Hair].x() >= 0 && skinLike(mainLab[Hair]))
        mainLab[Hair] = QVector3D(-1, 0, 0);   // (no hair to be seen: what was found is skin)
    // a sculler rows barefoot: no shoes (what the feet show is skin); any
    // garment whose colour is the skin's is bare skin, not a garment
    if (m_outfit == Outfit::RowerAvatar)
        mainLab[Shoes] = QVector3D(-1, 0, 0);
    for (int g = Jersey; g < GarmentCount; ++g)
        if (g != Hair && mainLab[g].x() >= 0 && skinLab.x() >= 0 && garmentDistance(mainLab[g], skinLab) >= 0)
            mainLab[g] = QVector3D(-1, 0, 0);

    // every vertex: the nearest of the garments that can be there
    m_garment.assign(V, NoGarment);
    m_shade.assign(V, 0.f);
    for (size_t i = 0; i < V; ++i) {
        Garment cand[2] = { NoGarment, NoGarment };
        switch (zone[i]) {
        // (hair: only behind the ears - the helmet's straps down the sides of
        // the face, the eyebrows, a beard keep their colours)
        case ZHead:   // (bare-headed: the hair wherever it is, a fringe too)
            cand[0] = helmeted ? Helmet : Hair;
            cand[1] = helmeted && facing(i) < kHairFront ? Hair : NoGarment;
            break;
        case ZNeck: cand[0] = Jersey; cand[1] = facing(i) < kHairFront ? Hair : NoGarment; break;
        case ZTorso: case ZUpperArm: case ZForeArm: cand[0] = Jersey; break;
        case ZHips: case ZUpLeg: cand[0] = Shorts; cand[1] = Jersey; break;
        case ZShin:   // the shorts' hem round the knee
            if (along(m_pos[i], knee[side[i]], ankle[side[i]]) < .2f)
                cand[0] = Shorts;
            break;
        case ZFoot:   // the shoe below the sock's cuff
            if (along(m_pos[i], knee[side[i]], ankle[side[i]]) > .97f)
                cand[0] = Shoes;
            break;
        default: break;
        }
        float best = 1e9f;
        for (Garment g : cand) {
            if (g == NoGarment || mainLab[g].x() < 0)
                continue;
            // (hair, not skin: nearer the hair's lightness than the skin's -
            // a face in shadow has the hair's hue, not its darkness)
            if (g == Hair && skinLab.x() >= 0 && garmentDistance(lab[i], skinLab) >= 0
                && std::abs(lab[i].x() - skinLab.x()) <= std::abs(lab[i].x() - mainLab[Hair].x()))
                continue;
            const float d = garmentDistance(lab[i], mainLab[g]);
            if (d >= 0 && d < best) {
                best = d;
                m_garment[i] = g;
                m_shade[i] = lab[i].x() - mainLab[g].x();
            }
        }
    }
    // Specks: a point whose neighbours (across the seams too) are nearly all
    // another part becomes that part - a highlight on black shorts that was
    // too light to match, a fleck of the jersey's colour in the shorts. A
    // whole logo or zip (many points together) stays as it is.
    {
        std::vector<std::vector<uint32_t>> nb(V);
        for (size_t t = 0; t + 2 < m_indices.size(); t += 3)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = weld[m_indices[t + size_t(k)]], b = weld[m_indices[t + size_t((k + 1) % 3)]];
                if (a != b) {
                    nb[a].push_back(b);
                    nb[b].push_back(a);
                }
            }
        std::vector<uint8_t> g(V);
        for (size_t i = 0; i < V; ++i)
            g[i] = m_garment[weld[i]];   // (one part per point)
        for (int pass = 0; pass < 3; ++pass) {
            std::vector<uint8_t> next = g;
            for (size_t i = 0; i < V; ++i) {
                if (weld[i] != i || nb[i].size() < 3)
                    continue;
                int count[GarmentCount] = {};
                for (uint32_t j : nb[i])
                    ++count[g[j]];
                const int top = int(std::max_element(count, count + GarmentCount) - count);
                if (top != g[i] && count[top] * 4 >= int(nb[i].size()) * 3)
                    next[i] = uint8_t(top);
            }
            g.swap(next);
        }
        for (size_t i = 0; i < V; ++i) {
            const uint8_t gi = g[weld[i]];
            m_garment[i] = gi;
            m_shade[i] = gi == NoGarment ? 0.f : std::clamp(lab[i].x() - mainLab[gi].x(), -.25f, .25f);
        }
    }
    // The face's skin like the body's: a Tripo texture often bakes a greyer,
    // darker face than the arms. The skin of the head and neck (not the hair,
    // the eyes, the lips - not skin-coloured) takes the bare arms' skin tone,
    // keeping its own light and shade; fading out down the neck.
    {
        auto skinOf = [&](std::initializer_list<int> zones) {
            QVector3D sum;
            int n = 0;
            for (size_t i = 0; i < V; ++i)
                if (std::find(zones.begin(), zones.end(), int(zone[i])) != zones.end() && m_garment[i] == NoGarment
                    && skinLike(lab[i])) {
                    sum += lab[i];
                    ++n;
                }
            return n > 30 ? sum / float(n) : QVector3D(-1, 0, 0);
        };
        const QVector3D face = skinOf({ ZHead }), body = skinOf({ ZForeArm, ZUpperArm });
        if (face.x() >= 0 && body.x() >= 0) {
            const float faceC = std::max(1e-4f, chroma(face));
            for (size_t i = 0; i < V; ++i) {
                // (the upper chest too: the correction fades out across it, no edge at the neck)
                if ((zone[i] != ZHead && zone[i] != ZNeck && zone[i] != ZTorso) || m_garment[i] != NoGarment)
                    continue;
                // skin in light and in shade - greyish or purplish too (what
                // greys the face), but not a strong other colour (blue eyes)
                const QVector3D& c = lab[i];
                const bool skinHue = garmentDistance(QVector3D(face.x(), c.y(), c.z()), face) >= 0;
                if (std::abs(c.x() - face.x()) > .35f || (!skinHue && chroma(c) > faceC * 1.5f))
                    continue;
                const float w = std::clamp((along(m_pos[i], neck, top) + .45f) / .5f, 0.f, 1.f);
                // the arms' lightness and hue; the point's own shade, its
                // chroma relative to the face's (a grey point: warmed too)
                // (a little lighter still: the texture bakes shade into the face)
                const float L = c.x() + (body.x() - face.x()) + kFaceLift;
                const float rel = std::clamp(chroma(c) / faceC, .85f, 1.4f) * kFaceChroma;
                const QVector3D want(L, body.y() * rel, body.z() * rel);
                lab[i] = c + (want - c) * w;
                m_color[i] = fromOklab(lab[i]);
            }
        }
    }
    for (int g = 0; g < GarmentCount; ++g)
        m_garmentMain[g] = mainLab[g].x() < 0 ? QVector3D(-1, 0, 0) : fromOklab(mainLab[g]);
    m_textured = true;
    return true;
}

std::vector<QVector3D> GlbModel::colorsFor(const GarmentColors& colors) const
{
    if (!m_textured)
        return {};
    QVector3D want[GarmentCount];
    bool any = false;
    for (int g = 1; g < GarmentCount; ++g) {
        if (colors.color[g].x() >= 0 && m_garmentMain[g].x() >= 0) {
            want[g] = toOklab(colors.color[g]);
            any = true;
        } else {
            want[g] = QVector3D(-1, 0, 0);
        }
    }
    if (!any)
        return {};   // (the figure's own colours)
    std::vector<QVector3D> out(m_color);
    for (size_t i = 0; i < out.size() && i < m_garment.size(); ++i) {
        const int g = m_garment[i];
        if (g == NoGarment || want[g].x() < 0)
            continue;
        // the chosen colour with this point's light and shade: lighter or
        // darker by as much as the texture is here than the garment's main colour
        const QVector3D& w = want[g];
        const float L = std::clamp(w.x() + m_shade[i], 0.f, 1.f);
        const float c = L < w.x() ? std::clamp(L / std::max(.05f, w.x()), .3f, 1.f) : 1.f;   // (shadows a little greyer)
        out[i] = fromOklab(QVector3D(L, w.y() * c, w.z() * c));
    }
    return out;
}

void GlbModel::pose(double phase, std::vector<GhostVertex>& out) const
{
    phase -= std::floor(phase);
    const double t = phase * m_duration;
    static thread_local std::vector<QVector3D> pos, nrm;   // (scratch: no per-frame allocation)
    skin(t, pos, &nrm);
    QMatrix4x4 m = m_fix;
    const QVector3D drift = m_rootXZ.size() == 2 ? m_rootXZ[0] + m_rootXZ[1] * float(phase - 0.5) : QVector3D();
    m.translate(-drift);
    // (the bounce put back: 0 with a foot on the ground, an arc in the flight)
    float lift = 0;
    if (!m_bounce.empty()) {
        const double x = phase * double(m_bounce.size());
        const size_t k = size_t(x) % m_bounce.size();
        const float f = float(x - std::floor(x));
        lift = m_bounce[k] * (1 - f) + m_bounce[(k + 1) % m_bounce.size()] * f;
    }
    out.resize(pos.size() + m_extra.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        QVector3D n = m.mapVector(nrm[i]);
        out[i] = { m.map(pos[i]) + QVector3D(0, lift, 0), n.lengthSquared() > 0 ? n.normalized() : QVector3D(0, 1, 0), m_color[i] };
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
        m_jShoulder[s] = find(pre + QLatin1String("shoulder"));   // (optional)
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
    // the same point stored more than once (a textured model repeats the
    // points on its texture's seams - often half of them): skinned once
    {
        m_same.resize(m_bf.size());
        std::unordered_map<quint64, uint32_t> seen;
        seen.reserve(m_bf.size());
        auto key = [&](size_t i) {
            quint64 h = 1469598103934665603ull;
            auto mix = [&](quint64 v) { h = (h ^ v) * 1099511628211ull; };
            for (int a = 0; a < 3; ++a) {
                mix(quint64(qint64(std::lround(m_bf[i][a] * 1e5f))));
                mix(quint64(qint64(std::lround(m_nf[i][a] * 1e3f))));
            }
            for (int k = 0; k < 4; ++k)
                mix(quint64(m_jnt[i][size_t(k)]) << 32 | quint64(std::lround(m_wgt[i][size_t(k)] * 1e4f)));
            return h;
        };
        auto same = [&](size_t a, size_t b) {
            return m_bf[a] == m_bf[b] && (m_nf[a] - m_nf[b]).lengthSquared() < 1e-6f && m_jnt[a] == m_jnt[b]
                   && m_wgt[a] == m_wgt[b];
        };
        for (size_t i = 0; i < m_bf.size(); ++i) {
            const auto [it, added] = seen.try_emplace(key(i), uint32_t(i));
            m_same[i] = !added && same(it->second, i) ? it->second : uint32_t(i);
        }
    }
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

    // the seat: the bottom and the top of the thighs (dominant joint: the hips
    // or a thigh, below the hip joints, the thigh's upper half) - what can
    // rest on the saddle (poseOnBike())
    m_seatVerts.clear();
    m_buttockVerts.clear();
    m_seatMask.assign(m_bf.size(), 0);
    {
        const float hipJointY = std::min(m_jf[size_t(m_jUpLeg[0])].y(), m_jf[size_t(m_jUpLeg[1])].y());
        for (size_t i = 0; i < m_bf.size(); ++i) {
            if (i < m_same.size() && m_same[i] != i)
                continue;
            int best = 0;
            for (int k = 1; k < 4; ++k)
                if (m_wgt[i][size_t(k)] > m_wgt[i][size_t(best)])
                    best = k;
            const int j = m_jnt[i][size_t(best)];
            bool seat = false;
            if (j == m_jHips)
                seat = m_bf[i].y() < hipJointY + .03f;
            for (int s = 0; s < 2; ++s)
                if (j == m_jUpLeg[s]) {
                    const QVector3D a = m_jf[size_t(m_jUpLeg[s])], b = m_jf[size_t(m_jLeg[s])];
                    const QVector3D d = b - a;
                    seat = QVector3D::dotProduct(m_bf[i] - a, d) / std::max(1e-9f, d.lengthSquared()) < .45f;
                }
            if (seat)
                m_seatVerts.push_back(uint32_t(i));
            if (seat && j == m_jHips)
                m_buttockVerts.push_back(uint32_t(i));
            m_seatMask[i] = seat;
        }
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
    // How the back's lean is shared out from the pelvis up (fractions of
    // torsoDeg: hips, spine, spine1, spine2). The textured avatars are
    // modelled standing with a hollow lower back and full glutes: their
    // pelvis tilts less and the spine above it more, so the lower back rounds
    // over the saddle as a rider's does instead of dipping (sway-backed).
    static constexpr double kBackClassic[4] = { .80, .90, .96, 1.0 };
    static constexpr double kBackAvatar[4] = { .45, .82, 1.04, 1.10 };
    const double* back = m_outfit == Outfit::Avatar ? kBackAvatar : kBackClassic;
    const QQuaternion hipsQ = lean(torsoDeg * back[0], 1.f);
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
        case Role::Spine: q = lean(torsoDeg * back[1], .85f); break;
        case Role::Spine1: q = lean(torsoDeg * back[2], .65f); break;
        case Role::Spine2: q = lean(torsoDeg * back[3], .45f); break;
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

float GlbModel::saddleRaise(const BikeFit& fit) const
{
    if (!m_canRide || m_outfit != Outfit::Avatar || m_buttockVerts.empty())
        return 0.f;
    {
        QMutexLocker lock(&m_seatDrop.mutex);
        if (m_seatDrop.valid && m_seatDrop.saddleTop == fit.saddleTop && m_seatDrop.bottomBracket == fit.bottomBracket)
            return m_seatDrop.raise;
    }
    // the buttocks' lowest point over the back of the saddle (where the sit
    // bones rest), the lowest over a pedal turn: the saddle comes up to it
    // (the thighs hang lower beside and in front of it, between the legs)
    float lowest = 1e30f;
    std::vector<GhostVertex> posed;
    BikeFit f = fit;
    // (posed as on the bike, but the seat not flattened onto the saddle:
    // its top far below - the hips' height does not depend on it)
    f.saddleTop.setY(fit.saddleTop.y() - 1.f);
    for (int k = 0; k < 8; ++k) {
        f.crank = k * M_PI / 4;
        poseOnBike(f, posed);
        for (uint32_t i : m_buttockVerts) {
            const QVector3D& p = posed[i].position;
            if (std::abs(p.x()) < kSeatHalfWidth && p.z() > fit.saddleTop.z() - kSeatBack
                && p.z() < fit.saddleTop.z() - kSeatRearEnd)
                lowest = std::min(lowest, p.y());
        }
    }
    const float raise = lowest < 1e29f ? std::clamp(lowest - fit.saddleTop.y() + kSeatSink, 0.f, kSeatMaxRaise) : 0.f;
    QMutexLocker lock(&m_seatDrop.mutex);
    m_seatDrop = SeatDrop();
    m_seatDrop.valid = true;
    m_seatDrop.saddleTop = fit.saddleTop;
    m_seatDrop.bottomBracket = fit.bottomBracket;
    m_seatDrop.raise = raise;
    return raise;
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
    // the hips: as high as the legs reach at the bottom of the stroke (the
    // knee still a little bent), above the saddle
    QVector3D bottom;
    QQuaternion unused;
    pedalAt(1, M_PI, &bottom, &unused);
    const float hipZ = fit.saddleTop.z() + .01f;
    const float reach = kRiderLegM * .95f, dz = hipZ - bottom.z();
    const float hipY = bottom.y() + std::sqrt(std::max(.01f, reach * reach - dz * dz));
    const QVector3D hipBase(0, std::max(hipY, fit.saddleTop.y() + .02f), hipZ);
    // the grip on the brake hoods: the palm lies on top of the hood (the
    // wrist behind it), the knuckles just past its front and the fingers
    // curl down around it
    QVector3D wrist[2];
    for (int s = 0; s < 2; ++s)
        wrist[s] = fit.hood[s] + QVector3D(0, .000f, -.070f);   // (the hood may pass through the hand)
    const float arm = (m_jf[size_t(m_jForeArm[0])] - m_jf[size_t(m_jArm[0])]).length()
                      + (m_jf[size_t(m_jHand[0])] - m_jf[size_t(m_jForeArm[0])]).length();

    // The skeleton at crank angle `crank`: the feet on the pedals, the back leaning until the arms reach the hoods
    // comfortably (elbows bent).
    std::vector<QVector3D> P;
    std::vector<QQuaternion> Q;
    auto place = [&](double crank) {
        BikeFit f = fit;
        f.crank = crank;
        QVector3D ankle[2];
        QQuaternion footQ[2];
        for (int s = 0; s < 2; ++s)
            pedalAt(s, crank + (s == 0 ? M_PI : 0.0), &ankle[s], &footQ[s]);
        const QVector3D& hipCenter = hipBase;
        double best = 45, bestErr = 1e30;
        for (double deg = 20; deg <= 92; deg += 2) {
            solveBike(f, deg, P, Q, ankle, footQ, wrist, hipCenter);
            double err = 0;
            for (int s = 0; s < 2; ++s)
                err += std::abs((P[size_t(m_jArm[s])] - wrist[s]).length() - arm * .97f);   // arms nearly straight (this figure has short arms)
            if (err < bestErr) {
                bestErr = err;
                best = deg;
            }
        }
        solveBike(f, best, P, Q, ankle, footQ, wrist, hipCenter);
    };

    // A textured avatar sits ON the saddle: GhostRider raises the saddle to
    // the seat (saddleRaise()), and skinPosed() flattens what would sink
    // below the saddle's top (the crotch over the narrow nose) onto it.
    const bool seated = m_outfit == Outfit::Avatar && !m_seatVerts.empty();
    place(fit.crank);

    skinPosed(P, Q, out, seated ? &fit.saddleTop : nullptr);
    if (head) {
        const QQuaternion& qh = Q[size_t(m_jHead)];
        head->helmetCenter = P[size_t(m_jHead)] + qh.rotatedVector(m_helmetOffset);
        head->helmetRadii = m_helmetRadii;
        head->up = qh.rotatedVector(QVector3D(0, 1, 0));
        head->forward = qh.rotatedVector(QVector3D(0, 0, 1));
    }
}

QVector3D GlbModel::skinOne(const std::vector<QVector3D>& P, const std::vector<QQuaternion>& Q, size_t i) const
{
    QVector3D p(0, 0, 0);
    for (int k = 0; k < 4; ++k) {
        const float w = m_wgt[i][size_t(k)];
        if (w > 0) {
            const size_t j = m_jnt[i][size_t(k)];
            p += w * (P[j] + Q[j].rotatedVector(m_bf[i] - m_jf[j]));
        }
    }
    return p;
}

// The saddle's top (GhostRider's saddle: a wide back, a narrow nose) at
// (x, z), relative to its highest point; widened by a soft margin, falling
// away beyond it - what a seated body is flattened onto (skinPosed()).
float GlbModel::saddleSurface(float x, float z)
{
    struct Pad { float z, rx, rz; };
    // (ghost_rider.h: ellipsoids at saddle + (0, 0, -.05) radii .068 / .065
    // and + (0, 0, .04) radii .035 / .12, .022 / .018 high; saddleTop is
    // .022 above their centre, 4 cm in front of the back's centre)
    static constexpr Pad pads[2] = { { -.05f, .068f + .02f, .065f + .015f }, { .04f, .035f + .02f, .12f } };
    float h = -1e30f;
    for (const Pad& p : pads) {
        const float r = std::sqrt((x / p.rx) * (x / p.rx) + ((z - p.z) / p.rz) * ((z - p.z) / p.rz));
        // a dome inside, falling away steeply outside (continuous at the edge)
        const float y = r <= 1.f ? -.022f + .022f * (1.f - r * r) : -.022f - (r - 1.f) * .25f;
        h = std::max(h, y);
    }
    return h;
}

void GlbModel::skinPosed(const std::vector<QVector3D>& P, const std::vector<QQuaternion>& Q, std::vector<GhostVertex>& out,
                         const QVector3D* saddleTop) const
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
        if (i < m_same.size() && m_same[i] != i) {   // (skinned already: the same point)
            const GhostVertex& s = out[m_same[i]];
            out[i] = { s.position, s.normal, m_color[i] };
            continue;
        }
        QVector3D p(0, 0, 0), n(0, 0, 0);
        for (int k = 0; k < 4; ++k) {
            const float w = m_wgt[i][size_t(k)];
            if (w <= 0)
                continue;
            const size_t j = m_jnt[i][size_t(k)];
            p += w * (P[j] + rot(R[j], m_bf[i] - m_jf[j]));
            n += w * rot(R[j], m_nf[i]);
        }
        if (saddleTop && i < m_seatMask.size() && m_seatMask[i]) {   // flattened onto the saddle
            const QVector3D d = p - *saddleTop;
            const float top = saddleSurface(d.x(), d.z()) - kSeatSink;
            if (d.y() < top)
                p.setY(saddleTop->y() + top);
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
    // (a textured rower is modelled standing with a hollow lower back: its
    // pelvis tilts further back and the spine above bends a little more,
    // so the lower back is straight, not sway-backed)
    static constexpr double kRowBackClassic[4] = { -8, -5, -2, 0 };
    static constexpr double kRowBackAvatar[4] = { -22, -8, 0, 3 };
    const double* back = m_outfit == Outfit::RowerAvatar ? kRowBackAvatar : kRowBackClassic;
    const QQuaternion hipsQ = pitch(torsoDeg + back[0]);
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
        // a textured rower's shoulders raised a little (the clavicles turned
        // up): with the arms down from the model's T-pose its shoulders sag
        // and its neck looks long and thin
        if (m_outfit == Outfit::RowerAvatar)
            for (int k = 0; k < 2; ++k)
                if (j == m_jShoulder[k]) {
                    const float up = k == 0 ? -1.f : 1.f;   // (left: x < 0)
                    q = QQuaternion::fromAxisAndAngle(qp.rotatedVector(QVector3D(0, 0, 1)), up * kRowShoulderUpDeg) * qp;
                }
        switch (info.role) {
        case Role::Hips: q = hipsQ; break;
        case Role::Spine: q = pitch(torsoDeg + back[1]); break;
        case Role::Spine1: q = pitch(torsoDeg + back[2]); break;
        case Role::Spine2: q = pitch(torsoDeg + back[3]); break;
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
