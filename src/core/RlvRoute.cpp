#include "RlvRoute.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {

constexpr double kEarthRadiusM = 6371008.8;
constexpr double kPi = 3.14159265358979323846;

// HTML 4 Latin-1 entities, code points 160..255 in order.
const char *const kLatin1Entities[] = {
    "nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo",
    "not", "shy", "reg", "macr", "deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot",
    "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest", "Agrave", "Aacute", "Acirc",
    "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute",
    "Icirc", "Iuml", "ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash",
    "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig", "agrave", "aacute", "acirc", "atilde",
    "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc",
    "iuml", "eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave",
    "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml"
};
static_assert(sizeof(kLatin1Entities) / sizeof(kLatin1Entities[0]) == 96, "Latin-1 entity table");

const QHash<QString, uint> &entityTable()
{
    static const QHash<QString, uint> table = [] {
        QHash<QString, uint> t;
        for (int i = 0; i < 96; ++i)
            t.insert(QString::fromLatin1(kLatin1Entities[i]), uint(160 + i));
        const std::pair<const char *, uint> extra[] = {
            { "euro", 8364 }, { "ndash", 8211 }, { "mdash", 8212 }, { "lsquo", 8216 }, { "rsquo", 8217 },
            { "sbquo", 8218 }, { "ldquo", 8220 }, { "rdquo", 8221 }, { "bdquo", 8222 }, { "hellip", 8230 },
            { "bull", 8226 }, { "trade", 8482 }, { "OElig", 338 }, { "oelig", 339 }, { "Scaron", 352 },
            { "scaron", 353 }, { "Yuml", 376 }, { "dagger", 8224 }, { "permil", 8240 }, { "prime", 8242 },
            { "rarr", 8594 }, { "larr", 8592 }, { "uarr", 8593 }, { "darr", 8595 }
        };
        for (const auto &e : extra)
            t.insert(QString::fromLatin1(e.first), e.second);
        return t;
    }();
    return table;
}

bool isXmlEntity(const QString &name)
{
    return name == QLatin1String("amp") || name == QLatin1String("lt") || name == QLatin1String("gt")
           || name == QLatin1String("quot") || name == QLatin1String("apos");
}

