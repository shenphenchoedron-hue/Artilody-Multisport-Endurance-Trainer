#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

namespace watt {

// One GPS track point of a Real Life Video route.
struct RlvPoint
{
    double time = 0;        // s since the first track point
    double videoTime = 0;   // s in the video (time + RlvRoute::videoOffset())
    double lat = 0;
    double lon = 0;
    double distance = 0;    // cumulative metres (haversine)
    double rawElevation = 0;
    double elevation = 0;   // smoothed (distance window kElevationWindowM)
    double grade = 0;       // % from the smoothed elevation over kGradeWindowM, clamped
    double speed = 0;       // m/s, filmed speed = d(distance)/d(time), smoothed
    double gpsSpeed = -1;   // m/s from <gte:gps speed="km/h">, -1 = not recorded
    double ascent = 0;      // cumulative ascent (hysteresis) up to this point
    int heartRate = 0;
    int cadence = 0;
};

// Text overlay at a route distance (<informations> in the gpx-import xml).
struct RlvInformation
{
    double distance = 0;    // m
    QString text;           // en, else de (literal "\n" -> newline, entities decoded)
    QString textEn;
    QString textDe;
};

// A Real Life Video route: video + GPS track (+ optional gpx-import xml).
//
// Accepted inputs for load():
//  * a folder: its gpx-import .xml, else a .gpx with a video of the same
//    base name (else the only video in the folder)
//  * a gpx-import .xml (name, title, country, id, video-file-path,
//    gpx-file-path, framerate, start-frame, end-frame, informations)
//  * a .gpx with a same-basename video (.mp4/.m4v/.mov/.mkv/.avi)
//
// Video <-> track: track point i is shown at video second
//     videoTime_i = (start-frame - 1) / framerate + (t_i - t_0)
// i.e. the first track point is the start frame. The video ends at
// end-frame / framerate (without xml: last point + 1 s).
//
// Pure parsing / geometry, no Qt modules beyond Core (QXmlStreamReader).
// HTML named entities (&auml; ...) that plain XML does not know are mapped to
// characters before parsing.
class RlvRoute
{
public:
    static constexpr double kElevationWindowM = 40.0;   // smoothing window (full width)
    static constexpr double kGradeWindowM = 40.0;       // grade base (full width)
    // The grade the rider feels (gradeAheadAtDistance()): smoothed elevation
    // from kFeltBehindM behind to kFeltAheadM ahead, so a descent (or climb)
    // is felt when it starts instead of when the centred 40 m base has passed
    // the crest.
    static constexpr double kFeltBehindM = 10.0;
    static constexpr double kFeltAheadM = 20.0;
    static constexpr double kMinGrade = -20.0;
    static constexpr double kMaxGrade = 25.0;
    static constexpr double kAscentThresholdM = 2.0;    // hysteresis for ascent / descent
    static constexpr double kStopSpeedKmh = 0.5;        // gps speed below: standing (no distance)
    static constexpr double kInfoMinShowM = 150.0;      // overlay shown at least this far ...
    static constexpr double kInfoMinShowSec = 10.0;     // ... or for this many filmed seconds
    static constexpr double kMinDistanceM = 100.0;      // shorter tracks are not a route (isValid())

    RlvRoute() = default;

    bool load(const QString &path, QString *error = nullptr);
    static RlvRoute fromPath(const QString &path, QString *error = nullptr);
    // At least two points and kMinDistanceM of track.
    bool isValid() const { return m_points.size() >= 2 && totalDistance() >= kMinDistanceM; }

    // Builds the route from parsed track points (tests / programmatic use).
    // Points need time/lat/lon/rawElevation (+ optional gpsSpeed, hr, cad).
    bool setPoints(QVector<RlvPoint> points, QString *error = nullptr);

    // metadata
    QString name() const { return m_name; }
    QString title() const { return m_title.isEmpty() ? m_name : m_title; }
    QString country() const { return m_country; }
    QString id() const { return m_id; }
    QString folderPath() const { return m_folder; }
    QString sourcePath() const { return m_source; }   // file load() resolved (xml or gpx)
    QString gpxPath() const { return m_gpxPath; }
    QString videoPath() const { return m_videoPath; } // absolute, empty if none found
    bool hasVideo() const;
    double framerate() const { return m_framerate; }
    int startFrame() const { return m_startFrame; }
    int endFrame() const { return m_endFrame; }      // 0 = unknown
    double videoOffset() const;                      // (startFrame - 1) / framerate
    double videoDuration() const;                    // s
    double trackDuration() const;                    // s, last point time + 1

    const QVector<RlvPoint> &points() const { return m_points; }
    const QVector<RlvInformation> &informations() const { return m_infos; }
    double totalDistance() const { return m_points.isEmpty() ? 0.0 : m_points.constLast().distance; }
    double ascent() const { return m_ascent; }
    double descent() const { return m_descent; }
    double minElevation() const { return m_minEle; }
    double maxElevation() const { return m_maxEle; }

    // Lookups (clamped to the route). Distances in m, times in s.
    double distanceAtVideoTime(double videoSec) const;
    // Monotonic inverse: where the distance stands still (stops in the film)
    // the END of the stop is returned, so a moving rider skips stopped parts.
    double videoTimeAtDistance(double distanceM) const;
    double gradeAtDistance(double distanceM) const;
    // Grade over [d - kFeltBehindM, d + kFeltAheadM] of the smoothed
    // elevation (shrinks at the route ends), clamped like gradeAtDistance().
    double gradeAheadAtDistance(double distanceM) const;
    double elevationAtDistance(double distanceM) const;
    double recordedSpeedAtDistance(double distanceM) const;   // m/s, filmed speed
    double ascentAtDistance(double distanceM) const;           // cumulative ascent
    QPointF positionAtDistance(double distanceM) const;        // (lon, lat)
    // Elevation profile: n samples evenly spaced in distance, (d, smoothed ele).
    QVector<QPointF> profile(int samples = 500) const;
    // Overlay active at distance d: the last information with
    // info.distance <= d < info.distance + max(kInfoMinShowM, kInfoMinShowSec × filmed speed);
    // -1 if none.
    int informationIndexAt(double distanceM) const;
    QString informationAt(double distanceM) const;

    // helpers (exposed for tests)
    static double haversine(double lat1, double lon1, double lat2, double lon2);
    // HTML named entities -> characters (XML's five are kept), literal "\n" -> newline.
    static QString decodeText(const QString &text);
    // On the raw bytes (ASCII "&name;" -> "&#N;"), any 8-bit encoding is kept;
    // UTF-16/32 input (BOM or NUL bytes) is returned unchanged.
    static QByteArray replaceHtmlEntities(const QByteArray &xml);
    static bool isRouteXml(const QString &path);   // root element <gpx-import>
    static QString findVideoFor(const QString &gpxOrXmlPath);

private:
    bool loadXml(const QString &path, QString *error);
    bool loadGpx(const QString &path, QString *error);
    int segmentAt(double distanceM) const;   // last i with d_i <= d (0..n-2)
    double interpolate(double distanceM, double RlvPoint::*field) const;
    void computeDerived();
    static void fillMissingElevation(QVector<RlvPoint> &points, const QVector<bool> &hasElevation);

    QString m_name, m_title, m_country, m_id, m_folder, m_source, m_gpxPath, m_videoPath;
    double m_framerate = 30.0;
    int m_startFrame = 1;
    int m_endFrame = 0;
    QVector<RlvPoint> m_points;
    QVector<RlvInformation> m_infos;
    double m_ascent = 0, m_descent = 0, m_minEle = 0, m_maxEle = 0;
};

} // namespace watt
