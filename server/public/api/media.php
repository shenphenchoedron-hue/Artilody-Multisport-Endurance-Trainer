<?php
// The media library for the stations (MediaSync in the app; lib/media.php):
//   GET  /api/media.php?since=<rev>  -> {"serverId":"...","rev":7,"media":[item, ...]}
//        the items (deleted ones as tombstones) changed after rev `since`
//   POST /api/media.php  {"kind":"gpx"|"video"|"workout"|"plan","name":"...","folder":"...",
//                         "station":"<host>","files":[{"name":"x.gpx","hash":"<sha1>"}, ...]}
//        -> {"serverId":"...","rev":8,"item":item,"created":true}
//        a new item, its files uploaded first (api/upload.php); the same files
//        already there: that item (created false)
// Files are downloaded from /blobs/<sha1> (Apache; ranges for resuming).
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';
require __DIR__ . '/../../lib/media.php';

require_station_header();

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
try {
    $pdo = artilody_db();
    if ($method === 'GET') {
        $since = max(0, (int)($_GET['since'] ?? 0));
        json_out(['serverId' => server_id($pdo), 'rev' => media_rev($pdo), 'media' => media_pull($pdo, $since)]);
    }
    if ($method === 'POST') {
        $body = file_get_contents('php://input', false, null, 0, 65537);
        $in = is_string($body) && strlen($body) <= 65536 ? json_decode($body, true) : null;
        if (!is_array($in)) {
            json_out(['error' => 'not JSON'], 400);
        }
        [$error, $item, $created] = media_add($pdo, $in, (string)($in['station'] ?? 'station'));
        if ($error !== '') {
            json_out(['error' => $error], 400);
        }
        json_out(['serverId' => server_id($pdo), 'rev' => media_rev($pdo), 'item' => $item, 'created' => $created]);
    }
    header('Allow: GET, POST');
    json_out(['error' => 'method not allowed'], 405);
} catch (Throwable $e) {
    error_log('artilody media: ' . $e->getMessage());
    json_out(['error' => 'server error'], 500);
}
