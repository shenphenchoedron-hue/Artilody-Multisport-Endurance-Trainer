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
    double weightKg = 70.0;
    int ftp = 250;
    int maxHr = 185;
    int restHr = 60;
    int preferredCadence = 85;
    bool defaultErg = true;       // default ride mode: ERG (true) or Free Ride (false)
    QString avatarPath;           // local image file, optional
    ZoneSet zones;

    double wattsPerKg() const { return weightKg > 0 ? ftp / weightKg : 0.0; }
    bool isValid(QString *error = nullptr) const;
};

} // namespace watt
