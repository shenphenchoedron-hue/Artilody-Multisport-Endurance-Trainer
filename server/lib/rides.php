<?php
// The rides (docs/server-plan.md, step 5): every station's completed rides,
// rows and runs - sent once (RideSync in the app), never changed, fetched by
// the other stations and analysed on the web. Used by api/rides.php,
// training.php and ride.php.
//
// A ride as exchanged with the stations (JSON):
//   { "uuid", "userUuid", "sport": "cycling" | "rowing" | "running",
//     "workoutName", "startedAt" (s since 1970), "durationSec", "blocks", "erg",
//     "ftp", "avgPower", "maxPower", "np", "avgHr", "maxHr", "avgCadence", "kj",
//     "ifValue", "tss", "zones": [6 x s], "distanceM",
//     "samples": [[t, power, target, cadence, hr, speed (m/s) | null], ...],
//     "laps": [[lap, timeSec, distanceM], ...],
//     "trace": { "uuid", "sport", "routeKey", "routeName", "startM", "loop",
//                "completed", "durationSec", "distanceM", "createdAt",
//                "points": [[t, d], ...] } | null,
//     "stationName", "deleted": false, "rev" }
// The list (GET ?since) leaves samples, laps and trace out.
declare(strict_types=1);

const RIDE_SPORTS = ['cycling', 'rowing', 'running'];
const RIDE_MAX_SAMPLES = 24 * 3600;
const RIDE_BEST_SECONDS = [5, 60, 300, 1200, 3600];   // the best average power over these

function rides_rev(PDO $pdo): int
{
    return (int)$pdo->query("SELECT v FROM counters WHERE k = 'rides'")->fetchColumn();
}

function rides_next_rev(PDO $pdo): int
{
    $pdo->exec("UPDATE counters SET v = LAST_INSERT_ID(v + 1) WHERE k = 'rides'");
    return (int)$pdo->query('SELECT LAST_INSERT_ID()')->fetchColumn();
}

function fnum($v, float $min = 0, float $max = 1e9): float
{
    return is_numeric($v) ? max($min, min($max, (float)$v)) : 0.0;
}

// The samples checked: [[t, power, target, cadence, hr, speed|null], ...] by t.
function ride_clean_samples($v): array
{
    $out = [];
    if (!is_array($v)) {
        return $out;
    }
    foreach ($v as $s) {
        if (!is_array($s) || count($s) < 5 || count($out) >= RIDE_MAX_SAMPLES) {
            continue;
        }
        $out[] = [
            (int)fnum($s[0], 0, 7 * 86400), (int)fnum($s[1], 0, 5000), (int)fnum($s[2], 0, 5000),
            (int)fnum($s[3], 0, 400), (int)fnum($s[4], 0, 260),
            isset($s[5]) && is_numeric($s[5]) ? round(fnum($s[5], 0, 100), 2) : null,
        ];
    }
    usort($out, fn($a, $b) => $a[0] <=> $b[0]);
    return $out;
}

// The best average power over each of RIDE_BEST_SECONDS (1 sample per s).
function ride_bests(array $samples): array
{
    $power = array_column($samples, 1);
    $n = count($power);
    $bests = [];
    foreach (RIDE_BEST_SECONDS as $w) {
        if ($n < $w) {
            continue;
        }
        $sum = array_sum(array_slice($power, 0, $w));
        $best = $sum;
        for ($i = $w; $i < $n; ++$i) {
            $sum += $power[$i] - $power[$i - $w];
            $best = max($best, $sum);
        }
        $bests[(string)$w] = (int)round($best / $w);
    }
    return $bests;
}

function ride_clean_trace($t): ?array
{
    if (!is_array($t) || !is_array($t['points'] ?? null) || count($t['points']) < 2) {
        return null;
    }
    $points = [];
    foreach ($t['points'] as $p) {
        if (is_array($p) && count($p) >= 2 && count($points) < RIDE_MAX_SAMPLES * 2) {
            $points[] = [round(fnum($p[0], 0, 7 * 86400), 1), round(fnum($p[1], 0, 1e7), 1)];
        }
    }
    $uuid = strtolower((string)($t['uuid'] ?? ''));
    return [
        'uuid' => is_uuid($uuid) ? $uuid : new_uuid(),
        'sport' => in_array($t['sport'] ?? '', ['gpx', 'run'], true) ? $t['sport'] : 'gpx',
        'routeKey' => mb_substr((string)($t['routeKey'] ?? ''), 0, 64),
        'routeName' => mb_substr((string)($t['routeName'] ?? ''), 0, 200),
        'startM' => fnum($t['startM'] ?? 0, 0, 1e7),
        'loop' => !empty($t['loop']),
        'completed' => !empty($t['completed']),
        'durationSec' => fnum($t['durationSec'] ?? 0, 0, 7 * 86400),
        'distanceM' => fnum($t['distanceM'] ?? 0, 0, 1e7),
        'createdAt' => mb_substr((string)($t['createdAt'] ?? ''), 0, 40),
        'points' => $points,
    ];
}

