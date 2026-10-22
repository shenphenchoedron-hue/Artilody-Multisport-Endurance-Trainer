#pragma once

#include "Track.h"

#include <QAbstractListModel>

namespace watt {

class TrackListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, ArtistRole, DurationRole, DurationTextRole, PathRole, CurrentRole };

    explicit TrackListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QVector<Track> &tracks() const { return m_tracks; }
    void setTracks(const QVector<Track> &tracks);
    void updateTrack(int row, const Track &t);
    void setCurrentRow(int row);
    int currentRow() const { return m_current; }

signals:
    void countChanged();

private:
    QVector<Track> m_tracks;
    int m_current = -1;
};

QString formatTrackTime(qint64 ms);

} // namespace watt