// Replaces "&name;" by replace(name) when it returns a non-null string.
template<typename F>
QString mapEntities(const QString &in, F replace)
{
    QString out;
    out.reserve(in.size());
    int i = 0;
    while (i < in.size()) {
        const QChar c = in.at(i);
        if (c == QLatin1Char('&')) {
            int j = i + 1;
            while (j < in.size() && j - i <= 10 && in.at(j).isLetterOrNumber() && in.at(j).unicode() < 128)
                ++j;
            if (j > i + 1 && j < in.size() && in.at(j) == QLatin1Char(';') && in.at(i + 1).isLetter()) {
                const QString r = replace(in.mid(i + 1, j - i - 1));
                if (!r.isNull()) {
                    out += r;
                    i = j + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

double parseNumber(QString s, bool *ok = nullptr)
{
    s = s.trimmed();
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    return s.toDouble(ok);
}

QString resolveFile(const QDir &dir, QString path)
{
    path = path.trimmed();
    if (path.isEmpty())
        return {};
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QFileInfo fi(QDir::isAbsolutePath(path) ? path : dir.filePath(path));
    if (fi.isFile())
        return fi.absoluteFilePath();
    // a path from another machine: try the file name next to the xml
    const QFileInfo local(dir.filePath(QFileInfo(path).fileName()));
    return local.isFile() ? local.absoluteFilePath() : QString();
}

} // namespace

const QStringList &RlvRoute::videoSuffixes()
{
    // (the server's media library takes the same: MEDIA_VIDEO_EXTENSIONS in server/lib/media.php)
    static const QStringList s { QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"),
                                 QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("webm") };
    return s;
}

// --- helpers --------------------------------------------------------------------

double RlvRoute::haversine(double lat1, double lon1, double lat2, double lon2)
{
    const double r = kPi / 180.0;
    const double dLat = (lat2 - lat1) * r;
    const double dLon = (lon2 - lon1) * r;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                     + std::cos(lat1 * r) * std::cos(lat2 * r) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusM * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}

QByteArray RlvRoute::replaceHtmlEntities(const QByteArray &xml)
{
    if (!xml.contains('&'))
        return xml;
    // UTF-16 / UTF-32 (byte order mark or NUL bytes): left as it is - the
    // reader decodes it; entity names are not single bytes there.
    if (xml.startsWith("\xFF\xFE") || xml.startsWith("\xFE\xFF") || xml.left(512).contains('\0'))
        return xml;
    // Works on the raw bytes: only ASCII "&name;" sequences are replaced (by
    // ASCII "&#N;"), every other byte is kept, so the file's own encoding
    // (UTF-8, ISO-8859-1 / Windows-1252 per its XML declaration) stays intact.
    auto isAlpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    auto isAlnum = [&](char c) { return isAlpha(c) || (c >= '0' && c <= '9'); };
    QByteArray out;
    out.reserve(xml.size() + 64);
    const int n = int(xml.size());
    int i = 0;
    while (i < n) {
        const char c = xml.at(i);
        if (c == '&') {
            int j = i + 1;
            while (j < n && j - i <= 10 && isAlnum(xml.at(j)))
                ++j;
            if (j > i + 1 && j < n && xml.at(j) == ';' && isAlpha(xml.at(i + 1))) {
                const QString name = QString::fromLatin1(xml.constData() + i + 1, j - i - 1);
                if (!isXmlEntity(name)) {
                    const auto it = entityTable().constFind(name);
                    if (it != entityTable().constEnd())
                        out += "&#" + QByteArray::number(it.value()) + ';';
                    else
                        out += "&amp;" + name.toLatin1() + ';';   // unknown: keep as text
                    i = j + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

QString RlvRoute::decodeText(const QString &text)
{
    QString out = mapEntities(text, [](const QString &name) -> QString {
        static const QHash<QString, QString> xml { { QStringLiteral("amp"), QStringLiteral("&") },
                                                   { QStringLiteral("lt"), QStringLiteral("<") },
                                                   { QStringLiteral("gt"), QStringLiteral(">") },
                                                   { QStringLiteral("quot"), QStringLiteral("\"") },
                                                   { QStringLiteral("apos"), QStringLiteral("'") } };
        if (xml.contains(name))
            return xml.value(name);
        const auto it = entityTable().constFind(name);
        if (it != entityTable().constEnd()) {
            const char32_t cp = char32_t(it.value());
            return QString::fromUcs4(&cp, 1);
        }
        return QString();
    });
    out.replace(QLatin1String("\\n"), QLatin1String("\n"));
    out.replace(QChar(0xA0), QLatin1Char(' '));
    return out.trimmed();
}

bool RlvRoute::isRouteXml(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QXmlStreamReader xml(replaceHtmlEntities(f.read(4096)));
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement)
            return xml.name() == QLatin1String("gpx-import");
    }
    return false;
}

QString RlvRoute::findVideoFor(const QString &path)
{
    const QFileInfo fi(path);
    const QDir dir = fi.absoluteDir();
    for (const QString &suffix : videoSuffixes()) {
        for (const QString &s : { suffix, suffix.toUpper() }) {
            const QFileInfo v(dir.filePath(fi.completeBaseName() + QLatin1Char('.') + s));
            if (v.isFile())
                return v.absoluteFilePath();
        }
    }
    QStringList filters;
    for (const QString &s : videoSuffixes())
        filters << QStringLiteral("*.") + s << QStringLiteral("*.") + s.toUpper();
    const QFileInfoList videos = dir.entryInfoList(filters, QDir::Files);
    return videos.size() == 1 ? videos.constFirst().absoluteFilePath() : QString();
}

bool RlvRoute::hasVideo() const
{
    return !m_videoPath.isEmpty() && QFileInfo(m_videoPath).isFile();
}

double RlvRoute::videoOffset() const
{
    return m_framerate > 0 ? std::max(0, m_startFrame - 1) / m_framerate : 0.0;
}

double RlvRoute::trackDuration() const
{
    if (m_points.size() < 2)
        return 0.0;
    const double last = m_points.constLast().time;
    return last + (last - m_points.at(m_points.size() - 2).time);
}

double RlvRoute::videoDuration() const
{
    if (m_endFrame > 0 && m_framerate > 0)
        return m_endFrame / m_framerate;
    return videoOffset() + trackDuration();
}

// --- loading ---------------------------------------------------------------------

RlvRoute RlvRoute::fromPath(const QString &path, QString *error)
{
    RlvRoute r;
    r.load(path, error);
    return r;
}

bool RlvRoute::load(const QString &pathIn, QString *error)
{
    *this = RlvRoute();
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        m_points.clear();
        return false;
    };
    const QFileInfo fi(pathIn);
    if (!fi.exists())
        return fail(QStringLiteral("Not found: %1").arg(pathIn));
    QString file;
    if (fi.isDir()) {
        const QDir dir(fi.absoluteFilePath());
        const QFileInfoList xmls = dir.entryInfoList({ QStringLiteral("*.xml"), QStringLiteral("*.XML") }, QDir::Files,
                                                     QDir::Name);
        for (const QFileInfo &x : xmls) {
            if (isRouteXml(x.absoluteFilePath())) {
                file = x.absoluteFilePath();
                break;
            }
        }
        if (file.isEmpty()) {
            const QFileInfoList gpxs = dir.entryInfoList({ QStringLiteral("*.gpx"), QStringLiteral("*.GPX") },
                                                         QDir::Files, QDir::Name);
            for (const QFileInfo &g : gpxs) {
                if (!findVideoFor(g.absoluteFilePath()).isEmpty()) {
                    file = g.absoluteFilePath();
                    break;
                }
            }
            if (file.isEmpty() && gpxs.size() == 1)
                file = gpxs.constFirst().absoluteFilePath();   // loads, but reports the missing video
        }
        if (file.isEmpty())
            return fail(QStringLiteral("No route (.xml or .gpx) in %1").arg(dir.absolutePath()));
    } else {
        file = fi.absoluteFilePath();
    }

    const QString suffix = QFileInfo(file).suffix().toLower();
    QString err;
    bool ok = false;
    if (suffix == QLatin1String("xml")) {
        ok = loadXml(file, &err);
    } else if (suffix == QLatin1String("gpx")) {
        m_name = QFileInfo(file).completeBaseName();
        m_videoPath = findVideoFor(file);
        ok = loadGpx(file, &err);
    } else {
        return fail(QStringLiteral("Unsupported file: %1").arg(file));
    }
    if (!ok)
        return fail(err);
    m_source = file;
    m_folder = QFileInfo(file).absolutePath();
    // (derived values: computed by setPoints() - every loader ends there)
    if (!isValid())
        return fail(m_points.size() < 2 ? QStringLiteral("The track has fewer than two points")
                                        : QStringLiteral("The track is shorter than %1 m").arg(int(kMinDistanceM)));
    if (error)
        *error = hasVideo() ? QString() : QStringLiteral("Video file not found");
    return true;
}

bool RlvRoute::loadXml(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read %1").arg(path);
        return false;
    }
    QXmlStreamReader xml(replaceHtmlEntities(f.readAll()));
    QString videoRel, gpxRel;
    bool root = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto n = xml.name();
        if (!root) {
            if (n != QLatin1String("gpx-import")) {
                *error = QStringLiteral("Not a gpx-import file: %1").arg(path);
                return false;
            }
            root = true;
            continue;
        }
        if (n == QLatin1String("information")) {
            const QXmlStreamAttributes a = xml.attributes();
            RlvInformation info;
            info.distance = parseNumber(a.value(QLatin1String("distance")).toString());
            info.textEn = decodeText(a.value(QLatin1String("en")).toString());
            info.textDe = decodeText(a.value(QLatin1String("de")).toString());
            info.text = !info.textEn.isEmpty() ? info.textEn : info.textDe;
            if (info.text.isEmpty()) {
                for (const QXmlStreamAttribute &at : a) {
                    if (at.name() != QLatin1String("distance") && !at.value().trimmed().isEmpty()) {
                        info.text = decodeText(at.value().toString());
                        break;
                    }
                }
            }
            if (!info.text.isEmpty() && info.distance >= 0)
                m_infos.append(info);
            xml.skipCurrentElement();
        } else if (n == QLatin1String("informations")) {
            continue;   // children handled above
        } else {
            const QString name = n.toString();
            const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if (name == QLatin1String("name"))
                m_name = decodeText(text);
            else if (name == QLatin1String("title"))
                m_title = decodeText(text);
            else if (name == QLatin1String("country"))
                m_country = text;
            else if (name == QLatin1String("id"))
                m_id = text;
            else if (name == QLatin1String("video-file-path"))
                videoRel = text;
            else if (name == QLatin1String("gpx-file-path"))
                gpxRel = text;
            else if (name == QLatin1String("framerate")) {
                bool ok = false;
                const double v = parseNumber(text, &ok);
                if (ok && v > 0 && v < 1000)
                    m_framerate = v;
            } else if (name == QLatin1String("start-frame")) {
                m_startFrame = std::max(1, int(std::lround(parseNumber(text))));
            } else if (name == QLatin1String("end-frame")) {
                m_endFrame = std::max(0, int(std::lround(parseNumber(text))));
            }
        }
    }
    if (xml.hasError()) {
        *error = QStringLiteral("%1: %2 (line %3)").arg(QFileInfo(path).fileName(), xml.errorString())
                     .arg(xml.lineNumber());
        return false;
    }
    std::sort(m_infos.begin(), m_infos.end(),
              [](const RlvInformation &a, const RlvInformation &b) { return a.distance < b.distance; });
    const QDir dir = QFileInfo(path).absoluteDir();
    if (m_name.isEmpty())
        m_name = QFileInfo(path).completeBaseName();
    QString gpx = resolveFile(dir, gpxRel);
    if (gpx.isEmpty()) {
        const QFileInfo same(dir.filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".gpx")));
        if (same.isFile())
            gpx = same.absoluteFilePath();
    }
    if (gpx.isEmpty()) {
        *error = QStringLiteral("GPS track not found (%1)").arg(gpxRel.isEmpty() ? QStringLiteral(".gpx") : gpxRel);
        return false;
    }
    m_videoPath = resolveFile(dir, videoRel);
    if (m_videoPath.isEmpty())
        m_videoPath = findVideoFor(path);
    return loadGpx(gpx, error);
}

bool RlvRoute::loadGpx(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read %1").arg(path);
        return false;
    }
    m_gpxPath = QFileInfo(path).absoluteFilePath();
    QXmlStreamReader xml(replaceHtmlEntities(f.readAll()));
    QVector<RlvPoint> pts;
    QVector<bool> hasTime, hasEle;
    QDateTime t0;
    RlvPoint cur;
    bool inPoint = false, curHasTime = false, curHasEle = false;
    QString trackName;
    int depthTrk = 0;
    QVector<RouteSegment> segs;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto n = xml.name();
            if (n == QLatin1String("trkpt") || n == QLatin1String("rtept")) {
                cur = RlvPoint();
                inPoint = true;
                curHasTime = false;
                curHasEle = false;
                bool okLat = false, okLon = false;
                cur.lat = xml.attributes().value(QLatin1String("lat")).toDouble(&okLat);
                cur.lon = xml.attributes().value(QLatin1String("lon")).toDouble(&okLon);
                if (!okLat || !okLon) {
                    inPoint = false;
                    xml.skipCurrentElement();
                }
            } else if (n == QLatin1String("trk")) {
                ++depthTrk;
            } else if (inPoint && n == QLatin1String("ele")) {
                bool ok = false;
                const double e = parseNumber(xml.readElementText(), &ok);
                if (ok && std::isfinite(e)) {
                    cur.rawElevation = e;
                    curHasEle = true;
                }
            } else if (inPoint && n == QLatin1String("time")) {
                const QString s = xml.readElementText().trimmed();
                QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
                if (!dt.isValid())
                    dt = QDateTime::fromString(s, Qt::ISODate);
                if (dt.isValid()) {
                    if (!t0.isValid())
                        t0 = dt;
                    cur.time = t0.msecsTo(dt) / 1000.0;
                    curHasTime = true;
                }
            } else if (inPoint && n == QLatin1String("gps")) {
                bool ok = false;
                const double kmh = parseNumber(xml.attributes().value(QLatin1String("speed")).toString(), &ok);
                if (ok && kmh >= 0)
                    cur.gpsSpeed = kmh / 3.6;
            } else if (inPoint && n == QLatin1String("hr")) {
                cur.heartRate = int(std::lround(parseNumber(xml.readElementText())));
            } else if (inPoint && n == QLatin1String("cad")) {
                cur.cadence = int(std::lround(parseNumber(xml.readElementText())));
            } else if (!inPoint && n == QLatin1String("segments")
                       && xml.namespaceUri() == QLatin1String(segments::kNamespace)) {
                bool ok = false;
                const double g = xml.attributes().value(QLatin1String("maxGrade")).toDouble(&ok);
                if (ok && g > 0)
                    m_maxCleanGrade = std::clamp(g, 5.0, 30.0);
            } else if (!inPoint && n == QLatin1String("segment")
                       && xml.namespaceUri() == QLatin1String(segments::kNamespace)) {
                const auto a = xml.attributes();
                RouteSegment s;
                s.kind = RouteSegment::kindFromName(a.value(QLatin1String("kind")));
                bool okS = false, okF = false;
                s.start = a.value(QLatin1String("start")).toDouble(&okS);
                s.finish = a.value(QLatin1String("finish")).toDouble(&okF);
                s.name = a.value(QLatin1String("name")).toString();
                s.left = a.value(QLatin1String("side")) == QLatin1String("left");
                if (okS && okF)
                    segs.append(s);
            } else if (!inPoint && depthTrk > 0 && n == QLatin1String("name") && trackName.isEmpty()) {
                trackName = xml.readElementText().trimmed();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const auto n = xml.name();
            if (inPoint && (n == QLatin1String("trkpt") || n == QLatin1String("rtept"))) {
                pts.append(cur);
                hasTime.append(curHasTime);
                hasEle.append(curHasEle);
                inPoint = false;
            } else if (n == QLatin1String("trk")) {
                --depthTrk;
            }
        }
    }
    if (xml.hasError()) {
        *error = QStringLiteral("%1: %2 (line %3)").arg(QFileInfo(path).fileName(), xml.errorString())
                     .arg(xml.lineNumber());
        return false;
    }
    // Points without a time stamp: 1 s after the previous one.
    for (int i = 0; i < pts.size(); ++i) {
        if (!hasTime.at(i))
            pts[i].time = i > 0 ? pts.at(i - 1).time + 1.0 : 0.0;
    }
    fillMissingElevation(pts, hasEle);
    if (m_title.isEmpty() && !trackName.isEmpty() && trackName != m_name)
        m_title = trackName;
    if (!setPoints(std::move(pts), error))
        return false;
    // the workshop's segments: one by one, so a bad one does not drop the rest
    std::sort(segs.begin(), segs.end(), [](const RouteSegment &a, const RouteSegment &b) { return a.start < b.start; });
    for (const RouteSegment &s : segs) {
        QVector<RouteSegment> &list = s.timed() ? m_segments : s.kind == RouteSegment::Place ? m_places : m_gravel;
        QVector<RouteSegment> trial = list;
        trial.append(s);
        if (segments::validate(trial, totalDistance()))
            list = trial;
    }
    return true;
}

