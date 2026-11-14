<?php
// The media library (docs/server-plan.md, step 4): GPX routes and video
// routes, uploaded from the web interface or a station and fetched by the
// stations. Used by api/upload.php, api/media.php and admin/media.php.
//
// Files are kept by their sha1 in blobs_dir() (Apache serves them at
// /blobs/<sha1>, with ranges: an interrupted download resumes). They arrive
// in pieces (upload_start / upload_chunk / upload_finish), so a video of
// several GB never has to fit in one request.
//
// Workouts (.awt / .json / .zwo) and training plans (.awplan) are items
// of one file too: the stations import workouts into their library by
// themselves, a plan when asked (onto a start date).
//
// An item as exchanged with the stations (JSON):
//   { "uuid": "...", "kind": "gpx" | "video" | "workout" | "plan", "name": "Menglonnais",
//     "folder": "Menglonnais" (video: its folder on the stations, gpx: ""),
//     "files": [ { "name": "Menglonnais.mp4", "hash": "<sha1>", "size": 123 }, ... ],
//     "sizeBytes": 123, "deleted": false, "updatedMs": ..., "rev": 7 }
declare(strict_types=1);

const MEDIA_MAX_FILE_BYTES = 20 * 1024 * 1024 * 1024;   // one file: 20 GB
const MEDIA_CHUNK_MAX_BYTES = 8 * 1024 * 1024;          // one piece of an upload (php: post_max_size 16M)
const MEDIA_DISK_RESERVE_BYTES = 2 * 1024 * 1024 * 1024; // kept free on the server's disk
const MEDIA_VIDEO_EXTENSIONS = ['mp4', 'm4v', 'mov', 'mkv', 'avi', 'webm'];   // (the app's RlvRoute::videoSuffixes(): tst_mediasync checks)
const MEDIA_UPLOAD_TTL_SEC = 24 * 3600;                 // an unfinished upload is removed after a day
// workouts: the app's import formats (workoutio::parseFile); training plans: the app's .awplan
const MEDIA_WORKOUT_EXTENSIONS = ['awt', 'json', 'zwo'];
const MEDIA_PLAN_EXTENSIONS = ['awplan'];
const MEDIA_KINDS = ['gpx', 'video', 'workout', 'plan'];

function blobs_dir(): string
{
    return rtrim((string)(artilody_config()['blobs_dir'] ?? '/var/lib/artilody/blobs'), '/');
}

function uploads_dir(): string
{
    return rtrim((string)(artilody_config()['uploads_dir'] ?? '/var/lib/artilody/uploads'), '/');
}

function is_sha1(string $s): bool
{
    return preg_match('/^[0-9a-f]{40}$/', $s) === 1;
}

function blob_path(string $hash): string
{
    return blobs_dir() . '/' . $hash;
}

function blob_size(string $hash): int
{
    $p = blob_path($hash);
    return is_sha1($hash) && is_file($p) ? (int)filesize($p) : -1;
}

function media_rev(PDO $pdo): int
{
    return (int)$pdo->query("SELECT v FROM counters WHERE k = 'media'")->fetchColumn();
}

function media_next_rev(PDO $pdo): int
{
    $pdo->exec("UPDATE counters SET v = LAST_INSERT_ID(v + 1) WHERE k = 'media'");
    return (int)$pdo->query('SELECT LAST_INSERT_ID()')->fetchColumn();
}

// --- uploads in pieces ------------------------------------------------------

function upload_paths(string $id): array
{
    return [uploads_dir() . "/$id.part", uploads_dir() . "/$id.json"];
}

function upload_valid_id(string $id): bool
{
    return preg_match('/^[0-9a-f]{32}$/', $id) === 1;
}

