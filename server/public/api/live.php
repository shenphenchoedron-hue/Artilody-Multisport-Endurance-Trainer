<?php
// Riding together (docs/server-plan.md, step 6; LiveRide in the app). Each
// station on a GPX Ride / Treadmill Run route asks about once a second:
//   POST /api/live.php {"station":"<uuid>","userUuid","name","sport":"gpx"|"run",
//                       "routeKey","riding":true,"distanceM","returning","speedKmh","watts",
//                       "look":{model, jersey, ...}}   (the rider's 3D figure: avatar3d / runner3d)
//   -> {"riders":[{"station","userUuid","name","distanceM","returning","speedKmh","watts","look","ageMs"}, ...]}
// riding true: its place is kept (shown to the others); false (only looking,
// or finished): its row for this sport is removed (a station's GPX Ride and
// Treadmill Run are kept apart). The answer: the others on the same route
// (sport and routeKey) seen in the last LIVE_SHOWN_MS.
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';

require_station_header();

const LIVE_SHOWN_MS = 5000;
const LIVE_KEPT_MS = 60000;

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    header('Allow: POST');
    json_out(['error' => 'method not allowed'], 405);
}
$in = json_decode((string)file_get_contents('php://input', false, null, 0, 4097), true);
if (!is_array($in)) {
    json_out(['error' => 'not JSON'], 400);
}
$station = strtolower((string)($in['station'] ?? ''));
$sport = in_array($in['sport'] ?? '', ['gpx', 'run'], true) ? $in['sport'] : '';
$route = mb_substr((string)($in['routeKey'] ?? ''), 0, 64);
if (!is_uuid($station) || $sport === '' || $route === '') {
    json_out(['error' => 'station, sport and routeKey are needed'], 400);
}
$num = fn($k, $max) => is_numeric($in[$k] ?? null) ? max(0.0, min($max, (float)$in[$k])) : 0.0;
try {
    $pdo = artilody_db();
    $now = now_ms();
    if (!empty($in['riding'])) {
        $user = strtolower((string)($in['userUuid'] ?? ''));
        // (the rider's own 3D figure and colours: the others draw it so)
        $look = is_array($in['look'] ?? null) ? json_encode(avatar_clean($in['look'])) : null;
        $pdo->prepare('INSERT INTO live_riders (station_uuid, user_uuid, name, sport, route_key, distance_m, is_back, speed_kmh, watts, look, updated_ms)'
            . ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE user_uuid = VALUES(user_uuid), name = VALUES(name),'
            . ' sport = VALUES(sport), route_key = VALUES(route_key), distance_m = VALUES(distance_m), is_back = VALUES(is_back),'
            . ' speed_kmh = VALUES(speed_kmh), watts = VALUES(watts), look = VALUES(look), updated_ms = VALUES(updated_ms)')
            ->execute([$station, is_uuid($user) ? $user : '', mb_substr(trim((string)($in['name'] ?? '')), 0, 100), $sport, $route,
                $num('distanceM', 1e7), empty($in['returning']) ? 0 : 1, $num('speedKmh', 200), (int)$num('watts', 5000), $look, $now]);
    } else {
        $pdo->prepare('DELETE FROM live_riders WHERE station_uuid = ? AND sport = ?')->execute([$station, $sport]);
    }
    if (random_int(0, 30) === 0) {
        $pdo->prepare('DELETE FROM live_riders WHERE updated_ms < ?')->execute([$now - LIVE_KEPT_MS]);
    }
    $q = $pdo->prepare('SELECT station_uuid, user_uuid, name, distance_m, is_back, speed_kmh, watts, look, updated_ms FROM live_riders'
        . ' WHERE sport = ? AND route_key = ? AND station_uuid <> ? AND updated_ms >= ? ORDER BY name');
    $q->execute([$sport, $route, $station, $now - LIVE_SHOWN_MS]);
    $riders = array_map(fn($r) => [
        'station' => $r['station_uuid'],
        'userUuid' => $r['user_uuid'],
        'name' => $r['name'],
        'distanceM' => round((float)$r['distance_m'], 1),
        'returning' => (bool)$r['is_back'],
        'speedKmh' => round((float)$r['speed_kmh'], 1),
        'watts' => (int)$r['watts'],
        'look' => $r['look'] !== null ? (json_decode((string)$r['look'], true) ?: (object)[]) : (object)[],
        'ageMs' => max(0, $now - (int)$r['updated_ms']),
    ], $q->fetchAll());
    json_out(['riders' => $riders]);
} catch (Throwable $e) {
    error_log('artilody live: ' . $e->getMessage());
    json_out(['error' => 'server error'], 500);
}