bool RlvRoute::gravelAt(double d) const
{
    // (few sections: a scan)
    for (const RouteSegment &g : m_gravel) {
        if (d < g.start)
            return false;
        if (d < g.finish)
            return true;
    }
    return false;
}

double RlvRoute::gravelBlend(double d, double rampM) const
{
    for (const RouteSegment &g : m_gravel) {
        if (d < g.start)
            return 0.0;
        if (d < g.finish) {
            const double ramp = std::min(rampM, 0.5 * g.length());
            if (ramp <= 0)
                return 1.0;
            return std::clamp(std::min(d - g.start, g.finish - d) / ramp, 0.0, 1.0);
        }
    }
    return 0.0;
}

QVector<RouteSegment> RlvRoute::allSegments() const
{
    QVector<RouteSegment> all = m_segments;
    all += m_gravel;
    all += m_places;
    std::stable_sort(all.begin(), all.end(), [](const RouteSegment &a, const RouteSegment &b) { return a.start < b.start; });
    return all;
}

void RlvRoute::fillMissingElevation(QVector<RlvPoint> &pts, const QVector<bool> &hasEle)
{
    // Points without <ele>: linear in distance between the neighbours that
    // have one; before the first / after the last known value it is carried.
    // No elevation at all: a flat 0 m.
    const int n = int(pts.size());
    if (hasEle.size() != n)
        return;
    QVector<double> along(n, 0.0);
    for (int i = 1; i < n; ++i)
        along[i] = along.at(i - 1) + haversine(pts.at(i - 1).lat, pts.at(i - 1).lon, pts.at(i).lat, pts.at(i).lon);
    int prev = -1;
    for (int i = 0; i < n; ++i) {
        if (!hasEle.at(i))
            continue;
        if (prev < 0) {
            for (int k = 0; k < i; ++k)
                pts[k].rawElevation = pts.at(i).rawElevation;
        } else {
            const double span = along.at(i) - along.at(prev);
            for (int k = prev + 1; k < i; ++k) {
                const double f = span > 0 ? (along.at(k) - along.at(prev)) / span : double(k - prev) / (i - prev);
                pts[k].rawElevation = pts.at(prev).rawElevation + (pts.at(i).rawElevation - pts.at(prev).rawElevation) * f;
            }
        }
        prev = i;
    }
    if (prev < 0)
        return;   // none: rawElevation stays 0
    for (int k = prev + 1; k < n; ++k)
        pts[k].rawElevation = pts.at(prev).rawElevation;
}

