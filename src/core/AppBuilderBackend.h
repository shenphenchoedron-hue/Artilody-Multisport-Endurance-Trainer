#pragma once

#include "BuilderBackend.h"

namespace watt {

class Database;
class ProfileManager;

// BuilderBackend of the Watts Trainer app: workouts live in the SQLite
// database, FTP / zones / cadence come from the current profile.
class AppBuilderBackend : public BuilderBackend
{
    Q_OBJECT
public:
    AppBuilderBackend(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    int ftpFor(const QString &sport) const override;
    ZoneSet zones() const override;
    int preferredCadence() const override;
    std::optional<Workout> workout(qint64 id) const override;
    qint64 saveWorkout(const Workout &workout) override;
    QString lastError() const override;

private:
    Database *m_db;
    ProfileManager *m_profiles;
};

} // namespace watt
