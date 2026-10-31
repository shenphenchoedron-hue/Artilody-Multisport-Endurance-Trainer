#pragma once

#include "RunWorkout.h"

#include <QDate>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

namespace watt {

class Database;
class ProfileManager;

// Treadmill running: the runner's anaerobic threshold (AnT) and the running
// workouts. Exposed to QML as "RunLibrary" (Artilody.Backend).
//
// AnT: the newest entry of ant_tests for the current profile (a test result
// or a value set by hand); kDefaultAntKmh until there is one. The test is
// "due" testIntervalWeeks after the last test (setting run/antTestWeeks).
//
// Workouts: the workouts of sport "running" (table workouts: built-in ones
// seeded by the Database, own ones made in the Workout Builder). They are
// edited, duplicated and deleted like every workout (Builder / Workouts);
// reloadWorkouts() picks up changes.
class RunLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double antSpeedKmh READ antSpeedKmh NOTIFY antChanged)
    Q_PROPERTY(int antHeartRate READ antHeartRate NOTIFY antChanged)
    Q_PROPERTY(QString antPace READ antPace NOTIFY antChanged)          // "4:17" min/km
    Q_PROPERTY(bool antTested READ antTested NOTIFY antChanged)          // a value exists (test or by hand)
    Q_PROPERTY(QDateTime lastTestAt READ lastTestAt NOTIFY antChanged)
    Q_PROPERTY(int daysSinceTest READ daysSinceTest NOTIFY antChanged)   // -1 = never
    Q_PROPERTY(int testIntervalWeeks READ testIntervalWeeks WRITE setTestIntervalWeeks NOTIFY antChanged)
    // Per profile: the speed (km/h) where the athlete goes from walking to
    // running. The 3D runner walks below it and runs above it (cross-faded
    // over +-0.75 km/h). Setting run/walkRunKmh/<profile id>.
    Q_PROPERTY(double walkRunKmh READ walkRunKmh WRITE setWalkRunKmh NOTIFY walkRunChanged)
    Q_PROPERTY(bool testDue READ testDue NOTIFY antChanged)
    Q_PROPERTY(QDate nextTestDate READ nextTestDate NOTIFY antChanged)
    Q_PROPERTY(QString testStatusText READ testStatusText NOTIFY antChanged)
    Q_PROPERTY(QVariantList antHistory READ antHistory NOTIFY antChanged)
    Q_PROPERTY(QVariantList zones READ zones NOTIFY antChanged)
    Q_PROPERTY(QVariantList workouts READ workouts NOTIFY workoutsChanged)
    // the built-in AnT test (-1 when missing)
    Q_PROPERTY(qint64 antTestWorkoutId READ antTestWorkoutId NOTIFY workoutsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    RunLibrary(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    double antSpeedKmh() const { return m_ant.speedKmh; }
    int antHeartRate() const { return m_ant.heartRate; }
    QString antPace() const;
    bool antTested() const { return m_hasAnt; }
    QDateTime lastTestAt() const { return m_lastTest ? m_lastTest->testedAt : QDateTime(); }
    int daysSinceTest() const;
    int testIntervalWeeks() const { return m_weeks; }
    static constexpr double kDefaultWalkRunKmh = 6.25;
    static constexpr double kMinWalkRunKmh = 4.0;
    static constexpr double kMaxWalkRunKmh = 10.0;
    double walkRunKmh() const { return m_walkRun; }
    void setWalkRunKmh(double kmh);
    void setTestIntervalWeeks(int weeks);
    bool testDue() const;
    QDate nextTestDate() const;
    QString testStatusText() const;
    QVariantList antHistory() const;
    QVariantList zones() const;
    QVariantList workouts() const;
    qint64 antTestWorkoutId() const;
    QString lastError() const { return m_lastError; }

    // C++: one workout (built-in or own)
    std::optional<RunWorkout> workout(qint64 id) const;
    // For "today" (tests)
    void setTodayForTesting(const QDate &d) { m_today = d; emit antChanged(); }

    Q_INVOKABLE QVariantMap get(qint64 id) const;
    // Speed (km/h) for a fraction of the current AnT.
    Q_INVOKABLE double speedFor(double fractionOfAnt) const { return fractionOfAnt * m_ant.speedKmh; }
    Q_INVOKABLE QString paceFor(double kmh) const;
    Q_INVOKABLE int zoneForSpeed(double kmh) const;
    // Sets the AnT by hand (a new history entry). false + lastError when out of range.
    Q_INVOKABLE bool setManualAnt(double speedKmh, int heartRate);
    // A finished AnT test (RunSession): the new current AnT.
    Q_INVOKABLE qint64 saveTestResult(double speedKmh, int heartRate, double inclinePercent, qint64 rideId);
    Q_INVOKABLE bool deleteAntTest(qint64 id);

public slots:
    void reload();
    void reloadWorkouts();   // the workouts table changed (Builder, Workouts page)

signals:
    void antChanged();
    void walkRunChanged();
    void workoutsChanged();
    void lastErrorChanged();

private:
    void setError(const QString &e);
    void antStored();        // after an ant_tests change
    QDate today() const { return m_today.isValid() ? m_today : QDate::currentDate(); }

    Database *m_db;
    ProfileManager *m_profiles;
    AntTest m_ant;
    bool m_hasAnt = false;
    std::optional<AntTest> m_lastTest;   // newest entry made by a test run (rideId >= 0 or note "test")
    QVector<AntTest> m_history;
    QVector<RunWorkout> m_workouts;   // sport "running", built-in first
    int m_weeks = 6;
    double m_walkRun = kDefaultWalkRunKmh;
    QDate m_today;
    QString m_lastError;
};

} // namespace watt
