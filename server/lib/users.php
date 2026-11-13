<?php
// The users (docs/server-plan.md, step 3): the stations' profiles, kept here
// and changed on the stations and in the web interface - the newest change
// wins. Used by api/users.php, api/picture.php and the web interface.
//
// A user as exchanged with the stations (JSON):
//   { "uuid": "...", "name": "Lone", "weightKg": 68, "heightCm": 0, "ftp": 110,
//     "rowingFtp": 150, "maxHr": 185, "restHr": 60, "cadence": 85,
//     "defaultErg": true, "zones": [0.55, 0.75, 0.87, 0.94, 1.05],
//     "avatar3d": {...}, "runner3d": {...}, "rower3d": {...},
//     "pictureHash": "sha1" | null, "deleted": false,
//     "updatedMs": 1763000000000, "rev": 12 }
// The limits match the app's Profile::isValid().
declare(strict_types=1);

const PICTURE_MAX_BYTES = 4 * 1024 * 1024;
const PICTURE_TYPES = ['image/jpeg', 'image/png', 'image/webp', 'image/gif'];
const AVATAR_COLOR_KEYS = ['jersey', 'shorts', 'helmet', 'shoes', 'hair', 'bike', 'boat'];
const DEFAULT_ZONES = [0.55, 0.75, 0.87, 0.94, 1.05];

function server_id(PDO $pdo): string
{
    $q = $pdo->query("SELECT v FROM meta WHERE k = 'server_id'");
    return (string)($q->fetchColumn() ?: '');
}

function users_rev(PDO $pdo): int
{
    return (int)$pdo->query("SELECT v FROM counters WHERE k = 'users'")->fetchColumn();
}

// The next change number (inside the caller's transaction).
function users_next_rev(PDO $pdo): int
{
    $pdo->exec("UPDATE counters SET v = LAST_INSERT_ID(v + 1) WHERE k = 'users'");
    return (int)$pdo->query('SELECT LAST_INSERT_ID()')->fetchColumn();
}

// Only the known keys of a 3D avatar: model "male" / "female", "#rrggbb" colours ("" = the model's own).
function avatar_clean($v): array
{
    $v = is_array($v) ? $v : [];
    $out = ['model' => strtolower(trim((string)($v['model'] ?? ''))) === 'female' ? 'female' : 'male'];
    foreach (AVATAR_COLOR_KEYS as $k) {
        $c = strtolower(trim((string)($v[$k] ?? '')));
        $out[$k] = preg_match('/^#[0-9a-f]{6}$/', $c) ? $c : '';
    }
    return $out;
}

function zones_clean($v): array
{
    if (!is_array($v) || count($v) !== 5) {
        return DEFAULT_ZONES;
    }
    $out = [];
    $prev = 0.0;
    foreach (array_values($v) as $z) {
        $z = is_numeric($z) ? (float)$z : 0.0;
        if ($z <= $prev || $z > 3) {
            return DEFAULT_ZONES;
        }
        $out[] = round($z, 4);
        $prev = $z;
    }
    return $out;
}

function num_in($v, float $min, float $max, float $default): float
{
    if (!is_numeric($v)) {
        return $default;
    }
    $v = (float)$v;
    return ($v >= $min && $v <= $max) ? $v : $default;
}

// A user's values checked: the error ('' when fine) and the cleaned values.
// (Like Profile::isValid(): a station never sends an invalid one; the web
// form shows the error.)
function user_validate(array $u): array
{
    $c = [
        'name' => mb_substr(trim((string)($u['name'] ?? '')), 0, 100),
        'weightKg' => is_numeric($u['weightKg'] ?? null) ? round((float)$u['weightKg'], 1) : 0.0,
        'heightCm' => is_numeric($u['heightCm'] ?? null) ? round((float)$u['heightCm'], 1) : 0.0,
        'ftp' => (int)($u['ftp'] ?? 0),
        'rowingFtp' => (int)($u['rowingFtp'] ?? 0),
        'maxHr' => (int)($u['maxHr'] ?? 0),
        'restHr' => (int)($u['restHr'] ?? 0),
        'cadence' => (int)($u['cadence'] ?? 0),
        'defaultErg' => !empty($u['defaultErg']),
        'zones' => zones_clean($u['zones'] ?? null),
        'avatar3d' => avatar_clean($u['avatar3d'] ?? null),
        'runner3d' => avatar_clean($u['runner3d'] ?? null),
        'rower3d' => avatar_clean($u['rower3d'] ?? null),
    ];
    $error = '';
    if ($c['name'] === '') {
        $error = 'Name must not be empty';
    } elseif (preg_match('/[<>\x00-\x1f]/u', $c['name'])) {
        $error = 'Name must not contain < or >';
    } elseif ($c['weightKg'] < 20 || $c['weightKg'] > 250) {
        $error = 'Weight must be between 20 and 250 kg';
    } elseif ($c['heightCm'] != 0 && ($c['heightCm'] < 100 || $c['heightCm'] > 230)) {
        $error = 'Height must be between 100 and 230 cm (or 0 = not set)';
    } elseif ($c['ftp'] < 50 || $c['ftp'] > 600) {
        $error = 'FTP must be between 50 and 600 W';
    } elseif ($c['rowingFtp'] < 50 || $c['rowingFtp'] > 600) {
        $error = 'Rowing FTP must be between 50 and 600 W';
    } elseif ($c['restHr'] < 30 || $c['restHr'] > 120) {
        $error = 'Resting heart rate must be between 30 and 120 BPM';
    } elseif ($c['maxHr'] <= $c['restHr'] + 20 || $c['maxHr'] > 240) {
        $error = 'Max heart rate must be above resting heart rate';
    } elseif ($c['cadence'] < 40 || $c['cadence'] > 150) {
        $error = 'Cadence must be between 40 and 150 RPM';
    }
    return [$error, $c];
}

