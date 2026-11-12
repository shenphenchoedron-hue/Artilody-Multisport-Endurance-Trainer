<?php
// The users for the stations (UserSync in the app; lib/users.php):
//   GET  /api/users.php?since=<rev>  -> {"serverId":"...","rev":12,"users":[user, ...]}
//        the users (deleted ones as tombstones) changed after rev `since`
//   POST /api/users.php  {"station":"<uuid>","name":"<host>","nowMs":...,"users":[user, ...]}
//        -> {"serverId":"...","rev":13,"results":[{"uuid":"...","taken":true}, ...]}
//        a station's changes; the newest change of each user wins
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';

require_station_header();

const USERS_POST_MAX_BYTES = 1024 * 1024;

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
try {
    $pdo = artilody_db();
    if ($method === 'GET') {
        $since = max(0, (int)($_GET['since'] ?? 0));
        json_out(['serverId' => server_id($pdo), 'rev' => users_rev($pdo), 'users' => users_pull($pdo, $since)]);
    }
    if ($method === 'POST') {
        $body = file_get_contents('php://input', false, null, 0, USERS_POST_MAX_BYTES + 1);
        if ($body === false || strlen($body) > USERS_POST_MAX_BYTES) {
            json_out(['error' => 'too large'], 413);
        }
        $payload = json_decode($body, true);
        if (!is_array($payload)) {
            json_out(['error' => 'not JSON'], 400);
        }
        json_out(['serverId' => server_id($pdo)] + users_push($pdo, $payload));
    }
    header('Allow: GET, POST');
    json_out(['error' => 'method not allowed'], 405);
} catch (Throwable $e) {
    error_log('artilody users: ' . $e->getMessage());
    json_out(['error' => 'server error'], 500);
}