// A new upload of `size` bytes: its id, or '' with $error set.
function upload_start(int $size, string &$error): string
{
    $error = '';
    // (unfinished ones of more than a day ago go)
    foreach (glob(uploads_dir() . '/*.{part,json}', GLOB_BRACE) ?: [] as $old) {
        if (filemtime($old) < time() - MEDIA_UPLOAD_TTL_SEC) {
            @unlink($old);
        }
    }
    if ($size <= 0 || $size > MEDIA_MAX_FILE_BYTES) {
        $error = 'a file must be 1 byte to ' . (MEDIA_MAX_FILE_BYTES >> 30) . ' GB';
        return '';
    }
    $free = @disk_free_space(blobs_dir());
    if ($free !== false && $free < $size * 2 + MEDIA_DISK_RESERVE_BYTES) {   // (the piece and the blob)
        $error = 'not enough room on the server (' . round($free / 1e9, 1) . ' GB free)';
        return '';
    }
    $id = bin2hex(random_bytes(16));
    [$part, $meta] = upload_paths($id);
    if (@file_put_contents($part, '') === false || @file_put_contents($meta, json_encode(['size' => $size])) === false) {
        $error = 'the server cannot store uploads (' . uploads_dir() . ')';
        return '';
    }
    return $id;
}

// Appends a piece at `offset`: the bytes received so far, or -1 with $error
// (an offset that does not match: the client goes on from what was received).
function upload_chunk(string $id, int $offset, string $data, string &$error): int
{
    $error = '';
    [$part, $meta] = upload_paths($id);
    if (!upload_valid_id($id) || !is_file($part) || !is_file($meta)) {
        $error = 'no such upload';
        return -1;
    }
    $size = (int)(json_decode((string)file_get_contents($meta), true)['size'] ?? 0);
    $f = fopen($part, 'ab');
    if (!$f || !flock($f, LOCK_EX)) {
        $error = 'the upload cannot be written';
        return -1;
    }
    clearstatcache(true, $part);
    $have = (int)filesize($part);
    if ($offset !== $have) {
        flock($f, LOCK_UN);
        fclose($f);
        $error = 'offset';
        return $have;
    }
    if ($data === '' || strlen($data) > MEDIA_CHUNK_MAX_BYTES || $have + strlen($data) > $size) {
        flock($f, LOCK_UN);
        fclose($f);
        $error = 'a piece of a wrong size';
        return -1;
    }
    $written = fwrite($f, $data);
    fflush($f);
    flock($f, LOCK_UN);
    fclose($f);
    touch($meta);
    if ($written !== strlen($data)) {
        $error = 'the server\'s disk is full';
        return -1;
    }
    return $have + $written;
}

// All received: kept as a blob. [hash, size], or ['', 0] with $error.
function upload_finish(string $id, string &$error): array
{
    $error = '';
    [$part, $meta] = upload_paths($id);
    if (!upload_valid_id($id) || !is_file($part) || !is_file($meta)) {
        $error = 'no such upload';
        return ['', 0];
    }
    $size = (int)(json_decode((string)file_get_contents($meta), true)['size'] ?? 0);
    clearstatcache(true, $part);
    if ((int)filesize($part) !== $size) {
        $error = 'the upload is not complete';
        return ['', 0];
    }
    set_time_limit(900);   // (sha1 of several GB)
    $hash = sha1_file($part);
    if ($hash === false || (!is_file(blob_path($hash)) && !@rename($part, blob_path($hash)))) {
        $error = 'the upload cannot be stored';
        return ['', 0];
    }
    @unlink($part);   // (already there: the same file)
    @unlink($meta);
    @chmod(blob_path($hash), 0644);
    return [$hash, $size];
}

// --- items --------------------------------------------------------------------

// A file name as it will be on the stations: no folders, no leading dot, no control characters.
function media_clean_filename(string $name): string
{
    $name = basename(str_replace('\\', '/', trim($name)));
    $name = preg_replace('/[\x00-\x1f\x7f\/:*?"<>|]/u', '_', $name);
    $name = ltrim((string)$name, '. ');
    return mb_substr($name, 0, 200);
}

function media_ext(string $name): string
{
    return strtolower(pathinfo($name, PATHINFO_EXTENSION));
}