bool RlvRoute::setPoints(QVector<RlvPoint> pts, QString *error)
{
    // keep strictly increasing times
    QVector<RlvPoint> clean;
    clean.reserve(pts.size());
    for (const RlvPoint &p : pts) {
        if (!std::isfinite(p.lat) || !std::isfinite(p.lon) || !std::isfinite(p.time))
            continue;
        if (!clean.isEmpty() && p.time <= clean.constLast().time)
            continue;
        clean.append(p);
    }
    if (!clean.isEmpty()) {
        const double t0 = clean.constFirst().time;
        for (RlvPoint &p : clean)
            p.time -= t0;
    }
    m_points = std::move(clean);
    if (m_points.size() < 2) {
        if (error)
            *error = QStringLiteral("The track has fewer than two points");
        return false;
    }
    computeDerived();
    if (totalDistance() < kMinDistanceM) {
        if (error)
            *error = QStringLiteral("The track is shorter than %1 m").arg(int(kMinDistanceM));
        return false;
    }
    return true;
}

void RlvRoute::computeDerived()
{
    const int n = m_points.size();
    if (n < 2)
        return;
    const double offset = videoOffset();
    double dist = 0;
    for (int i = 0; i < n; ++i) {
        RlvPoint &p = m_points[i];
        p.videoTime = p.time + offset;
        if (i > 0) {
            const RlvPoint &q = m_points.at(i - 1);
            double d = haversine(q.lat, q.lon, p.lat, p.lon);
            if (p.gpsSpeed >= 0 && p.gpsSpeed * 3.6 < kStopSpeedKmh)
                d = 0;   // standing: ignore GPS jitter
            dist += d;
        }
        p.distance = dist;
    }

    cleanElevations();

    // elevation: mean of the cleaned (piecewise linear) profile over
    // [d - window/2, d + window/2] - exact integral, so standing points and
    // uneven point spacing do not bias it
    QVector<double> integral(n, 0.0);
    for (int i = 1; i < n; ++i) {
        const RlvPoint &a = m_points.at(i - 1);
        const RlvPoint &b = m_points.at(i);
        integral[i] = integral.at(i - 1) + (b.distance - a.distance) * (a.cleanElevation + b.cleanElevation) / 2.0;
    }
    const double total = dist;
    auto integralAt = [&](double x) {
        if (x <= 0)
            return 0.0;
        if (x >= total)
            return integral.constLast();
        const int i = segmentAt(x);
        const RlvPoint &a = m_points.at(i);
        const RlvPoint &b = m_points.at(i + 1);
        const double span = b.distance - a.distance;
        const double f = span > 0 ? (x - a.distance) / span : 0.0;
        const double ex = a.cleanElevation + (b.cleanElevation - a.cleanElevation) * f;
        return integral.at(i) + (x - a.distance) * (a.cleanElevation + ex) / 2.0;
    };
    const double half = kElevationWindowM / 2.0;
    for (int i = 0; i < n; ++i) {
        const double d = m_points.at(i).distance;
        const double h = std::min({ half, d, total - d });   // symmetric, shrinks at the ends
        const double a = d - h;
        const double b = d + h;
        m_points[i].elevation = b - a > 1e-6 ? (integralAt(b) - integralAt(a)) / (b - a) : m_points.at(i).cleanElevation;
    }

    // grade over ±kGradeWindowM/2 of smoothed elevation
    const double g = kGradeWindowM / 2.0;
    for (int i = 0; i < n; ++i) {
        const double d = m_points.at(i).distance;
        const double a = std::max(0.0, d - g);
        const double b = std::min(total, d + g);
        double grade = 0;
        if (b - a > 5.0)
            grade = (interpolate(b, &RlvPoint::elevation) - interpolate(a, &RlvPoint::elevation)) / (b - a) * 100.0;
        m_points[i].grade = std::clamp(grade, kMinGrade, kMaxGrade);
    }

    // filmed speed: central difference over ±2 points
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - 2);
        const int b = std::min(n - 1, i + 2);
        const double dt = m_points.at(b).time - m_points.at(a).time;
        m_points[i].speed = dt > 0 ? (m_points.at(b).distance - m_points.at(a).distance) / dt : 0.0;
    }

    // ascent / descent with hysteresis on the smoothed elevation
    m_ascent = m_descent = 0;
    double ref = m_points.constFirst().elevation;
    m_minEle = m_maxEle = ref;
    for (int i = 0; i < n; ++i) {
        const double e = m_points.at(i).elevation;
        m_minEle = std::min(m_minEle, e);
        m_maxEle = std::max(m_maxEle, e);
        if (e - ref >= kAscentThresholdM) {
            m_ascent += e - ref;
            ref = e;
        } else if (ref - e >= kAscentThresholdM) {
            m_descent += ref - e;
            ref = e;
        }
        m_points[i].ascent = m_ascent;
    }
    // what is left below the threshold at the end still counts
    const double end = m_points.constLast().elevation;
    if (end > ref)
        m_ascent += end - ref;
    else
        m_descent += ref - end;
    m_points[n - 1].ascent = m_ascent;
}

