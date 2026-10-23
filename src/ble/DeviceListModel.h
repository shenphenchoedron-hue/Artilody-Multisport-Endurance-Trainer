#pragma once

#include <QAbstractListModel>

namespace watt {

struct DeviceEntry
{
    QString id;
    QString name;
    QString protocol;
    int rssi = 0;
    int state = 0;           // LinkState
    bool simulated = false;
    bool known = false;      // previously paired / used
    QString kind;            // "trainer", "heartRate" or "rower" (QML section key)
};

// Discovered devices of one category (smart trainers, heart-rate sensors or
// rowers). The "kind" role lets QML group/section mixed lists.
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, ProtocolRole, RssiRole, StateRole, SimulatedRole, KnownRole, KindRole };

    explicit DeviceListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const DeviceEntry &entry);   // keeps state of existing rows
    void setState(const QString &id, int state);
    void setRssi(const QString &id, int rssi);
    void removeDisconnected();
    void remove(const QString &id);
    bool contains(const QString &id) const;
    const DeviceEntry *find(const QString &id) const;

signals:
    void countChanged();

private:
    int indexOf(const QString &id) const;
    QVector<DeviceEntry> m_items;
};

} // namespace watt
