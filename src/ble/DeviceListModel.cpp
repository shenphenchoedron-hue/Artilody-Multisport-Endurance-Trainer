#include "DeviceListModel.h"

namespace watt {

DeviceListModel::DeviceListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const DeviceEntry &e = m_items.at(index.row());
    switch (role) {
    case IdRole: return e.id;
    case NameRole: return e.name;
    case ProtocolRole: return e.protocol;
    case RssiRole: return e.rssi;
    case StateRole: return e.state;
    case SimulatedRole: return e.simulated;
    case KnownRole: return e.known;
    case KindRole: return e.kind;
    default: return {};
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return { { IdRole, "deviceId" }, { NameRole, "name" }, { ProtocolRole, "protocol" }, { RssiRole, "rssi" },
             { StateRole, "linkState" }, { SimulatedRole, "simulated" }, { KnownRole, "known" },
             { KindRole, "kind" } };
}

int DeviceListModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

bool DeviceListModel::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

const DeviceEntry *DeviceListModel::find(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? &m_items.at(i) : nullptr;
}

void DeviceListModel::upsert(const DeviceEntry &entry)
{
    const int i = indexOf(entry.id);
    if (i < 0) {
        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(entry);
        endInsertRows();
        emit countChanged();
        return;
    }
    DeviceEntry &e = m_items[i];
    const int state = e.state;
    e = entry;
    e.state = state;
    emit dataChanged(index(i), index(i));
}

void DeviceListModel::setState(const QString &id, int state)
{
    const int i = indexOf(id);
    if (i < 0 || m_items[i].state == state)
        return;
    m_items[i].state = state;
    emit dataChanged(index(i), index(i), { StateRole });
}

void DeviceListModel::setRssi(const QString &id, int rssi)
{
    const int i = indexOf(id);
    if (i < 0 || m_items[i].rssi == rssi)
        return;
    m_items[i].rssi = rssi;
    emit dataChanged(index(i), index(i), { RssiRole });
}

void DeviceListModel::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    beginRemoveRows(QModelIndex(), i, i);
    m_items.removeAt(i);
    endRemoveRows();
    emit countChanged();
}

void DeviceListModel::removeDisconnected()
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).state == 0 && !m_items.at(i).simulated) {
            beginRemoveRows(QModelIndex(), i, i);
            m_items.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

} // namespace watt
