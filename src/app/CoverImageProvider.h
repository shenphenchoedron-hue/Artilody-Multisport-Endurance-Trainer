#pragma once

#include <QQuickImageProvider>

namespace watt {

class MusicPlayer;

// Serves the album art of the current track as image://cover/<n>.
class CoverImageProvider : public QQuickImageProvider
{
public:
    explicit CoverImageProvider(MusicPlayer *player)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_player(player)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    MusicPlayer *m_player;
};

} // namespace watt