function user_from_row(array $r): array
{
    return [
        'uuid' => $r['uuid'],
        'name' => $r['name'],
        'weightKg' => (float)$r['weight_kg'],
        'heightCm' => (float)$r['height_cm'],
        'ftp' => (int)$r['ftp'],
        'rowingFtp' => (int)$r['rowing_ftp'],
        'maxHr' => (int)$r['max_hr'],
        'restHr' => (int)$r['rest_hr'],
        'cadence' => (int)$r['cadence'],
        'defaultErg' => (bool)$r['default_erg'],
        'zones' => zones_clean(json_decode((string)$r['zones'], true)),
        'avatar3d' => avatar_clean(json_decode((string)$r['avatar3d'], true)),
        'runner3d' => avatar_clean(json_decode((string)$r['runner3d'], true)),
        'rower3d' => avatar_clean(json_decode((string)$r['rower3d'], true)),
        'pictureHash' => $r['picture_hash'],
        'deleted' => (bool)$r['deleted'],
        'mergedInto' => $r['merged_into'] ?? null,
        'updatedMs' => (int)$r['updated_ms'],
        'updatedBy' => $r['updated_by'],
        'rev' => (int)$r['rev'],
    ];
}

function user_get(PDO $pdo, string $uuid): ?array
{
    $q = $pdo->prepare('SELECT * FROM users WHERE uuid = ?');
    $q->execute([$uuid]);
    $r = $q->fetch();
    return $r ? user_from_row($r) : null;
}

// The users (deleted ones as tombstones) changed after `since`, oldest first.
function users_pull(PDO $pdo, int $since): array
{
    $q = $pdo->prepare('SELECT * FROM users WHERE rev > ? ORDER BY rev');
    $q->execute([$since]);
    return array_map('user_from_row', $q->fetchAll());
}

// Writes a user (already cleaned) as the newest version: a new rev. Inside a transaction.
function user_write(PDO $pdo, string $uuid, array $c, ?string $pictureHash, bool $deleted, int $updatedMs, string $by): void
{
    $rev = users_next_rev($pdo);
    $q = $pdo->prepare(
        'INSERT INTO users (uuid, name, weight_kg, height_cm, ftp, rowing_ftp, max_hr, rest_hr, cadence, default_erg,'
        . ' zones, avatar3d, runner3d, rower3d, picture_hash, deleted, updated_ms, updated_by, rev, created_at)'
        . ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, UTC_TIMESTAMP())'
        . ' ON DUPLICATE KEY UPDATE name = VALUES(name), weight_kg = VALUES(weight_kg), height_cm = VALUES(height_cm),'
        . ' ftp = VALUES(ftp), rowing_ftp = VALUES(rowing_ftp), max_hr = VALUES(max_hr), rest_hr = VALUES(rest_hr),'
        . ' cadence = VALUES(cadence), default_erg = VALUES(default_erg), zones = VALUES(zones),'
        . ' avatar3d = VALUES(avatar3d), runner3d = VALUES(runner3d), rower3d = VALUES(rower3d),'
        . ' picture_hash = VALUES(picture_hash), deleted = VALUES(deleted), updated_ms = VALUES(updated_ms),'
        . ' updated_by = VALUES(updated_by), rev = VALUES(rev)'
    );
    $json = fn($v) => json_encode($v, JSON_UNESCAPED_SLASHES);
    $q->execute([
        $uuid, $c['name'], $c['weightKg'], $c['heightCm'], $c['ftp'], $c['rowingFtp'], $c['maxHr'], $c['restHr'],
        $c['cadence'], $c['defaultErg'] ? 1 : 0, $json($c['zones']), $json($c['avatar3d']), $json($c['runner3d']),
        $json($c['rower3d']), $pictureHash, $deleted ? 1 : 0, $updatedMs, mb_substr($by, 0, 100), $rev,
    ]);
}