// Checks an item's kind and files: the error ('' when fine) and the cleaned files.
function media_validate(string $kind, array $files): array
{
    $clean = [];
    $count = ['gpx' => 0, 'xml' => 0, 'video' => 0, 'workout' => 0, 'plan' => 0];
    foreach ($files as $f) {
        $name = media_clean_filename((string)($f['name'] ?? ''));
        $hash = strtolower((string)($f['hash'] ?? ''));
        $ext = media_ext($name);
        if ($name === '' || !is_sha1($hash)) {
            return ['a file without a name or hash', []];
        }
        if (blob_size($hash) < 0) {
            return ["$name is not uploaded", []];
        }
        $type = $ext === 'gpx' ? 'gpx' : ($ext === 'xml' ? 'xml' : (in_array($ext, MEDIA_VIDEO_EXTENSIONS, true) ? 'video'
            : (in_array($ext, MEDIA_WORKOUT_EXTENSIONS, true) ? 'workout' : (in_array($ext, MEDIA_PLAN_EXTENSIONS, true) ? 'plan' : ''))));
        if ($type === '') {
            return ["$name: only .gpx, .xml, videos (" . implode(', ', MEDIA_VIDEO_EXTENSIONS) . '), workouts ('
                . implode(', ', MEDIA_WORKOUT_EXTENSIONS) . ') and plans (.awplan)', []];
        }
        ++$count[$type];
        foreach ($clean as $c) {
            if (strcasecmp($c['name'], $name) === 0) {
                return ["$name twice", []];
            }
        }
        $clean[] = ['name' => $name, 'hash' => $hash, 'size' => blob_size($hash)];
    }
    if (!in_array($kind, MEDIA_KINDS, true)) {
        return ['kind must be ' . implode(', ', MEDIA_KINDS), []];
    }
    if ($kind === 'gpx' && !(count($clean) === 1 && $count['gpx'] === 1)) {
        return ['a GPX route is one .gpx file', []];
    }
    if ($kind === 'video' && !($count['video'] === 1 && $count['gpx'] === 1 && $count['xml'] <= 1
            && count($clean) === 2 + $count['xml'])) {
        return ['a video route is one video and one .gpx (and optionally one .xml)', []];
    }
    if ($kind === 'workout' && !(count($clean) === 1 && $count['workout'] === 1)) {
        return ['a workout is one file (' . implode(', ', MEDIA_WORKOUT_EXTENSIONS) . ')', []];
    }
    if ($kind === 'plan' && !(count($clean) === 1 && $count['plan'] === 1)) {
        return ['a training plan is one .awplan file', []];
    }
    return ['', $clean];
}

function media_from_row(PDO $pdo, array $r): array
{
    $q = $pdo->prepare('SELECT filename, hash, size_bytes FROM media_files WHERE media_uuid = ? ORDER BY filename');
    $q->execute([$r['uuid']]);
    $files = array_map(fn($f) => ['name' => $f['filename'], 'hash' => $f['hash'], 'size' => (int)$f['size_bytes']], $q->fetchAll());
    return [
        'uuid' => $r['uuid'],
        'kind' => $r['kind'],
        'name' => $r['name'],
        'folder' => $r['folder'],
        'files' => $files,
        'sizeBytes' => (int)$r['size_bytes'],
        'deleted' => (bool)$r['deleted'],
        'updatedMs' => (int)$r['updated_ms'],
        'uploadedBy' => $r['uploaded_by'],
        'rev' => (int)$r['rev'],
    ];
}

function media_get(PDO $pdo, string $uuid): ?array
{
    $q = $pdo->prepare('SELECT * FROM media WHERE uuid = ?');
    $q->execute([$uuid]);
    $r = $q->fetch();
    return $r ? media_from_row($pdo, $r) : null;
}

function media_pull(PDO $pdo, int $since): array
{
    $q = $pdo->prepare('SELECT * FROM media WHERE rev > ? ORDER BY rev');
    $q->execute([$since]);
    return array_map(fn($r) => media_from_row($pdo, $r), $q->fetchAll());
}