void RlvRoute::cleanElevations()
{
    const int n = m_points.size();
    m_spikes = m_limited = 0;
    m_rawMaxGrade = 0;
    for (RlvPoint &p : m_points)
        p.cleanElevation = p.rawElevation;
    const double total = m_points.constLast().distance;
    if (n < 3 || total < 2 * kCleanStepM)
        return;
    // the raw track on a regular grid
    const int m = int(std::ceil(total / kCleanStepM)) + 1;
    const double step = total / (m - 1);
    std::vector<double> raw(static_cast<size_t>(m)), e;
    for (int k = 0, i = 0; k < m; ++k) {
        const double x = k * step;
        while (i < n - 2 && m_points.at(i + 1).distance < x)
            ++i;
        const RlvPoint &a = m_points.at(i), &b = m_points.at(i + 1);
        const double span = b.distance - a.distance;
        const double f = span > 0 ? std::clamp((x - a.distance) / span, 0.0, 1.0) : 0.0;
        raw[size_t(k)] = a.rawElevation + (b.rawElevation - a.rawElevation) * f;
    }
    {   // the steepest 40 m grade as recorded
        const int w = std::max(1, int(std::lround(40.0 / step)));
        for (int k = 0; k + w < m; ++k)
            m_rawMaxGrade = std::max(m_rawMaxGrade, std::abs(raw[size_t(k + w)] - raw[size_t(k)]) / (w * step) * 100.0);
    }
    // 1. spikes: a Hampel filter (median and median absolute deviation)
    e = raw;
    const int half = std::max(1, int(std::lround(kSpikeWindowM / step)));
    std::vector<double> win, dev;
    for (int k = 0; k < m; ++k) {
        win.clear();
        for (int j = std::max(0, k - half); j <= std::min(m - 1, k + half); ++j)
            if (j != k)
                win.push_back(raw[size_t(j)]);
        std::nth_element(win.begin(), win.begin() + win.size() / 2, win.end());
        const double med = win[win.size() / 2];
        dev.clear();
        for (double v : win)
            dev.push_back(std::abs(v - med));
        std::nth_element(dev.begin(), dev.begin() + dev.size() / 2, dev.end());
        const double mad = 1.4826 * dev[dev.size() / 2];
        const double off = std::abs(raw[size_t(k)] - med);
        if (off > kSpikeM && off > kSpikeMads * mad) {
            e[size_t(k)] = med;
            ++m_spikes;
        }
    }
    // 2. grade limit: share each step's excess with its neighbours until none
    //    is steeper than kMaxCleanGrade (the sum of the heights is kept)
    const double limit = maxCleanGrade() / 100.0 * step;
    std::vector<char> touched(size_t(m), 0);
    const int passes = std::clamp(int(3e8 / m), 500, 20000);   // bounded work on long routes
    for (int pass = 0; pass < passes; ++pass) {
        double worst = 0;
        for (int k = 0; k + 1 < m; ++k) {
            const double diff = e[size_t(k + 1)] - e[size_t(k)];
            const double excess = std::abs(diff) - limit;
            if (excess <= 1e-4)
                continue;
            const double moved = (diff > 0 ? excess : -excess) / 2.0;
            e[size_t(k)] += moved;
            e[size_t(k + 1)] -= moved;
            touched[size_t(k)] = touched[size_t(k + 1)] = 1;
            worst = std::max(worst, excess);
        }
        if (worst < 0.01)
            break;
    }
    m_limited = int(std::count(touched.begin(), touched.end(), 1));
    if (m_spikes == 0 && m_limited == 0)
        return;
    // back onto the points: the raw value plus the grid's correction there
    for (int i = 0; i < n; ++i) {
        RlvPoint &p = m_points[i];
        const double x = std::clamp(p.distance / step, 0.0, double(m - 1));
        const int k = std::min(m - 2, int(x));
        const double f = x - k;
        const double corr = (e[size_t(k)] - raw[size_t(k)]) * (1 - f) + (e[size_t(k + 1)] - raw[size_t(k + 1)]) * f;
        // a point between grid points can itself be a spike: take the grid
        const double gridValue = e[size_t(k)] * (1 - f) + e[size_t(k + 1)] * f;
        p.cleanElevation = std::abs(p.rawElevation + corr - gridValue) > kSpikeM ? gridValue : p.rawElevation + corr;
    }
}

