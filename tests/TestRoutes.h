// Synthetic RLV routes for unit tests: a gpx track (+ dummy video, optional
// gpx-import xml) in a folder.
#pragma once

#include "RlvRoute.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <functional>

namespace watt::testroutes {

// Straight route north, 1 point per second at `speed` m/s; grade(d) in %.
// With xml: <name>name</name><title>title</title> (title empty: no xml).
inline bool writeRoute(const QString &dir, const QString &name, double lengthM, double speed,
                       const std::function<double(double)> &grade, const QString &title = QString())
{
    const double mPerDegLat = RlvRoute::haversine(0, 0, 1, 0);
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><trkseg>\n";
    const QDateTime t0 = QDateTime::fromString(QStringLiteral("1999-12-31T23:00:00Z"), Qt::ISODate);
    double ele = 300, d = 0;
    for (int i = 0; d <= lengthM + 1e-6; ++i, d += speed) {
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"6.0\"><ele>%2</ele><time>%3</time></trkpt>\n")
                   .arg(45.0 + d / mPerDegLat, 0, 'f', 8)
                   .arg(ele, 0, 'f', 3)
                   .arg(t0.addSecs(i).toString(Qt::ISODate))
                   .toUtf8();
        ele += grade(d + speed / 2) / 100.0 * speed;
    }
    gpx += "</trkseg></trk></gpx>\n";
    QFile g(dir + QLatin1Char('/') + name + QStringLiteral(".gpx"));
    QFile v(dir + QLatin1Char('/') + name + QStringLiteral(".mp4"));
    if (!g.open(QIODevice::WriteOnly) || !v.open(QIODevice::WriteOnly))
        return false;
    g.write(gpx);
    v.write("not a real video");
    if (!title.isEmpty()) {
        QFile x(dir + QLatin1Char('/') + name + QStringLiteral(".xml"));
        if (!x.open(QIODevice::WriteOnly))
            return false;
        x.write("<gpx-import><name>" + name.toUtf8() + "</name><title>" + title.toUtf8()
                + "</title><country>DK</country><video-file-path>" + name.toUtf8()
                + ".mp4</video-file-path><gpx-file-path>" + name.toUtf8()
                + ".gpx</gpx-file-path><framerate>30</framerate><start-frame>1</start-frame></gpx-import>");
    }
    return true;
}

} // namespace watt::testroutes
