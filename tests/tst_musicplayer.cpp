// MusicPlayer robustness: no crash without playlists (database refuses to
// create one), bounds-checked accessors, deleting the last playlist.
#include "Database.h"
#include "MusicPlayer.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

class TestMusicPlayer : public QObject
{
    Q_OBJECT
private slots:
    void noPlaylistIsNoCrash()
    {
        Database db;   // never opened: every query fails, no playlist can be created
        MusicPlayer m(&db);
        m.initialize({});
        QVERIFY(!m.hasTrack());
        QCOMPARE(m.currentIndex(), -1);
        QCOMPARE(m.title(), QStringLiteral("No music"));
        QCOMPARE(m.artist(), QStringLiteral("Add music to a playlist"));
        QCOMPARE(m.duration(), qint64(0));
        QVERIFY(!m.favorite());
        QVERIFY(m.upNext().isEmpty());
        m.play();
        m.next();
        m.previous();
        m.playIndex(3);
        m.toggleFavorite();
        QVERIFY(!m.deletePlaylist(1));
        QVERIFY(!m.hasTrack());
    }

    void deletingTheLastPlaylistKeepsOne()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("m.sqlite"))));
        db.seedDefaults();
        MusicPlayer m(&db);
        m.initialize({ dir.filePath(QStringLiteral("no-music-here")) });
        QCOMPARE(m.playlists().size(), 1);
        const qint64 id = m.playlists().constFirst().toMap().value(QStringLiteral("id")).toLongLong();
        QVERIFY(m.deletePlaylist(id));
        QCOMPARE(m.playlists().size(), 1);   // a new empty "Workout Mix"
        QVERIFY(!m.currentPlaylistName().isEmpty());
        QVERIFY(!m.hasTrack());
    }

    void tracksAndTheSameFileTwice()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("m.sqlite"))));
        db.seedDefaults();
        const QString file = dir.filePath(QStringLiteral("Artist - Song.mp3"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not really audio");
        f.close();
        MusicPlayer m(&db);
        m.initialize({ dir.path() });
        QCOMPARE(m.addFiles({ QUrl::fromLocalFile(file) }), 1);   // now twice in the list
        QVERIFY(m.hasTrack());
        QCOMPARE(m.title(), QStringLiteral("Song"));
        QCOMPARE(m.artist(), QStringLiteral("Artist"));
        m.playIndex(1);   // the same file again: no reload, nothing lost
        QCOMPARE(m.currentIndex(), 1);
        QCOMPARE(m.title(), QStringLiteral("Song"));
        m.pause();
        m.removeTrack(1);
        QVERIFY(m.currentIndex() < 1);
        QCOMPARE(m.duration(), qint64(0));
        m.removeTrack(0);
        QVERIFY(!m.hasTrack());
        QCOMPARE(m.title(), QStringLiteral("No music"));
    }
};

QTEST_MAIN(TestMusicPlayer)
#include "tst_musicplayer.moc"
