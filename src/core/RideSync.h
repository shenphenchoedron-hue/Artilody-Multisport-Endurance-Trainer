#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include "SyncTask.h"

namespace watt {

class Database;
class ProfileManager;
class ServerConnection;

// The rides kept with the server (docs/server-plan.md, step 5). Local first:
// a ride is always saved here. Whenever the server answers
// (ServerConnection::answered) this station
//   1. sends what it has not sent: its deletions (ride_tombstones) and its
//      rides (oldest first, in batches; samples, laps and the ghost trace with
//      them) - the first time all of its history,
//   2. fetches what changed since its last look (api/rides.php ?since=<rev>,
//      only when the ping's ridesRev differs): rides recorded on other
//      stations are downloaded whole into History (and their ghost traces:
//      the ghost can race them); rides deleted elsewhere are deleted here.
// A ride of a profile this station does not know (yet) is skipped; when a new
// profile arrives (UserSync) the list is looked through again from the start.
// Another server (its id differs): everything is sent again. Nothing while a
// ride runs - neither sent nor fetched (building and storing rides would cost
// the picture frames). Exposed to QML as "RideSyncStatus" (state / waiting /
// message: SyncTask).
class RideSync : public SyncTask
{
    Q_OBJECT

public:
    static constexpr int kBatchRides = 20;                    // rides per POST, at most
    static constexpr int kBatchBytes = 6 * 1024 * 1024;       // JSON per POST, about (the server takes 15 MB)
    static constexpr int kTimeoutMs = 60000;

    RideSync(Database *db, ProfileManager *profiles, ServerConnection *connection, QObject *parent = nullptr);

    // A ride was saved or deleted here: sent shortly (when the server is there).
    void rideSaved();

    // A ride as sent to the server, compact JSON ("" when it cannot be sent).
    QByteArray rideJson(qint64 rideId) const;

signals:
    void ridesChanged();   // rides arrived from / were deleted by the server: History again

protected:
    void round() override;
    void resendAll() override;
    int countWaiting() const override;
    void roundEnded() override;

private:
    void sendTombstones();
    void sendRides();
    void fetch();
    void download(int index);
    // the server's answer to a POST of rides (deletions: those sent)
    bool applyResults(const QJsonObject &o, bool deletions);
    void checkNewProfiles();

    QPointer<ProfileManager> m_profiles;
    bool m_ridesTouched = false;
    QSet<QString> m_profileUuids;   // known, to see a new one arrive

    // the round under way
    QStringList m_sent;              // uuids in the POST under way
    QStringList m_toDownload;
    qint64 m_listRev = -1;
    bool m_more = false;
};

} // namespace watt