// The live user of this name (another uuid), case and spaces ignored - the
// same rider made on two stations; null when none.
function user_named(PDO $pdo, string $name, string $notUuid): ?array
{
    $q = $pdo->prepare('SELECT * FROM users WHERE deleted = 0 AND uuid <> ? AND LOWER(TRIM(name)) = LOWER(TRIM(?))'
        . ' ORDER BY created_at, uuid LIMIT 1');
    $q->execute([$notUuid, $name]);
    $r = $q->fetch();
    return $r ? user_from_row($r) : null;
}

// $dup merged into $into (inside a transaction): a tombstone naming $into,
// its rides moved there (a new rev each: the stations fetch them again).
function user_merge(PDO $pdo, string $dup, string $into, string $name, string $by): void
{
    $rev = users_next_rev($pdo);
    $pdo->prepare('INSERT INTO users (uuid, name, zones, avatar3d, runner3d, rower3d, deleted, merged_into, updated_ms,'
        . ' updated_by, rev, created_at) VALUES (?, ?, \'[]\', \'{}\', \'{}\', \'{}\', 1, ?, ?, ?, ?, UTC_TIMESTAMP())'
        . ' ON DUPLICATE KEY UPDATE deleted = 1, merged_into = VALUES(merged_into), picture_hash = NULL,'
        . ' updated_ms = VALUES(updated_ms), updated_by = VALUES(updated_by), rev = VALUES(rev)')
        ->execute([$dup, mb_substr($name, 0, 100), $into, now_ms(), mb_substr($by, 0, 100), $rev]);
    // its rides belong to the one kept (each a new rev: the analysis and the stations follow)
    $q = $pdo->prepare('SELECT uuid FROM rides WHERE user_uuid = ?');
    $q->execute([$dup]);
    $move = $pdo->prepare('UPDATE rides SET user_uuid = ?, rev = ? WHERE uuid = ?');
    foreach ($q->fetchAll(PDO::FETCH_COLUMN) as $ride) {
        $pdo->exec("UPDATE counters SET v = LAST_INSERT_ID(v + 1) WHERE k = 'rides'");
        $move->execute([$into, (int)$pdo->query('SELECT LAST_INSERT_ID()')->fetchColumn(), $ride]);
    }
}

// The same rider twice (made on two stations before they met the server, or
// deleted by hand while a station still has it with rides): every other user
// of the same name - live, or deleted but not merged - is merged into the
// oldest live one. Cheap (a few rows); run before the stations look.
function users_repair_duplicates(PDO $pdo): int
{
    $n = 0;
    $rows = $pdo->query('SELECT uuid, name, deleted, merged_into FROM users ORDER BY created_at, uuid')->fetchAll();
    $keep = [];
    foreach ($rows as $r) {
        if (!$r['deleted']) {
            $keep[mb_strtolower(trim($r['name']))] ??= $r['uuid'];
        }
    }
    foreach ($rows as $r) {
        $key = mb_strtolower(trim($r['name']));
        $into = $keep[$key] ?? null;
        if ($into === null || $into === $r['uuid'] || $r['merged_into'] !== null || trim($r['name']) === '') {
            continue;
        }
        $pdo->beginTransaction();
        user_merge($pdo, $r['uuid'], $into, $r['name'], 'server');
        $pdo->commit();
        ++$n;
    }
    return $n;
}

function picture_exists(PDO $pdo, string $hash): bool
{
    $q = $pdo->prepare('SELECT 1 FROM pictures WHERE hash = ?');
    $q->execute([$hash]);
    return (bool)$q->fetchColumn();
}

