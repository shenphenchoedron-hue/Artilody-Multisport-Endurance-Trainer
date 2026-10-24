#include "AppBuilderBackend.h"

#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutBuilder.h"

namespace watt {

AppBuilderBackend::AppBuilderBackend(Database *db, ProfileManager *profiles, QObject *parent)
    : BuilderBackend(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    connect(m_profiles, &ProfileManager::profileChanged, this, &BuilderBackend::profileChanged);
}

int AppBuilderBackend::ftpFor(const QString &sport) const
{
    return m_profiles->current().ftpFor(sport);
}

ZoneSet AppBuilderBackend::zones() const
{
    return m_profiles->current().zones;
}

int AppBuilderBackend::preferredCadence() const
{
    return m_profiles->preferredCadence();
}

std::optional<Workout> AppBuilderBackend::workout(qint64 id) const
{
    return m_db->workout(id);
}

qint64 AppBuilderBackend::saveWorkout(const Workout &workout)
{
    return m_db->saveWorkout(workout);
}

QString AppBuilderBackend::lastError() const
{
    return m_db->lastError();
}

// Convenience constructor of the app (declared in WorkoutBuilder.h). Defined
// here so WorkoutBuilder.cpp does not depend on Database / ProfileManager.
WorkoutBuilder::WorkoutBuilder(Database *db, ProfileManager *profiles, QObject *parent)
    : WorkoutBuilder(new AppBuilderBackend(db, profiles), parent)
{
    m_backend->setParent(this);
}

} // namespace watt