void RlvRoute::setMaxCleanGrade(double percent)
{
    const double v = percent > 0 ? std::clamp(percent, 5.0, 30.0) : 0.0;
    if (v == m_maxCleanGrade)
        return;
    m_maxCleanGrade = v;
    if (m_points.size() >= 2)
        computeDerived();
}

double RlvRoute::steepestGrade() const
{
    double best = 0;
    const double total = totalDistance();
    for (double d = 0; d + 40 <= total; d += 10)
        best = std::max(best, std::abs(elevationAtDistance(d + 40) - elevationAtDistance(d)) / 40.0 * 100.0);
    return best;
}

// --- lookups ---------------------------------------------------------------------------

int RlvRoute::segmentAt(double d) const
{
    // last index with distance <= d, limited to n - 2 (segment start)
    const auto it = std::upper_bound(m_points.cbegin(), m_points.cend(), d,
                                     [](double v, const RlvPoint &p) { return v < p.distance; });
    const int i = int(it - m_points.cbegin()) - 1;
    return std::clamp(i, 0, int(m_points.size()) - 2);
}

double RlvRoute::interpolate(double d, double RlvPoint::*field) const
{
    if (m_points.isEmpty())
        return 0.0;
    if (m_points.size() == 1 || d <= m_points.constFirst().distance)
        return m_points.constFirst().*field;
    if (d >= m_points.constLast().distance)
        return m_points.constLast().*field;
    const int i = segmentAt(d);
    const RlvPoint &a = m_points.at(i);
    const RlvPoint &b = m_points.at(i + 1);
    const double span = b.distance - a.distance;
    const double f = span > 0 ? (d - a.distance) / span : 1.0;
    return a.*field + (b.*field - a.*field) * f;
}