// A station's changes: { "station": "<uuid>", "name": "<host>", "nowMs": <its clock>,
//   "users": [ user, ... ] } (a user with "deleted": true: deleted on the station).
// Each is kept when it is newer than the server's (its time moved to the
// server's clock: + server now - station now). Returns, per uuid, whether it
// was taken (not taken: "user", the server's newer version, when it has one),
// and the users' rev afterwards.
function users_push(PDO $pdo, array $payload): array
{
    $now = now_ms();
    $skew = isset($payload['nowMs']) && is_numeric($payload['nowMs']) ? $now - (int)$payload['nowMs'] : 0;
    $by = mb_substr(trim((string)($payload['name'] ?? '')), 0, 90) ?: 'station';
    $results = [];
    foreach (is_array($payload['users'] ?? null) ? $payload['users'] : [] as $u) {
        $uuid = strtolower((string)($u['uuid'] ?? ''));
        if (!is_array($u) || !is_uuid($uuid)) {
            continue;
        }
        $deleted = !empty($u['deleted']);
        // (a station's time in the future: at most now)
        $updated = min($now, (int)($u['updatedMs'] ?? 0) + $skew);
        $pdo->beginTransaction();
        try {
            $q = $pdo->prepare('SELECT * FROM users WHERE uuid = ? FOR UPDATE');
            $q->execute([$uuid]);
            $row = $q->fetch();
            // the same rider as one already here (another station made it): merged into that one
            $twin = !$row && !$deleted ? user_named($pdo, (string)($u['name'] ?? ''), $uuid) : null;
            if ($twin) {
                user_merge($pdo, $uuid, $twin['uuid'], (string)$u['name'], $by);
                $pdo->commit();
                $results[] = ['uuid' => $uuid, 'taken' => false, 'mergedInto' => $twin['uuid'], 'user' => $twin];
                continue;
            }
            if ($row && $row['merged_into']) {   // (merged before: the station moves to the one kept)
                $pdo->commit();
                $results[] = ['uuid' => $uuid, 'taken' => false, 'mergedInto' => $row['merged_into'],
                    'user' => user_get($pdo, $row['merged_into'])];
                continue;
            }
            $take = !$row || $updated > (int)$row['updated_ms'];
            $error = '';
            if ($take && $deleted) {
                if ($row) {   // (a tombstone keeps the last values)
                    [, $c] = user_validate(user_from_row($row));
                    user_write($pdo, $uuid, $c, null, true, $updated, $by);
                } else {
                    $take = false;   // (never known here: nothing to delete)
                }
            } elseif ($take) {
                [$error, $c] = user_validate($u);
                $hash = isset($u['pictureHash']) && is_string($u['pictureHash'])
                    && preg_match('/^[0-9a-f]{40}$/', $u['pictureHash']) ? $u['pictureHash'] : null;
                if ($hash !== null && !picture_exists($pdo, $hash)) {
                    $error = 'picture not uploaded';
                }
                if ($error === '') {
                    user_write($pdo, $uuid, $c, $hash, false, $updated, $by);
                } else {
                    $take = false;
                }
            }
            $pdo->commit();
        } catch (Throwable $e) {
            $pdo->rollBack();
            throw $e;
        }
        // not taken: the server's own version, for the station to take instead
        $current = !$take && $row ? ['user' => user_get($pdo, $uuid)] : [];
        $results[] = ['uuid' => $uuid, 'taken' => $take] + ($error !== '' ? ['error' => $error] : []) + $current;
    }
    return ['results' => $results, 'rev' => users_rev($pdo)];
}

// Stores a picture (raw bytes); its sha1, or '' with $error set.
function picture_store(PDO $pdo, string $data, string &$error): string
{
    $error = '';
    if ($data === '' || strlen($data) > PICTURE_MAX_BYTES) {
        $error = 'the picture must be 1 byte to ' . (PICTURE_MAX_BYTES >> 20) . ' MB';
        return '';
    }
    $type = (new finfo(FILEINFO_MIME_TYPE))->buffer($data) ?: '';
    if (!in_array($type, PICTURE_TYPES, true)) {
        $error = 'not a JPEG, PNG, WebP or GIF picture';
        return '';
    }
    $hash = sha1($data);
    $q = $pdo->prepare('INSERT IGNORE INTO pictures (hash, type, data, created_at) VALUES (?, ?, ?, UTC_TIMESTAMP())');
    $q->bindValue(1, $hash);
    $q->bindValue(2, $type);
    $q->bindValue(3, $data, PDO::PARAM_LOB);
    $q->execute();
    // pictures no user shows any more (older than a day: one being sent with a user may be new)
    $pdo->exec('DELETE FROM pictures WHERE created_at < UTC_TIMESTAMP() - INTERVAL 1 DAY'
        . ' AND hash NOT IN (SELECT picture_hash FROM users WHERE picture_hash IS NOT NULL)');
    return $hash;
}

function picture_get(PDO $pdo, string $hash): ?array
{
    $q = $pdo->prepare('SELECT type, data FROM pictures WHERE hash = ?');
    $q->execute([$hash]);
    $r = $q->fetch();
    return $r ?: null;
}
