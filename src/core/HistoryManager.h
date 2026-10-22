#pragma once

#include "Ride.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace watt {

class Database;
class ProfileManager;

// Training history, statistics and CSV export. Exposed to QML as "History".
class HistoryManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int rangeDays READ rangeDays WRITE setRangeDays NOTIFY dataChanged)
    Q_PROPERTY(QVariantList rangeOptions READ rangeOptions CONSTANT)
    Q_PROPERTY(int totalRides READ totalRides NOTIFY dataChanged)
    Q_PROPERTY(int totalSeconds READ totalSeconds NOTIFY dataChanged)
    Q_PROPERTY(QString totalTimeText READ totalTimeText NOTIFY dataChanged)
    Q_PROPERTY(int avgPower READ avgPower NOTIFY dataChanged)
    Q_PROPERTY(int totalKj READ totalKj NOTIFY dataChanged)
    Q_PROPERTY(int totalTss READ totalTss NOTIFY dataChanged)
    Q_PROPERTY(QVariantList rides READ rides NOTIFY dataChanged)
    Q_PROPERTY(QVariantList weeklyHours READ weeklyHours NOTIFY dataChanged)
    Q_PROPERTY(QVariantList zoneDistribution READ zoneDistribution NOTIFY dataChanged)
    Q_PROPERTY(qint64 selectedRideId READ selectedRideId WRITE selectRide NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedRide READ selectedRide NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedPower READ selectedPower NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedTarget READ selectedTarget NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedHeartRate READ selectedHeartRate NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedZones READ selectedZones NOTIFY selectionChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)

public:
    HistoryManager(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    int rangeDays() const { return m_rangeDays; }
    void setRangeDays(int days);
    QVariantList rangeOptions() const;
    int totalRides() const { return m_rides.size(); }
    int totalSeconds() const;
    QString totalTimeText() const;
    int avgPower() const;
    int totalKj() const;
    int totalTss() const;
    QVariantList rides() const;
    QVariantList weeklyHours() const;
    QVariantList zoneDistribution() const;
    qint64 selectedRideId() const { return m_selected.id; }
    QVariantMap selectedRide() const;
    QVariantList selectedPower() const { return m_selPower; }
    QVariantList selectedTarget() const { return m_selTarget; }
    QVariantList selectedHeartRate() const { return m_selHr; }
    QVariantList selectedZones() const;
    QString lastMessage() const { return m_lastMessage; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectRide(qint64 id);
    Q_INVOKABLE bool deleteRide(qint64 id);
    Q_INVOKABLE bool exportSelectedCsv(const QUrl &file);
    Q_INVOKABLE bool exportSummaryCsv(const QUrl &file);
    Q_INVOKABLE QUrl suggestedExportUrl(bool summary) const;

signals:
    void dataChanged();
    void selectionChanged();
    void lastMessageChanged();

private:
    void setMessage(const QString &m);
    static QVariantList zonesToVariant(const std::array<int, kZoneCount> &zones);

    Database *m_db;
    ProfileManager *m_profiles;
    int m_rangeDays = 30;
    QVector<RideRecord> m_rides;
    RideRecord m_selected;
    QVector<Sample> m_selSamples;
    QVariantList m_selPower, m_selTarget, m_selHr;
    bool m_selIsFtpTest = false;
    int m_selBest1min = 0;
    int m_selEstimatedFtp = 0;   // 0 when not plausible
    QString m_lastMessage;
};

} // namespace watt