double RlvRoute::videoTimeAtDistance(double d) const
{
    if (m_points.size() < 2)
        return videoOffset();
    if (d >= totalDistance())
        return m_points.constLast().videoTime;
    d = std::max(0.0, d);
    // last point with distance <= d -> end of a stop at that distance
    const int i = segmentAt(d);
    const RlvPoint &a = m_points.at(i);
    const RlvPoint &b = m_points.at(i + 1);
    const double span = b.distance - a.distance;
    const double f = span > 0 ? std::clamp((d - a.distance) / span, 0.0, 1.0) : 1.0;
    return a.videoTime + (b.videoTime - a.videoTime) * f;
}

double RlvRoute::distanceAtVideoTime(double t) const
{
    if (m_points.size() < 2)
        return 0.0;
    if (t <= m_points.constFirst().videoTime)
        return 0.0;
    if (t >= m_points.constLast().videoTime)
        return totalDistance();
    const auto it = std::upper_bound(m_points.cbegin(), m_points.cend(), t,
                                     [](double v, const RlvPoint &p) { return v < p.videoTime; });
    const int i = std::clamp(int(it - m_points.cbegin()) - 1, 0, int(m_points.size()) - 2);
    const RlvPoint &a = m_points.at(i);
    const RlvPoint &b = m_points.at(i + 1);
    const double f = (t - a.videoTime) / (b.videoTime - a.videoTime);
    return a.distance + (b.distance - a.distance) * f;
}

