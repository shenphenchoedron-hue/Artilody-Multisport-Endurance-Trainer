#pragma once

// Scripted GATT link for driver tests (no Bluetooth hardware): a fake
// trainer with a chosen set of characteristics. Writes are recorded and
// answered by `responder` (notifications/indications emitted from the event
// loop, like a real stack); reads by `readValues`.
#include "GattLink.h"

#include <QBluetoothUuid>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <functional>

namespace watt::test {

class FakeGattLink : public GattLink
{
    Q_OBJECT
public:
    struct Write
    {
        QBluetoothUuid uuid;
        QByteArray data;
        bool withResponse = true;
    };
    using Notification = QPair<QBluetoothUuid, QByteArray>;

    explicit FakeGattLink(QList<QBluetoothUuid> characteristics, QList<QBluetoothUuid> services = {},
                          QObject *parent = nullptr)
        : GattLink(parent)
        , m_chars(characteristics)
        , m_services(services)
    {
    }

    // --- script ---
    std::function<QVector<Notification>(const QBluetoothUuid &, const QByteArray &)> responder;
    QHash<QBluetoothUuid, QByteArray> readValues;
    bool othersInspected = false;

    // --- GattLink ---
    void connectToDevice() override
    {
        m_state = LinkState::Connected;
        emit stateChanged(m_state);
        emit serviceReady();
    }
    void disconnectFromDevice() override
    {
        m_state = LinkState::Disconnected;
        emit stateChanged(m_state);
    }
    bool reconnectNow() override { return false; }
    void setAutoReconnect(bool) override {}
    LinkState state() const override { return m_state; }
    bool hasCharacteristic(const QBluetoothUuid &uuid) const override { return m_chars.contains(uuid); }
    QList<QBluetoothUuid> discoveredServices() const override { return m_services; }
    bool otherServicesInspected() const override { return othersInspected; }
    bool enableNotifications(const QBluetoothUuid &c, bool indicate = false) override
    {
        if (!m_chars.contains(c))
            return false;
        subscribed.insert(c);
        Q_UNUSED(indicate)
        QTimer::singleShot(0, this, [this, c] { emit notificationResult(c, true, true); });
        return true;
    }
    bool write(const QBluetoothUuid &c, const QByteArray &data, bool withResponse = true) override
    {
        if (m_state != LinkState::Connected || !m_chars.contains(c))
            return false;
        writes.append({ c, data, withResponse });
        if (responder) {
            const QVector<Notification> answers = responder(c, data);
            for (const Notification &n : answers)
                QTimer::singleShot(0, this, [this, n] { emit characteristicChanged(n.first, n.second); });
        }
        return true;
    }
    bool read(const QBluetoothUuid &c) override
    {
        if (!m_chars.contains(c))
            return false;
        reads.append(c);
        if (readValues.contains(c)) {
            const QByteArray v = readValues.value(c);
            QTimer::singleShot(0, this, [this, c, v] { emit characteristicRead(c, v); });
        }
        return true;
    }

    // --- test helpers ---
    void notify(const QBluetoothUuid &c, const QByteArray &v) { emit characteristicChanged(c, v); }
    void finishOtherServices()
    {
        othersInspected = true;
        emit otherServicesDiscovered();
    }
    QVector<QByteArray> writesTo(const QBluetoothUuid &c) const
    {
        QVector<QByteArray> out;
        for (const Write &w : writes)
            if (w.uuid == c)
                out << w.data;
        return out;
    }
    // writes to `c` whose first byte is `op`
    int countOp(const QBluetoothUuid &c, quint8 op) const
    {
        int n = 0;
        for (const Write &w : writes)
            if (w.uuid == c && !w.data.isEmpty() && quint8(w.data.at(0)) == op)
                ++n;
        return n;
    }

    QVector<Write> writes;
    QVector<QBluetoothUuid> reads;
    QSet<QBluetoothUuid> subscribed;

private:
    QList<QBluetoothUuid> m_chars;
    QList<QBluetoothUuid> m_services;
    LinkState m_state = LinkState::Disconnected;
};

} // namespace watt::test
