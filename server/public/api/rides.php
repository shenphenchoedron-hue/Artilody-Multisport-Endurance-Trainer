<?php
// The rides for the stations (RideSync in the app; lib/rides.php):
//   GET  /api/rides.php?since=<rev>  -> {"serverId","rev","more":false,"rides":[summary, ...]}
//        the rides (deleted ones as tombstones) changed after rev `since`,
//        without samples / laps / trace (at most 5000: "more" - ask again)
//   GET  /api/rides.php?uuid=<uuid>  -> {"ride": ride} (with samples, laps, trace)
//   POST /api/rides.php  {"station","name","rides":[ride | {uuid, deleted:true}, ...]}
//        -> {"serverId","rev","results":[{"uuid","taken",("deleted")}]}
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';
require __DIR__ . '/../../lib/rides.php';

require_station_header();

const RIDES_POST_MAX_BYTES = 15 * 1024 * 1024;   // (php: post_max_size 16M)
const RIDES_PULL_LIMIT = 5000;

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
try {
    $pdo = artilody_db();
    if ($method === 'GET' && isset($_GET['uuid'])) {
        $uuid = strtolower((string)$_GET['uuid']);
        $ride = is_uuid($uuid) ? ride_get($pdo, $uuid) : null;
        if (!$ride || $ride['deleted']) {
            json_out(['error' => 'no such ride'], 404);
        }
        json_out(['ride' => $ride]);
    }
    if ($method === 'GET') {
        // (the rev first: a ride stored meanwhile comes with the next look, never lost)
        $rev = rides_rev($pdo);
        $rides = rides_pull($pdo, max(0, (int)($_GET['since'] ?? 0)), RIDES_PULL_LIMIT + 1);
        $more = count($rides) > RIDES_PULL_LIMIT;
        $rides = array_slice($rides, 0, RIDES_PULL_LIMIT);
        $rides = array_values(array_filter($rides, fn($r) => $r['rev'] <= $rev || $more));
        if ($more) {
            $rev = end($rides)['rev'];
        }
        json_out(['serverId' => server_id($pdo), 'rev' => $rev, 'more' => $more, 'rides' => $rides]);
    }
    if ($method === 'POST') {
        set_time_limit(300);
        $body = file_get_contents('php://input', false, null, 0, RIDES_POST_MAX_BYTES + 1);
        if ($body === false || strlen($body) > RIDES_POST_MAX_BYTES) {
            json_out(['error' => 'too large'], 413);
        }
        $payload = json_decode($body, true);
        if (!is_array($payload)) {
            json_out(['error' => 'not JSON'], 400);
        }
        json_out(['serverId' => server_id($pdo)] + rides_push($pdo, $payload));
    }
    header('Allow: GET, POST');
    json_out(['error' => 'method not allowed'], 405);
} catch (Throwable $e) {
    error_log('artilody rides: ' . $e->getMessage());
    json_out(['error' => 'server error'], 500);
}
