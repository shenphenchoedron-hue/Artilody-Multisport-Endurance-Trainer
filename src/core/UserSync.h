#pragma once

#include <QHash>
#include <QJsonObject>
#include <QVector>

#include "Profile.h"
#include "SyncTask.h"

namespace watt {

class Database;
class ProfileManager;
class ServerConnection;

// The profiles kept with the server (docs/server-plan.md, step 3). Local
// first: a profile is always saved here; changed or deleted here it is
// marked to send (Database: dirty / profile_tombstones). Whenever the server
// answers (ServerConnection::answered) this station
//   1. sends its changes - pictures first (api/picture.php), then the
//      profiles (api/users.php POST); the newest change of each wins there,
//   2. fetches what changed on the server since its last look (api/users.php
//      ?since=<rev>, only when the ping's usersRev differs) and stores it -
//      new users, changed ones (with their pictures) and deleted ones (a
//      profile with rides here is kept).
// Another server (its id differs): everything is sent again. Nothing while a
// ride runs (the rider must stay as it is). Exposed to QML as "ProfileSync"
// (state / waiting / message: SyncTask).
class UserSync : public SyncTask
{
    Q_OBJECT

public:
    static constexpr qint64 kPictureMaxBytes = 4 * 1024 * 1024;   // (the server's PICTURE_MAX_BYTES)
    static constexpr int kTimeoutMs = 15000;

    // pictureDir: where the server's profile pictures are kept here
    UserSync(Database *db, ProfileManager *profiles, ServerConnection *connection, const QString &pictureDir,
             QObject *parent = nullptr);

protected:
    void round() override;
    void resendAll() override;
    int countWaiting() const override;
    void roundEnded() override;

private:
    void sendPictures(int index);
    void sendProfiles();
    void fetch();
    void fetchPictures(int index);
    QJsonObject profileJson(const Profile &p, const QString &pictureHash) const;
    // a user from the server stored here; true when something changed
    bool applyUser(const QJsonObject &u);
    void removeStarterProfiles();

    ProfileManager *m_profiles;
    QString m_pictureDir;
    bool m_profilesTouched = false;

    // the round under way
    QVector<Profile> m_toSend;
    QVector<QPair<QString, qint64>> m_tombstones;
    QHash<QString, QString> m_sendHash;               // uuid -> its picture's sha1 as sent ("" none)
    QVector<QPair<QString, QString>> m_needPictures;  // uuid, sha1 to fetch
    qint64 m_fetchedRev = -1;
};

} // namespace watt
