<?php
// GET /api/ping.php - "is the server there?" Asked by every station every
// ServerConnection::kIntervalMs (30 s) while its Server setting is on
// (src/core/ServerConnection.cpp in the app). This comment is the contract
// between the two - change both sides together.
//
// Request headers (optional): X-Artilody-Station (the station's uuid),
// X-Artilody-Name (its host name), X-Artilody-Version (the app's version) -
// a known station is recorded in `stations` (last seen, version, address).
//
// Answer: {"app":"artilody-server","api":1,"version":"0.1.0","name":"...",
//          "time":"2026-11-12T18:00:00+00:00","db":true,"station":true,
//          "serverId":"...","usersRev":12,"mediaRev":7,"ridesRev":40}
// app: always "artilody-server"; api: ARTILODY_API (the station warns when it
// is newer than its own); db: the database answers; station: this station
// was recorded (false with db true: the station shows a warning); serverId /
// usersRev / mediaRev / ridesRev (with db): this server's id and the change
// numbers of its users, media library and rides - the station fetches them
// when they differ from what it has (UserSync, MediaSync, RideSync).
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';
require __DIR__ . '/../../lib/media.php';
require __DIR__ . '/../../lib/rides.php';

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if ($method !== 'GET' && $method !== 'HEAD') {
    header('Allow: GET, HEAD');
    json_out(['error' => 'method not allowed'], 405);
}

$station = strtolower(request_header('X-Artilody-Station', 36));
$db = false;
$recorded = false;
$users = [];
try {
    $pdo = artilody_db();
    $users = ['serverId' => server_id($pdo), 'usersRev' => users_rev($pdo), 'mediaRev' => media_rev($pdo), 'ridesRev' => rides_rev($pdo)];
    $db = true;
    if (is_uuid($station)) {
        $q = $pdo->prepare(
            'INSERT INTO stations (uuid, name, app_version, last_ip, first_seen, last_seen)'
            . ' VALUES (?, ?, ?, ?, UTC_TIMESTAMP(), UTC_TIMESTAMP())'
            . ' ON DUPLICATE KEY UPDATE name = VALUES(name), app_version = VALUES(app_version),'
            . ' last_ip = VALUES(last_ip), last_seen = UTC_TIMESTAMP()'
        );
        $q->execute([
            $station,
            request_header('X-Artilody-Name', STATION_NAME_MAX),
            request_header('X-Artilody-Version', STATION_VERSION_MAX),
            mb_substr((string)($_SERVER['REMOTE_ADDR'] ?? ''), 0, STATION_IP_MAX),
        ]);
        $recorded = true;
    }
} catch (Throwable $e) {
    error_log('artilody ping: ' . $e->getMessage());
}

json_out([
    'app' => 'artilody-server',
    'api' => ARTILODY_API,
    'version' => ARTILODY_SERVER_VERSION,
    'name' => artilody_server_name(),
    'time' => gmdate('c'),
    'db' => $db,
    'station' => $recorded,
] + $users);
