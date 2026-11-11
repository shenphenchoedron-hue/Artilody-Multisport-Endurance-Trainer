#pragma once

#include "Profile.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace watt {

class Database;

// Current user profile + personal training zones. Exposed to QML as "Profiles".
class ProfileManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 profileId READ profileId NOTIFY profileChanged)
    Q_PROPERTY(QString name READ name NOTIFY profileChanged)
    Q_PROPERTY(QString initials READ initials NOTIFY profileChanged)
    Q_PROPERTY(double weight READ weight NOTIFY profileChanged)
    Q_PROPERTY(double heightCm READ heightCm NOTIFY profileChanged)   // 0 = not set
    Q_PROPERTY(int ftp READ ftp NOTIFY profileChanged)
    Q_PROPERTY(int rowingFtp READ rowingFtp WRITE setRowingFtp NOTIFY profileChanged)
    Q_PROPERTY(int maxHr READ maxHr NOTIFY profileChanged)
    Q_PROPERTY(int restHr READ restHr NOTIFY profileChanged)
    Q_PROPERTY(int preferredCadence READ preferredCadence NOTIFY profileChanged)
    Q_PROPERTY(bool defaultErg READ defaultErg NOTIFY profileChanged)
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl NOTIFY profileChanged)
    // The rider in the GPX Ride's 3D view: { model: "male" | "female",
    // jersey, shorts, helmet, shoes, hair, bike (the frame), boat (the hull): "#rrggbb" or "" (the model's own) } -
    // per profile (setting "avatar/<id>"), saved with saveProfile({ avatar3d: {...} }).
    Q_PROPERTY(QVariantMap avatar3d READ avatar3d NOTIFY profileChanged)
    // The runner in the Treadmill Run's 3D view: the same keys (setting
    // "runnerAvatar/<id>"), saved with saveProfile({ runner3d: {...} }).
    Q_PROPERTY(QVariantMap runner3d READ runner3d NOTIFY profileChanged)
    // The rower in the Rowing page's 3D view (setting "rowerAvatar/<id>",
    // saveProfile({ rower3d: {...} })).
    Q_PROPERTY(QVariantMap rower3d READ rower3d NOTIFY profileChanged)
    Q_PROPERTY(double wattsPerKg READ wattsPerKg NOTIFY profileChanged)
    Q_PROPERTY(QVariantList powerZones READ powerZones NOTIFY profileChanged)
    Q_PROPERTY(QVariantList zoneBounds READ zoneBounds NOTIFY profileChanged)            // fractions of FTP
    Q_PROPERTY(QVariantList defaultZoneBounds READ defaultZoneBounds CONSTANT)          // percent
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ProfileManager(Database *db, QObject *parent = nullptr);

    void load();
    // The AnT speed (ant_tests) changed: re-reads it into current() and emits
    // profileChanged (running workouts are % of it).
    void refreshAnt();
    const Profile &current() const { return m_profile; }
    // While it returns true (a ride / row is running or paused, main.cpp)
    // deleteProfile() is refused: the ride's rider must still exist when it
    // is saved.
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }

    qint64 profileId() const { return m_profile.id; }
    QString name() const { return m_profile.name; }
    QString initials() const;
    double weight() const { return m_profile.weightKg; }
    double heightCm() const { return m_profile.heightCm; }
    int ftp() const { return m_profile.ftp; }
    int rowingFtp() const { return m_profile.rowingFtp; }
    // Validates (Profile::kMinFtp..kMaxFtp) and persists the rowing FTP of the current profile.
    Q_INVOKABLE bool setRowingFtp(int watts);
    // FTP for workouts of a sport: rowingFtp for "rowing", ftp otherwise.
    Q_INVOKABLE int ftpForSport(const QString &sport) const { return m_profile.ftpFor(sport); }
    int maxHr() const { return m_profile.maxHr; }
    int restHr() const { return m_profile.restHr; }
    int preferredCadence() const { return m_profile.preferredCadence; }
    bool defaultErg() const { return m_profile.defaultErg; }
    QUrl avatarUrl() const;
    QVariantMap avatar3d() const { return m_avatar3d; }
    QVariantMap runner3d() const { return m_runner3d; }
    QVariantMap rower3d() const { return m_rower3d; }
    double wattsPerKg() const { return m_profile.wattsPerKg(); }
    QVariantList powerZones() const;
    QVariantList zoneBounds() const;
    QVariantList defaultZoneBounds() const;
    QVariantList profiles() const;
    QString lastError() const { return m_lastError; }

    // Saves the edited profile. Keys: name, weight, heightCm, ftp, maxHr, restHr,
    // preferredCadence, defaultErg, avatarUrl, rowingFtp, zoneBounds (list of 5 fractions),
    // avatar3d, runner3d, rower3d (maps, see the properties).
    Q_INVOKABLE bool saveProfile(const QVariantMap &values);
    Q_INVOKABLE bool selectProfile(qint64 id);
    Q_INVOKABLE qint64 createProfile(const QString &name);
    // Validates the values first and only then inserts + selects the profile.
    Q_INVOKABLE qint64 createProfileWith(const QVariantMap &values);
    Q_INVOKABLE bool deleteProfile(qint64 id);

    // Display values for an FTP change of the current profile (FTP test result):
    // estimatedFtp, previousFtp, diff, diffText ("+12 W"), percentText ("+4.8%"),
    // wattsPerKg, wattsPerKgText ("3.85 W/kg").
    Q_INVOKABLE QVariantMap ftpComparison(int estimatedFtp, int previousFtp) const;

    Q_INVOKABLE int zoneForWatts(int watts) const;
    Q_INVOKABLE int heartRateZoneFor(int bpm) const;
    Q_INVOKABLE QString heartRateZoneRange(int zone) const;   // "135 – 155"
    // Heart-rate zones for arbitrary (e.g. unsaved) max/rest values.
    Q_INVOKABLE QVariantList heartRateZonesFor(int maxHr, int restHr) const;

signals:
    void profileChanged();
    void profilesChanged();
    void lastErrorChanged();

private:
    void setError(const QString &e);
    static void applyValues(Profile &p, const QVariantMap &values);
    // the profile's 3D avatar: read from / written to the settings
    void loadAvatar3d();
    void saveAvatars(qint64 id, const QVariantMap &values);   // avatar3d / runner3d / rower3d in values
    Database *m_db;
    Profile m_profile;
    QVariantMap m_avatar3d, m_runner3d, m_rower3d;
    QString m_lastError;
    std::function<bool()> m_busy;
};

} // namespace watt
