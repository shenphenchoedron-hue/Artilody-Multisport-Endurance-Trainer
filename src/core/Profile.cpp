#include "Profile.h"

#include "Workout.h"

namespace watt {

int Profile::ftpFor(const QString &sport) const
{
    return ftpForSport(sport, ftp, rowingFtp);
}

bool Profile::isValid(QString *error) const
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (name.trimmed().isEmpty())
        return fail(QStringLiteral("Name must not be empty"));
    if (weightKg < 20 || weightKg > 250)
        return fail(QStringLiteral("Weight must be between 20 and 250 kg"));
    if (ftp < kMinFtp || ftp > kMaxFtp)
        return fail(QStringLiteral("FTP must be between %1 and %2 W").arg(kMinFtp).arg(kMaxFtp));
    if (rowingFtp < kMinFtp || rowingFtp > kMaxFtp)
        return fail(QStringLiteral("Rowing FTP must be between %1 and %2 W").arg(kMinFtp).arg(kMaxFtp));
    if (restHr < 30 || restHr > 120)
        return fail(QStringLiteral("Resting heart rate must be between 30 and 120 BPM"));
    if (maxHr <= restHr + 20 || maxHr > 240)
        return fail(QStringLiteral("Max heart rate must be above resting heart rate"));
    if (preferredCadence < 40 || preferredCadence > 150)
        return fail(QStringLiteral("Cadence must be between 40 and 150 RPM"));
    if (!zones.isValid())
        return fail(QStringLiteral("Power zones must be increasing"));
    return true;
}

} // namespace watt