function ride_from_row(array $r, bool $full): array
{
    $ride = [
        'uuid' => $r['uuid'],
        'userUuid' => $r['user_uuid'],
        'stationName' => $r['station_name'],
        'sport' => $r['sport'],
        'workoutName' => $r['workout_name'],
        'startedAt' => (int)$r['started_at'],
        'durationSec' => (int)$r['duration'],
        'blocks' => (int)$r['blocks'],
        'erg' => (bool)$r['erg'],
        'ftp' => (int)$r['ftp'],
        'avgPower' => (float)$r['avg_power'],
        'maxPower' => (int)$r['max_power'],
        'np' => (float)$r['np'],
        'avgHr' => (float)$r['avg_hr'],
        'maxHr' => (int)$r['max_hr'],
        'avgCadence' => (float)$r['avg_cadence'],
        'kj' => (float)$r['kj'],
        'ifValue' => (float)$r['if_value'],
        'tss' => (float)$r['tss'],
        'zones' => json_decode((string)$r['zones'], true) ?: [],
        'distanceM' => (float)$r['distance_m'],
        'bests' => json_decode((string)$r['bests'], true) ?: (object)[],
        'deleted' => (bool)$r['deleted'],
        'rev' => (int)$r['rev'],
    ];
    if ($full) {
        $ride['samples'] = $r['samples'] !== null ? (json_decode((string)gzuncompress($r['samples']), true) ?: []) : [];
        $ride['laps'] = $r['laps'] !== null ? (json_decode((string)$r['laps'], true) ?: []) : [];
        $ride['trace'] = $r['trace'] !== null ? json_decode((string)$r['trace'], true) : null;
    }
    return $ride;
}

const RIDE_SUMMARY_COLUMNS = 'uuid, user_uuid, station_name, sport, workout_name, started_at, duration, blocks, erg, ftp,'
    . ' avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones, distance_m, bests, deleted, rev';

function rides_pull(PDO $pdo, int $since, int $limit = 5000): array
{
    $q = $pdo->prepare('SELECT ' . RIDE_SUMMARY_COLUMNS . ' FROM rides WHERE rev > ? ORDER BY rev LIMIT ' . max(1, $limit));
    $q->execute([$since]);
    return array_map(fn($r) => ride_from_row($r, false), $q->fetchAll());
}

function ride_get(PDO $pdo, string $uuid, bool $full = true): ?array
{
    $q = $pdo->prepare('SELECT * FROM rides WHERE uuid = ?');
    $q->execute([$uuid]);
    $r = $q->fetch();
    return $r ? ride_from_row($r, $full) : null;
}

// A user's rides (not deleted), newest first.
function rides_of_user(PDO $pdo, string $userUuid, int $limit = 1000, int $offset = 0): array
{
    $q = $pdo->prepare('SELECT ' . RIDE_SUMMARY_COLUMNS . ' FROM rides WHERE user_uuid = ? AND deleted = 0'
        . ' ORDER BY started_at DESC LIMIT ' . max(1, $limit) . ' OFFSET ' . max(0, $offset));
    $q->execute([$userUuid]);
    return array_map(fn($r) => ride_from_row($r, false), $q->fetchAll());
}