double RlvRoute::gradeAtDistance(double d) const { return interpolate(d, &RlvPoint::grade); }
double RlvRoute::gradeAheadAtDistance(double d) const
{
    if (m_points.size() < 2)
        return 0.0;
    const double total = totalDistance();
    const double a = std::clamp(d - kFeltBehindM, 0.0, total);
    const double b = std::clamp(d + kFeltAheadM, 0.0, total);
    if (b - a <= 5.0)
        return gradeAtDistance(d);
    const double g = (interpolate(b, &RlvPoint::elevation) - interpolate(a, &RlvPoint::elevation)) / (b - a) * 100.0;
    return std::clamp(g, kMinGrade, kMaxGrade);
}

double RlvRoute::elevationAtDistance(double d) const { return interpolate(d, &RlvPoint::elevation); }
double RlvRoute::gradeBackAtDistance(double d) const
{
    if (m_points.size() < 2)
        return 0.0;
    const double total = totalDistance();
    const double a = std::clamp(d - kFeltAheadM, 0.0, total);
    const double b = std::clamp(d + kFeltBehindM, 0.0, total);
    if (b - a <= 5.0)
        return -gradeAtDistance(d);
    const double g = (interpolate(a, &RlvPoint::elevation) - interpolate(b, &RlvPoint::elevation)) / (b - a) * 100.0;
    return std::clamp(g, kMinGrade, kMaxGrade);
}

bool RlvRoute::isRoundTrip() const
{
    if (m_points.size() < 2)
        return false;
    const RlvPoint &a = m_points.constFirst(), &b = m_points.constLast();
    return haversine(a.lat, a.lon, b.lat, b.lon) < kRoundTripM;
}

double RlvRoute::ascentAtDistance(double d) const { return interpolate(d, &RlvPoint::ascent); }

double RlvRoute::recordedSpeedAtDistance(double d) const
{
    return std::max(0.0, interpolate(d, &RlvPoint::speed));
}

QPointF RlvRoute::positionAtDistance(double d) const
{
    return QPointF(interpolate(d, &RlvPoint::lon), interpolate(d, &RlvPoint::lat));
}

QVector<QPointF> RlvRoute::profile(int samples) const
{
    QVector<QPointF> out;
    if (!isValid() || samples < 2)
        return out;
    out.reserve(samples);
    const double total = totalDistance();
    for (int i = 0; i < samples; ++i) {
        const double d = total * i / (samples - 1);
        out.append(QPointF(d, elevationAtDistance(d)));
    }
    return out;
}

int RlvRoute::informationIndexAt(double d) const
{
    int found = -1;
    for (int i = 0; i < m_infos.size(); ++i) {
        const RlvInformation &info = m_infos.at(i);
        if (info.distance > d)
            break;
        const double show = std::max(kInfoMinShowM, kInfoMinShowSec * recordedSpeedAtDistance(info.distance));
        if (d < info.distance + show)
            found = i;
    }
    return found;
}

QString RlvRoute::informationAt(double d) const
{
    const int i = informationIndexAt(d);
    return i >= 0 ? m_infos.at(i).text : QString();
}

} // namespace watt
