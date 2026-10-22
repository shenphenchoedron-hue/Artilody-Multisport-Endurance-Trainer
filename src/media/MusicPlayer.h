#pragma once

#include "TrackListModel.h"

#include <QAudioOutput>
#include <QImage>
#include <QMediaPlayer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

namespace watt {

class Database;

// Local music player with user-defined playlists (stored in SQLite).
// Exposed to QML as "Music".
class MusicPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY currentChanged)
    Q_PROPERTY(QString coverSource READ coverSource NOTIFY coverChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY currentChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)
    Q_PROPERTY(bool repeat READ repeat WRITE setRepeat NOTIFY modeChanged)
    Q_PROPERTY(bool favorite READ favorite NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(TrackListModel *tracks READ tracks CONSTANT)
    Q_PROPERTY(QVariantList upNext READ upNext NOTIFY currentChanged)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(qint64 currentPlaylistId READ currentPlaylistId NOTIFY playlistsChanged)
    Q_PROPERTY(QString currentPlaylistName READ currentPlaylistName NOTIFY playlistsChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit MusicPlayer(Database *db, QObject *parent = nullptr);
    ~MusicPlayer() override;

    // Loads playlists; on first start creates "Workout Mix" from the given folders.
    void initialize(const QStringList &defaultMusicFolders);
    static QStringList supportedExtensions();

    QString title() const;
    QString artist() const;
    QString coverSource() const;
    QImage cover() const { return m_cover; }
    bool hasTrack() const { return m_index >= 0; }
    bool playing() const;
    qint64 position() const { return m_player.position(); }
    qint64 duration() const;
    QString positionText() const;
    QString durationText() const;
    double volume() const { return m_audio.volume(); }
    void setVolume(double v);
    bool muted() const { return m_audio.isMuted(); }
    void setMuted(bool m);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool s);
    bool repeat() const { return m_repeat; }
    void setRepeat(bool r);
    bool favorite() const;
    int currentIndex() const { return m_index; }
    TrackListModel *tracks() { return &m_tracks; }
    QVariantList upNext() const;
    QVariantList playlists() const;
    qint64 currentPlaylistId() const { return m_playlistId; }
    QString currentPlaylistName() const;
    QString errorText() const { return m_error; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE void toggleFavorite();

    Q_INVOKABLE void selectPlaylist(qint64 id);
    Q_INVOKABLE qint64 createPlaylist(const QString &name);
    Q_INVOKABLE bool renamePlaylist(qint64 id, const QString &name);
    Q_INVOKABLE bool deletePlaylist(qint64 id);
    Q_INVOKABLE int addFiles(const QList<QUrl> &files);
    Q_INVOKABLE int addFolder(const QUrl &folder);
    Q_INVOKABLE void removeTrack(int index);
    Q_INVOKABLE void moveTrack(int from, int to);

signals:
    void currentChanged();
    void coverChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void modeChanged();
    void playlistsChanged();
    void errorTextChanged();

private:
    void loadPlaylist(qint64 id, bool keepPlaying = false);
    void setIndex(int index, bool autoplay);
    void onMetaData();
    void onStatus(QMediaPlayer::MediaStatus status);
    int addPaths(const QStringList &paths, qint64 playlistId);
    qint64 favoritesPlaylistId(bool create);
    static Track trackFromFile(const QString &path);
    void setError(const QString &e);

    Database *m_db;
    QMediaPlayer m_player;
    QAudioOutput m_audio;
    TrackListModel m_tracks;
    qint64 m_playlistId = -1;
    int m_index = -1;
    bool m_shuffle = false;
    bool m_repeat = true;
    QImage m_cover;
    int m_coverVersion = 0;
    QString m_metaTitle, m_metaArtist;
    QString m_error;
    QTimer m_volumeSave;   // debounces volume persistence while dragging
};

} // namespace watt
