#include "MusicPlayer.h"

#include "Database.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QRandomGenerator>
#include <QVariantMap>

namespace watt {

namespace {
const QString kFavorites = QStringLiteral("Favorites");
}

MusicPlayer::MusicPlayer(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
    m_player.setAudioOutput(&m_audio);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &MusicPlayer::playingChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &MusicPlayer::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &MusicPlayer::durationChanged);
    connect(&m_player, &QMediaPlayer::metaDataChanged, this, &MusicPlayer::onMetaData);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, &MusicPlayer::onStatus);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
        setError(msg);
    });
    connect(&m_audio, &QAudioOutput::volumeChanged, this, &MusicPlayer::volumeChanged);
    m_volumeSave.setSingleShot(true);
    m_volumeSave.setInterval(600);
    connect(&m_volumeSave, &QTimer::timeout, this, [this] {
        m_db->setSetting(QStringLiteral("music/volume"), double(m_audio.volume()));
    });
    connect(&m_audio, &QAudioOutput::mutedChanged, this, &MusicPlayer::volumeChanged);
}

MusicPlayer::~MusicPlayer()
{
    if (m_volumeSave.isActive())
        m_db->setSetting(QStringLiteral("music/volume"), double(m_audio.volume()));
}

QStringList MusicPlayer::supportedExtensions()
{
    return { QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("flac"),
             QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("wma") };
}

void MusicPlayer::initialize(const QStringList &defaultMusicFolders)
{
    m_audio.setVolume(m_db->setting(QStringLiteral("music/volume"), 0.7).toDouble());
    m_shuffle = m_db->setting(QStringLiteral("music/shuffle"), false).toBool();
    m_repeat = m_db->setting(QStringLiteral("music/repeat"), true).toBool();

    QVector<Playlist> lists = m_db->playlists();
    if (lists.isEmpty()) {
        const qint64 id = m_db->createPlaylist(QStringLiteral("Workout Mix"));
        QStringList files;
        for (const QString &folder : defaultMusicFolders) {
            QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && files.size() < 200) {
                const QString f = it.next();
                if (supportedExtensions().contains(QFileInfo(f).suffix().toLower()))
                    files << f;
            }
            if (!files.isEmpty())
                break;   // first folder with music wins
        }
        if (id >= 0)
            addPaths(files, id);
        lists = m_db->playlists();
    }
    if (lists.isEmpty()) {
        // the database refused to create a playlist: no music, but no crash
        loadPlaylist(-1);
        return;
    }
    const qint64 wanted = m_db->setting(QStringLiteral("music/playlist"), lists.constFirst().id).toLongLong();
    bool exists = false;
    for (const Playlist &p : lists)
        exists = exists || p.id == wanted;
    loadPlaylist(exists ? wanted : lists.constFirst().id);
}

const Track *MusicPlayer::currentTrack() const
{
    const auto &tracks = m_tracks.tracks();
    return m_index >= 0 && m_index < tracks.size() ? &tracks.at(m_index) : nullptr;
}

// --- state -----------------------------------------------------------------

QString MusicPlayer::title() const
{
    const Track *t = currentTrack();
    if (!t)
        return QStringLiteral("No music");
    if (!m_metaTitle.isEmpty())
        return m_metaTitle;
    return t->title;
}

QString MusicPlayer::artist() const
{
    const Track *t = currentTrack();
    if (!t)
        return QStringLiteral("Add music to a playlist");
    if (!m_metaArtist.isEmpty())
        return m_metaArtist;
    const QString a = t->artist;
    return a.isEmpty() ? QStringLiteral("Unknown artist") : a;
}

QString MusicPlayer::coverSource() const
{
    return m_cover.isNull() ? QString() : QStringLiteral("image://cover/%1").arg(m_coverVersion);
}

bool MusicPlayer::playing() const
{
    return m_player.playbackState() == QMediaPlayer::PlayingState;
}

qint64 MusicPlayer::duration() const
{
    if (m_player.duration() > 0)
        return m_player.duration();
    const Track *t = currentTrack();
    return t ? t->durationMs : 0;
}