// A station's rides and deletions: { "station", "name", "rides": [ride | {uuid, deleted: true}] }.
// A ride already here is not changed (taken: it is here); one deleted here
// answers "deleted": the station removes its copy too.
function rides_push(PDO $pdo, array $payload): array
{
    $station = strtolower((string)($payload['station'] ?? ''));
    $station = is_uuid($station) ? $station : '';
    $by = mb_substr(trim((string)($payload['name'] ?? '')), 0, 100);
    $results = [];
    $ins = $pdo->prepare('INSERT INTO rides (uuid, user_uuid, station_uuid, station_name, sport, workout_name, started_at,'
        . ' duration, blocks, erg, ftp, avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones,'
        . ' distance_m, bests, samples, laps, trace, deleted, rev, received_at)'
        . ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, UTC_TIMESTAMP())');
    foreach (is_array($payload['rides'] ?? null) ? $payload['rides'] : [] as $r) {
        $uuid = strtolower((string)($r['uuid'] ?? ''));
        if (!is_array($r) || !is_uuid($uuid)) {
            continue;
        }
        $pdo->beginTransaction();
        try {
            $q = $pdo->prepare('SELECT deleted FROM rides WHERE uuid = ? FOR UPDATE');
            $q->execute([$uuid]);
            $have = $q->fetchColumn();
            if (!empty($r['deleted'])) {
                if ($have === false) {   // (never here: a tombstone, so it is never taken later)
                    $pdo->prepare("INSERT INTO rides (uuid, user_uuid, zones, bests, deleted, rev, received_at)"
                        . " VALUES (?, '', '[]', '{}', 1, ?, UTC_TIMESTAMP())")->execute([$uuid, rides_next_rev($pdo)]);
                } elseif ((int)$have === 0) {
                    $pdo->prepare('UPDATE rides SET deleted = 1, samples = NULL, laps = NULL, trace = NULL, rev = ? WHERE uuid = ?')
                        ->execute([rides_next_rev($pdo), $uuid]);
                }
                $results[] = ['uuid' => $uuid, 'taken' => true, 'deleted' => true];
            } elseif ($have !== false) {
                $results[] = ['uuid' => $uuid, 'taken' => true] + ((int)$have === 1 ? ['deleted' => true] : []);
            } else {
                $user = strtolower((string)($r['userUuid'] ?? ''));
                if (!is_uuid($user)) {
                    $results[] = ['uuid' => $uuid, 'taken' => false, 'error' => 'no user'];
                    $pdo->commit();
                    continue;
                }
                $samples = ride_clean_samples($r['samples'] ?? null);
                $laps = [];
                foreach (is_array($r['laps'] ?? null) ? $r['laps'] : [] as $l) {
                    if (is_array($l) && count($l) >= 2 && count($laps) < 10000) {
                        $laps[] = [(int)fnum($l[0], 0, 100000), round(fnum($l[1], 0, 7 * 86400), 2), round(fnum($l[2] ?? 0, 0, 1e7), 1)];
                    }
                }
                $zones = array_map(fn($z) => (int)fnum($z, 0, 7 * 86400), array_slice(is_array($r['zones'] ?? null) ? array_values($r['zones']) : [], 0, 6));
                $trace = ride_clean_trace($r['trace'] ?? null);
                $sport = in_array($r['sport'] ?? '', RIDE_SPORTS, true) ? $r['sport'] : 'cycling';
                $ins->execute([
                    $uuid, $user, $station, $by, $sport, mb_substr((string)($r['workoutName'] ?? ''), 0, 200),
                    (int)fnum($r['startedAt'] ?? 0, 0, 4e9), (int)fnum($r['durationSec'] ?? 0, 0, 7 * 86400),
                    (int)fnum($r['blocks'] ?? 0, 0, 10000), empty($r['erg']) ? 0 : 1, (int)fnum($r['ftp'] ?? 0, 0, 2000),
                    fnum($r['avgPower'] ?? 0, 0, 5000), (int)fnum($r['maxPower'] ?? 0, 0, 5000), fnum($r['np'] ?? 0, 0, 5000),
                    fnum($r['avgHr'] ?? 0, 0, 260), (int)fnum($r['maxHr'] ?? 0, 0, 260), fnum($r['avgCadence'] ?? 0, 0, 400),
                    fnum($r['kj'] ?? 0, 0, 1e6), fnum($r['ifValue'] ?? 0, 0, 5), fnum($r['tss'] ?? 0, 0, 5000),
                    json_encode($zones), fnum($r['distanceM'] ?? 0, 0, 1e7), json_encode((object)ride_bests($samples)),
                    gzcompress(json_encode($samples), 6), json_encode($laps),
                    $trace ? json_encode($trace, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) : null,
                    rides_next_rev($pdo),
                ]);
                $results[] = ['uuid' => $uuid, 'taken' => true];
            }
            $pdo->commit();
        } catch (Throwable $e) {
            $pdo->rollBack();
            throw $e;
        }
    }
    return ['results' => $results, 'rev' => rides_rev($pdo)];
}

function fmt_duration(int $s): string
{
    return $s >= 3600 ? sprintf('%d:%02d:%02d', intdiv($s, 3600), intdiv($s % 3600, 60), $s % 60)
                      : sprintf('%d:%02d', intdiv($s, 60), $s % 60);
}

function sport_label(string $sport): string
{
    return ['cycling' => 'Ride', 'rowing' => 'Row', 'running' => 'Run'][$sport] ?? $sport;
}
