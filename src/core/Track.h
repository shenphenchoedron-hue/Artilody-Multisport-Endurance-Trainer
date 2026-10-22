#pragma once

#include <QString>

namespace watt {

struct Track
{
    qint64 id = -1;
    QString path;          // local file path
    QString title;
    QString artist;
    qint64 durationMs = 0; // 0 = unknown until played
};

struct Playlist
{
    qint64 id = -1;
    QString name;
    int trackCount = 0;
};

} // namespace watt
