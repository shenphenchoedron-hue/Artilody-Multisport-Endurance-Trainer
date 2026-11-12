-- The Artilody server's database (MariaDB). Applied by install.sh on every
-- install / update: only CREATE ... IF NOT EXISTS and additive changes, so
-- running it again never loses data.

CREATE TABLE IF NOT EXISTS meta (
    k VARCHAR(64) NOT NULL PRIMARY KEY,
    v TEXT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO meta (k, v) VALUES ('schema_version', '6');
UPDATE meta SET v = '6' WHERE k = 'schema_version' AND CAST(v AS UNSIGNED) < 6;
-- This server's own id: a station that finds another id (a new / reinstalled
-- server) sends all its profiles again.
INSERT IGNORE INTO meta (k, v) VALUES ('server_id', UUID());
-- (admin_hash: the web interface's password, password_hash(); set with
-- `sudo artilody-admin-password`)

-- Every station (training Pi) that has been in touch (api/ping.php).
-- uuid: the station's own id (the app's Database::stationId()).
-- (the sizes of name / app_version / last_ip: STATION_*_MAX in lib/bootstrap.php)
CREATE TABLE IF NOT EXISTS stations (
    uuid CHAR(36) NOT NULL PRIMARY KEY,
    name VARCHAR(100) NOT NULL DEFAULT '',
    app_version VARCHAR(40) NOT NULL DEFAULT '',
    last_ip VARCHAR(45) NOT NULL DEFAULT '',
    first_seen DATETIME NOT NULL,   -- UTC
    last_seen DATETIME NOT NULL     -- UTC
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Change counters: every change of a user gets the next `users` number (its
-- rev), so a station asks only for what changed since its last look.
CREATE TABLE IF NOT EXISTS counters (
    k VARCHAR(32) NOT NULL PRIMARY KEY,
    v BIGINT NOT NULL
) ENGINE=InnoDB;
INSERT IGNORE INTO counters (k, v) VALUES ('users', 0);

-- The users (the stations' profiles; the uuid is the profile's uuid).
-- The newest change wins (updated_ms, ms since 1970 UTC, by the server's
-- clock); a deleted user stays as a tombstone (deleted = 1) so the stations
-- learn about it. Limits and fields: lib/users.php (user_clean()).
CREATE TABLE IF NOT EXISTS users (
    uuid CHAR(36) NOT NULL PRIMARY KEY,
    name VARCHAR(100) NOT NULL DEFAULT '',
    weight_kg DOUBLE NOT NULL DEFAULT 75,
    height_cm DOUBLE NOT NULL DEFAULT 0,
    ftp INT NOT NULL DEFAULT 250,
    rowing_ftp INT NOT NULL DEFAULT 150,
    max_hr INT NOT NULL DEFAULT 185,
    rest_hr INT NOT NULL DEFAULT 60,
    cadence INT NOT NULL DEFAULT 85,
    default_erg TINYINT NOT NULL DEFAULT 1,
    zones TEXT NOT NULL,            -- JSON: the 5 upper bounds (fractions of FTP)
    avatar3d TEXT NOT NULL,         -- JSON: the cyclist  { model, jersey, ... }
    runner3d TEXT NOT NULL,         -- JSON: the runner
    rower3d TEXT NOT NULL,          -- JSON: the rower
    picture_hash CHAR(40) NULL,     -- pictures.hash (sha1), NULL: none
    deleted TINYINT NOT NULL DEFAULT 0,
    updated_ms BIGINT NOT NULL,
    updated_by VARCHAR(100) NOT NULL DEFAULT '',   -- station name / 'web'
    rev BIGINT NOT NULL,
    created_at DATETIME NOT NULL,   -- UTC
    KEY idx_users_rev (rev)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Profile pictures, by their sha1 (the same picture is stored once).
CREATE TABLE IF NOT EXISTS pictures (
    hash CHAR(40) NOT NULL PRIMARY KEY,
    type VARCHAR(40) NOT NULL,      -- image/jpeg, image/png, image/webp, image/gif
    data MEDIUMBLOB NOT NULL,
    created_at DATETIME NOT NULL
) ENGINE=InnoDB;

-- The media library (step 4): GPX routes and video routes. The files are
-- kept on disk by their sha1 (/var/lib/artilody/blobs/<sha1>, served by
-- Apache at /blobs/<sha1>, so downloads resume with HTTP ranges); the same
-- file is kept once. A deleted item stays as a tombstone (deleted = 1).
INSERT IGNORE INTO counters (k, v) VALUES ('media', 0);

-- kind: 'gpx' (one .gpx) or 'video' (a folder: video + .gpx [+ .xml]).
-- folder: the video route's folder name on the stations ('' for gpx).
CREATE TABLE IF NOT EXISTS media (
    uuid CHAR(36) NOT NULL PRIMARY KEY,
    kind VARCHAR(10) NOT NULL,
    name VARCHAR(200) NOT NULL,
    folder VARCHAR(200) NOT NULL DEFAULT '',
    size_bytes BIGINT NOT NULL DEFAULT 0,
    deleted TINYINT NOT NULL DEFAULT 0,
    updated_ms BIGINT NOT NULL,
    uploaded_by VARCHAR(100) NOT NULL DEFAULT '',
    rev BIGINT NOT NULL,
    created_at DATETIME NOT NULL,
    KEY idx_media_rev (rev)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The files of an item: its file name on the stations and the blob.
CREATE TABLE IF NOT EXISTS media_files (
    media_uuid CHAR(36) NOT NULL,
    filename VARCHAR(255) NOT NULL,
    hash CHAR(40) NOT NULL,
    size_bytes BIGINT NOT NULL,
    PRIMARY KEY (media_uuid, filename),
    KEY idx_media_files_hash (hash)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The rides (step 5): every station's completed rides / rows / runs, sent
-- once and never changed; a deleted one stays as a tombstone (deleted = 1,
-- its data cleared). The fields: lib/rides.php.
INSERT IGNORE INTO counters (k, v) VALUES ('rides', 0);

CREATE TABLE IF NOT EXISTS rides (
    uuid CHAR(36) NOT NULL PRIMARY KEY,
    user_uuid CHAR(36) NOT NULL,
    station_uuid CHAR(36) NOT NULL DEFAULT '',
    station_name VARCHAR(100) NOT NULL DEFAULT '',
    sport VARCHAR(20) NOT NULL DEFAULT 'cycling',
    workout_name VARCHAR(200) NOT NULL DEFAULT '',
    started_at BIGINT NOT NULL DEFAULT 0,      -- s since 1970 UTC
    duration INT NOT NULL DEFAULT 0,           -- s
    blocks INT NOT NULL DEFAULT 0,
    erg TINYINT NOT NULL DEFAULT 0,
    ftp INT NOT NULL DEFAULT 0,
    avg_power DOUBLE NOT NULL DEFAULT 0,
    max_power INT NOT NULL DEFAULT 0,
    np DOUBLE NOT NULL DEFAULT 0,
    avg_hr DOUBLE NOT NULL DEFAULT 0,
    max_hr INT NOT NULL DEFAULT 0,
    avg_cadence DOUBLE NOT NULL DEFAULT 0,
    kj DOUBLE NOT NULL DEFAULT 0,
    if_value DOUBLE NOT NULL DEFAULT 0,
    tss DOUBLE NOT NULL DEFAULT 0,
    zones TEXT NOT NULL,                       -- JSON: 6 zones' seconds
    distance_m DOUBLE NOT NULL DEFAULT 0,
    bests TEXT NOT NULL,                       -- JSON: best average power {"5":..,"60":..,...} (s -> W)
    samples MEDIUMBLOB NULL,                   -- gzip of JSON [[t, power, target, cadence, hr, speed], ...]
    laps TEXT NULL,                            -- JSON [[lap, time_s, distance_m], ...]
    trace MEDIUMTEXT NULL,                     -- JSON: its ghost trace (GPX Ride / Treadmill Run) or NULL
    deleted TINYINT NOT NULL DEFAULT 0,
    rev BIGINT NOT NULL,
    received_at DATETIME NOT NULL,
    KEY idx_rides_user (user_uuid, started_at),
    KEY idx_rides_rev (rev)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Riding together (step 6): where each station's rider is on its route right
-- now (api/live.php, about once a second while riding) - a station's GPX Ride
-- and Treadmill Run apart. A row older than a few seconds is not shown; one
-- older than a minute is removed. (Schema 5's table `live` had the station
-- alone as its key; it held seconds-old rows only, so it simply goes.)
DROP TABLE IF EXISTS live;
CREATE TABLE IF NOT EXISTS live_riders (
    station_uuid CHAR(36) NOT NULL,
    sport VARCHAR(10) NOT NULL,                -- 'gpx' (GPX Ride) / 'run' (Treadmill Run)
    user_uuid CHAR(36) NOT NULL DEFAULT '',
    name VARCHAR(100) NOT NULL DEFAULT '',
    route_key VARCHAR(64) NOT NULL,
    distance_m DOUBLE NOT NULL DEFAULT 0,      -- place on the route
    is_back TINYINT NOT NULL DEFAULT 0,        -- on the way back (a looped there-and-back road)
    speed_kmh DOUBLE NOT NULL DEFAULT 0,
    watts INT NOT NULL DEFAULT 0,
    updated_ms BIGINT NOT NULL,
    PRIMARY KEY (station_uuid, sport),
    KEY idx_live_route (sport, route_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