// A new item: { "kind", "name", "folder", "files": [{name, hash}] } - its files
// uploaded first. The same files already there (not deleted): that item.
// Returns [error, item, created].
function media_add(PDO $pdo, array $in, string $by): array
{
    $kind = (string)($in['kind'] ?? '');
    [$error, $files] = media_validate($kind, is_array($in['files'] ?? null) ? $in['files'] : []);
    if ($error !== '') {
        return [$error, null, false];
    }
    $name = mb_substr(trim((string)($in['name'] ?? '')), 0, 200);
    if ($name === '') {
        $name = pathinfo($files[0]['name'], PATHINFO_FILENAME);
    }
    $folder = $kind === 'video' ? media_clean_filename((string)($in['folder'] ?? '') ?: $name) : '';
    if ($kind === 'video' && $folder === '') {
        return ['a video route needs a folder name', null, false];
    }
    // the same files already there: that one
    $hashes = array_column($files, 'hash');
    sort($hashes);
    $q = $pdo->prepare('SELECT m.uuid, GROUP_CONCAT(f.hash ORDER BY f.hash) AS hashes FROM media m'
        . ' JOIN media_files f ON f.media_uuid = m.uuid WHERE m.deleted = 0 AND m.kind = ? GROUP BY m.uuid');
    $q->execute([$kind]);
    foreach ($q->fetchAll() as $r) {
        if ($r['hashes'] === implode(',', $hashes)) {
            return ['', media_get($pdo, $r['uuid']), false];
        }
    }
    $uuid = new_uuid();
    $pdo->beginTransaction();
    try {
        $rev = media_next_rev($pdo);
        $pdo->prepare('INSERT INTO media (uuid, kind, name, folder, size_bytes, deleted, updated_ms, uploaded_by, rev, created_at)'
            . ' VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?, UTC_TIMESTAMP())')
            ->execute([$uuid, $kind, $name, $folder, array_sum(array_column($files, 'size')), now_ms(), mb_substr($by, 0, 100), $rev]);
        $f = $pdo->prepare('INSERT INTO media_files (media_uuid, filename, hash, size_bytes) VALUES (?, ?, ?, ?)');
        foreach ($files as $file) {
            $f->execute([$uuid, $file['name'], $file['hash'], $file['size']]);
        }
        $pdo->commit();
    } catch (Throwable $e) {
        $pdo->rollBack();
        throw $e;
    }
    return ['', media_get($pdo, $uuid), true];
}

function media_rename(PDO $pdo, string $uuid, string $name): bool
{
    $name = mb_substr(trim($name), 0, 200);
    if ($name === '') {
        return false;
    }
    $pdo->beginTransaction();
    $rev = media_next_rev($pdo);
    $q = $pdo->prepare('UPDATE media SET name = ?, rev = ?, updated_ms = ? WHERE uuid = ? AND deleted = 0');
    $q->execute([$name, $rev, now_ms(), $uuid]);
    $ok = $q->rowCount() > 0;
    $ok ? $pdo->commit() : $pdo->rollBack();
    return $ok;
}

// Deleted: a tombstone (the stations keep their copies), and the files no
// other item uses are removed from the disk.
function media_delete(PDO $pdo, string $uuid): bool
{
    $pdo->beginTransaction();
    $rev = media_next_rev($pdo);
    $q = $pdo->prepare('UPDATE media SET deleted = 1, rev = ?, updated_ms = ? WHERE uuid = ? AND deleted = 0');
    $q->execute([$rev, now_ms(), $uuid]);
    if ($q->rowCount() === 0) {
        $pdo->rollBack();
        return false;
    }
    $h = $pdo->prepare('SELECT hash FROM media_files WHERE media_uuid = ?');
    $h->execute([$uuid]);
    $hashes = $h->fetchAll(PDO::FETCH_COLUMN);
    $pdo->prepare('DELETE FROM media_files WHERE media_uuid = ?')->execute([$uuid]);
    $pdo->commit();
    $used = $pdo->prepare('SELECT 1 FROM media_files WHERE hash = ? LIMIT 1');
    foreach ($hashes as $hash) {
        $used->execute([$hash]);
        if (!$used->fetchColumn() && is_sha1($hash)) {
            @unlink(blob_path($hash));
        }
    }
    return true;
}

function human_size(int $bytes): string
{
    if ($bytes >= 1e9) {
        return number_format($bytes / 1e9, 1) . ' GB';
    }
    if ($bytes >= 1e6) {
        return number_format($bytes / 1e6, 1) . ' MB';
    }
    return max(1, (int)round($bytes / 1e3)) . ' kB';
}
