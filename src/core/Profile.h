#pragma once

#include "Zones.h"

#include <QString>

namespace watt {

struct Profile
{
    // Valid FTP range (also used to judge FTP test estimates).
    static constexpr int kMinFtp = 50;
    static constexpr int kMaxFtp = 600;

    qint64 id = -1;
    QString name = QStringLiteral("Rider");
    double weightKg = 75.0;
    // Optional (0 = not set): with the weight it sizes the rider's air drag
    // (road::autoCda()) for virtual gears.
    double heightCm = 0.0;
    static constexpr double kMinHeightCm = 100.0;
    static constexpr double kMaxHeightCm = 230.0;
    int ftp = 250;
    int rowingFtp = 150;          // rowing FTP (W); rowing workouts are % of this
    // Running: the current AnT speed (km/h) - not a profiles column, the
    // newest entry of ant_tests (Database fills it in; = running::kDefaultAntKmh
    // until there is one). Running workouts are % of this.
    double antSpeedKmh = 12.0;
    int maxHr = 185;
    int restHr = 60;
    int preferredCadence = 85;
    bool defaultErg = true;       // default ride mode: ERG (true) or Free Ride (false)
    QString avatarPath;           // local image file, optional
    ZoneSet zones;

    double wattsPerKg() const { return weightKg > 0 ? ftp / weightKg : 0.0; }
    // FTP to use for workouts of the given sport ("cycling"/"rowing"); for
    // "running" the AnT speed in 0.1 km/h (running::speedUnits()).
    int ftpFor(const QString &sport) const;
    bool isValid(QString *error = nullptr) const;
};

} // namespace watt