QString MusicPlayer::positionText() const
{
    const qint64 s = qMax<qint64>(0, position()) / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
QString MusicPlayer::durationText() const { return formatTrackTime(duration()); }

void MusicPlayer::setVolume(double v)
{
    v = qBound(0.0, v, 1.0);
    m_audio.setVolume(float(v));
    m_volumeSave.start();   // persist once the user stops dragging
}

void MusicPlayer::setMuted(bool m)
{
    m_audio.setMuted(m);
}

void MusicPlayer::setShuffle(bool s)
{
    if (m_shuffle == s)
        return;
    m_shuffle = s;
    m_db->setSetting(QStringLiteral("music/shuffle"), s);
    emit modeChanged();
    emit currentChanged();
}

void MusicPlayer::setRepeat(bool r)
{
    if (m_repeat == r)
        return;
    m_repeat = r;
    m_db->setSetting(QStringLiteral("music/repeat"), r);
    emit modeChanged();
}

bool MusicPlayer::favorite() const
{
    const Track *cur = currentTrack();
    if (!cur)
        return false;
    const qint64 fav = const_cast<MusicPlayer *>(this)->favoritesPlaylistId(false);
    if (fav < 0)
        return false;
    const QString path = cur->path;
    for (const Track &t : m_db->playlistTracks(fav)) {
        if (t.path == path)
            return true;
    }
    return false;
}

QVariantList MusicPlayer::upNext() const
{
    QVariantList list;
    const auto &tracks = m_tracks.tracks();
    if (tracks.isEmpty())
        return list;
    for (int i = 1; i <= 3 && i < tracks.size(); ++i) {
        const int idx = (qMax(0, m_index) + i) % tracks.size();
        if (!m_repeat && qMax(0, m_index) + i >= tracks.size())
            break;
        const Track &t = tracks.at(idx);
        QVariantMap m;
        m.insert(QStringLiteral("index"), idx);
        m.insert(QStringLiteral("title"), t.title);
        m.insert(QStringLiteral("artist"), t.artist.isEmpty() ? QStringLiteral("Unknown artist") : t.artist);
        m.insert(QStringLiteral("durationText"), formatTrackTime(t.durationMs));
        list.append(m);
    }
    return list;
}

QVariantList MusicPlayer::playlists() const
{
    QVariantList list;
    for (const Playlist &p : m_db->playlists()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), p.id);
        m.insert(QStringLiteral("name"), p.name);
        m.insert(QStringLiteral("count"), p.trackCount);
        list.append(m);
    }
    return list;
}

QString MusicPlayer::currentPlaylistName() const
{
    for (const Playlist &p : m_db->playlists()) {
        if (p.id == m_playlistId)
            return p.name;
    }
    return QString();
}

void MusicPlayer::setError(const QString &e)
{
    if (m_error == e)
        return;
    m_error = e;
    emit errorTextChanged();
}

// --- transport -------------------------------------------------------------

void MusicPlayer::play()
{
    if (m_index < 0) {
        if (m_tracks.rowCount() == 0)
            return;
        setIndex(0, true);
        return;
    }
    m_player.play();
}

void MusicPlayer::pause()
{
    m_player.pause();
}

void MusicPlayer::togglePlay()
{
    playing() ? pause() : play();
}

void MusicPlayer::next()
{
    const int n = m_tracks.rowCount();
    if (n == 0)
        return;
    int idx;
    if (m_shuffle && n > 1) {
        do {
            idx = int(QRandomGenerator::global()->bounded(n));
        } while (idx == m_index);
    } else {
        idx = m_index + 1;
        if (idx >= n) {
            if (!m_repeat) {
                m_player.stop();
                return;
            }
            idx = 0;
        }
    }
    setIndex(idx, true);
}

void MusicPlayer::previous()
{
    const int n = m_tracks.rowCount();
    if (n == 0)
        return;
    if (m_player.position() > 3000) {
        m_player.setPosition(0);
        return;
    }
    setIndex((m_index - 1 + n) % n, playing() || m_index < 0);
}

void MusicPlayer::seek(qint64 ms)
{
    m_player.setPosition(qMax<qint64>(0, ms));
}

