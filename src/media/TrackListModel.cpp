#include "TrackListModel.h"

namespace watt {

QString formatTrackTime(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("–:––");
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

TrackListModel::TrackListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size())
        return {};
    const Track &t = m_tracks.at(index.row());
    switch (role) {
    case IdRole: return t.id;
    case TitleRole: return t.title;
    case ArtistRole: return t.artist;
    case DurationRole: return t.durationMs;
    case DurationTextRole: return formatTrackTime(t.durationMs);
    case PathRole: return t.path;
    case CurrentRole: return index.row() == m_current;
    default: return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return { { IdRole, "trackId" }, { TitleRole, "title" }, { ArtistRole, "artist" }, { DurationRole, "duration" },
             { DurationTextRole, "durationText" }, { PathRole, "path" }, { CurrentRole, "isCurrent" } };
}

void TrackListModel::setTracks(const QVector<Track> &tracks)
{
    beginResetModel();
    m_tracks = tracks;
    if (m_current >= m_tracks.size())
        m_current = -1;
    endResetModel();
    emit countChanged();
}

void TrackListModel::updateTrack(int row, const Track &t)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    m_tracks[row] = t;
    emit dataChanged(index(row), index(row));
}

void TrackListModel::setCurrentRow(int row)
{
    if (row == m_current)
        return;
    const int old = m_current;
    m_current = row;
    if (old >= 0 && old < m_tracks.size())
        emit dataChanged(index(old), index(old), { CurrentRole });
    if (row >= 0 && row < m_tracks.size())
        emit dataChanged(index(row), index(row), { CurrentRole });
}

} // namespace watt