void MusicPlayer::playIndex(int index)
{
    setIndex(index, true);
}

void MusicPlayer::setIndex(int index, bool autoplay)
{
    if (index < 0 || index >= m_tracks.rowCount()) {
        m_index = -1;
        m_player.stop();
        m_player.setSource(QUrl());
        m_tracks.setCurrentRow(-1);
        m_cover = QImage();
        emit coverChanged();
        emit currentChanged();
        emit durationChanged();
        return;
    }
    m_index = index;
    m_tracks.setCurrentRow(index);
    const Track &t = m_tracks.tracks().at(index);
    const QUrl source = QUrl::fromLocalFile(t.path);
    const bool samePath = !t.path.isEmpty() && m_player.source() == source;
    if (!samePath) {
        // a new file: its metadata / cover arrive with metaDataChanged. The
        // same file again (repeat of a one-track list, the same file twice)
        // keeps them - the player does not load it again, so they would not
        // come back.
        m_metaTitle.clear();
        m_metaArtist.clear();
        m_cover = QImage();
        emit coverChanged();
    }
    if (!QFileInfo::exists(t.path))
        setError(QStringLiteral("File not found: %1").arg(t.path));
    else
        setError(QString());
    if (samePath)
        m_player.setPosition(0);
    else
        m_player.setSource(source);
    if (autoplay)
        m_player.play();
    emit currentChanged();
    emit durationChanged();
}

void MusicPlayer::onStatus(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::EndOfMedia)
        next();
    else if (status == QMediaPlayer::InvalidMedia)
        setError(QStringLiteral("Cannot play this file"));
}

void MusicPlayer::onMetaData()
{
    const QMediaMetaData md = m_player.metaData();
    m_metaTitle = md.stringValue(QMediaMetaData::Title);
    m_metaArtist = md.stringValue(QMediaMetaData::ContributingArtist);
    if (m_metaArtist.isEmpty())
        m_metaArtist = md.stringValue(QMediaMetaData::AlbumArtist);

    QImage img = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();
    if (img.isNull())
        img = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
    m_cover = img;
    ++m_coverVersion;
    emit coverChanged();

    // Persist better titles/durations for the playlist view.
    if (const Track *cur = currentTrack()) {
        Track t = *cur;
        bool changed = false;
        if (!m_metaTitle.isEmpty() && t.title != m_metaTitle) { t.title = m_metaTitle; changed = true; }
        if (!m_metaArtist.isEmpty() && t.artist != m_metaArtist) { t.artist = m_metaArtist; changed = true; }
        const qint64 d = m_player.duration();
        if (d > 0 && t.durationMs != d) { t.durationMs = d; changed = true; }
        if (changed) {
            m_db->updateTrackInfo(t.id, t.title, t.artist, t.durationMs);
            m_tracks.updateTrack(m_index, t);
        }
    }
    emit currentChanged();
}

void MusicPlayer::toggleFavorite()
{
    const Track *curPtr = currentTrack();
    if (!curPtr)
        return;
    const Track cur = *curPtr;
    const qint64 fav = favoritesPlaylistId(true);
    if (fav < 0)
        return;
    bool removed = false;
    for (const Track &t : m_db->playlistTracks(fav)) {
        if (t.path == cur.path) {
            m_db->removeTrack(t.id);
            removed = true;
        }
    }
    if (!removed)
        m_db->addTrack(fav, cur);
    if (fav == m_playlistId) {
        loadPlaylist(m_playlistId, true);
    }
    emit playlistsChanged();
    emit currentChanged();
}

qint64 MusicPlayer::favoritesPlaylistId(bool create)
{
    for (const Playlist &p : m_db->playlists()) {
        if (p.name == kFavorites)
            return p.id;
    }
    return create ? m_db->createPlaylist(kFavorites) : -1;
}

// --- playlists -------------------------------------------------------------

void MusicPlayer::loadPlaylist(qint64 id, bool keepPlaying)
{
    const Track *cur = currentTrack();
    const QString currentPath = cur ? cur->path : QString();
    m_playlistId = id;
    m_db->setSetting(QStringLiteral("music/playlist"), id);
    m_tracks.setTracks(m_db->playlistTracks(id));

    if (keepPlaying && !currentPath.isEmpty()) {
        for (int i = 0; i < m_tracks.rowCount(); ++i) {
            if (m_tracks.tracks().at(i).path == currentPath) {
                m_index = i;
                m_tracks.setCurrentRow(i);
                emit playlistsChanged();
                emit currentChanged();
                return;
            }
        }
    }
    setIndex(m_tracks.rowCount() > 0 ? 0 : -1, false);
    emit playlistsChanged();
}

void MusicPlayer::selectPlaylist(qint64 id)
{
    if (id == m_playlistId)
        return;
    const bool wasPlaying = playing();
    loadPlaylist(id);
    if (wasPlaying && m_index >= 0)
        m_player.play();
}

qint64 MusicPlayer::createPlaylist(const QString &name)
{
    const QString n = name.trimmed().isEmpty() ? QStringLiteral("New Playlist") : name.trimmed();
    const qint64 id = m_db->createPlaylist(n);
    if (id >= 0)
        loadPlaylist(id);
    return id;
}

bool MusicPlayer::renamePlaylist(qint64 id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;
    const bool ok = m_db->renamePlaylist(id, name.trimmed());
    emit playlistsChanged();
    return ok;
}

bool MusicPlayer::deletePlaylist(qint64 id)
{
    if (!m_db->deletePlaylist(id))
        return false;
    QVector<Playlist> lists = m_db->playlists();
    if (lists.isEmpty()) {
        m_db->createPlaylist(QStringLiteral("Workout Mix"));
        lists = m_db->playlists();
    }
    if (id == m_playlistId)
        loadPlaylist(lists.isEmpty() ? -1 : lists.constFirst().id);
    else
        emit playlistsChanged();
    return true;
}

Track MusicPlayer::trackFromFile(const QString &path)
{
    Track t;
    t.path = QDir::cleanPath(path);
    const QString base = QFileInfo(path).completeBaseName();
    const int sep = base.indexOf(QStringLiteral(" - "));
    if (sep > 0) {
        t.artist = base.left(sep).trimmed();
        t.title = base.mid(sep + 3).trimmed();
    } else {
        t.title = base;
    }
    return t;
}

int MusicPlayer::addPaths(const QStringList &paths, qint64 playlistId)
{
    int added = 0;
    for (const QString &p : paths) {
        if (!supportedExtensions().contains(QFileInfo(p).suffix().toLower()))
            continue;
        if (m_db->addTrack(playlistId, trackFromFile(p)) >= 0)
            ++added;
    }
    return added;
}

int MusicPlayer::addFiles(const QList<QUrl> &files)
{
    if (m_playlistId < 0)
        return 0;
    QStringList paths;
    for (const QUrl &u : files)
        paths << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    const int n = addPaths(paths, m_playlistId);
    loadPlaylist(m_playlistId, true);
    return n;
}

int MusicPlayer::addFolder(const QUrl &folder)
{
    if (m_playlistId < 0)
        return 0;
    const QString dir = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    QStringList paths;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        paths << it.next();
    paths.sort(Qt::CaseInsensitive);
    const int n = addPaths(paths, m_playlistId);
    loadPlaylist(m_playlistId, true);
    return n;
}

void MusicPlayer::removeTrack(int index)
{
    if (index < 0 || index >= m_tracks.rowCount())
        return;
    const bool removingCurrent = index == m_index;
    m_db->removeTrack(m_tracks.tracks().at(index).id);
    if (removingCurrent) {
        m_player.stop();
        m_index = -1;
    }
    loadPlaylist(m_playlistId, !removingCurrent);
}

void MusicPlayer::moveTrack(int from, int to)
{
    const int n = m_tracks.rowCount();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    QVector<qint64> ids;
    for (const Track &t : m_tracks.tracks())
        ids << t.id;
    ids.move(from, to);
    m_db->reorderTracks(m_playlistId, ids);
    loadPlaylist(m_playlistId, true);
}

} // namespace watt
