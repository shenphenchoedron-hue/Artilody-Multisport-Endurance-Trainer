<?php
// Tests of lib/media.php against a throw-away database (artilody_test) and
// folders in /tmp. On the server:
//   sudo php ~/artilody-server/tests/test_media.php
declare(strict_types=1);

$dir = dirname(__DIR__);
$tmp = sys_get_temp_dir() . '/artilody-media-test-' . bin2hex(random_bytes(4));
mkdir("$tmp/blobs", 0755, true);
mkdir("$tmp/uploads", 0755, true);
$conf = "$tmp/config.php";
file_put_contents($conf, "<?php return ['dsn' => 'mysql:unix_socket=/run/mysqld/mysqld.sock;dbname=artilody_test;charset=utf8mb4',"
    . " 'user' => 'root', 'password' => '', 'blobs_dir' => '$tmp/blobs', 'uploads_dir' => '$tmp/uploads'];");
putenv('ARTILODY_CONFIG=' . $conf);
require $dir . '/lib/bootstrap.php';
require $dir . '/lib/users.php';
require $dir . '/lib/media.php';

$root = new PDO('mysql:unix_socket=/run/mysqld/mysqld.sock;charset=utf8mb4', 'root', '', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
$root->exec('DROP DATABASE IF EXISTS artilody_test');
$root->exec('CREATE DATABASE artilody_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci');
$root->exec('USE artilody_test');
foreach ([1, 2] as $_) {   // (twice: an update never fails)
    artilody_apply_schema($root, file_get_contents($dir . '/schema.sql'));
}

$failures = 0;
$checks = 0;
function check(bool $ok, string $what): void
{
    global $failures, $checks;
    ++$checks;
    if (!$ok) {
        ++$failures;
        echo "FAIL: $what\n";
    }
}

// uploads a string in pieces of $piece bytes: [hash, error]
function upload(string $data, int $piece = 7): array
{
    $e = '';
    $id = upload_start(strlen($data), $e);
    if ($id === '') {
        return ['', $e];
    }
    for ($o = 0; $o < strlen($data); $o += $piece) {
        if (upload_chunk($id, $o, substr($data, $o, $piece), $e) < 0) {
            return ['', $e];
        }
    }
    [$hash] = upload_finish($id, $e);
    return [$hash, $e];
}

$pdo = artilody_db();
check(media_rev($pdo) === 0, 'no media yet');

// an upload in pieces: kept by its sha1
$gpx = '<?xml version="1.0"?><gpx><trk><trkseg><trkpt lat="1" lon="2"/></trkseg></trk></gpx>';
[$h, $e] = upload($gpx);
check($h === sha1($gpx) && $e === '' && file_get_contents(blob_path($h)) === $gpx, 'uploaded in pieces');
check(glob("$tmp/uploads/*") === [], 'the pieces are gone');

// pieces: a wrong offset tells what was received; too much is refused; unfinished is refused
$id = upload_start(10, $e);
check(upload_chunk($id, 0, 'abcd', $e) === 4, 'a piece');
check(upload_chunk($id, 2, 'cdef', $e) === 4 && $e === 'offset', 'a wrong offset: what was received');
check(upload_chunk($id, 4, 'efghijklmn', $e) === -1, 'more than the size: refused');
check(upload_finish($id, $e)[0] === '' && $e !== '', 'unfinished: refused');
check(upload_start(0, $e) === '' && upload_start(MEDIA_MAX_FILE_BYTES + 1, $e) === '', 'sizes refused');
check(upload_chunk('../../etc', 0, 'x', $e) === -1, 'a bad id');

// a GPX route
[$error, $item, $created] = media_add($pdo, ['kind' => 'gpx', 'name' => 'Flad', 'files' => [['name' => '../Flad.gpx', 'hash' => $h]]], 'iMac');
check($error === '' && $created && $item['name'] === 'Flad' && $item['files'][0]['name'] === 'Flad.gpx', 'a GPX route');
check($item['sizeBytes'] === strlen($gpx) && $item['folder'] === '' && $item['rev'] === 1, 'its size and rev');
// the same file again: the same item
[$error, $again, $created] = media_add($pdo, ['kind' => 'gpx', 'name' => 'Other', 'files' => [['name' => 'x.gpx', 'hash' => $h]]], 'Pi');
check($error === '' && !$created && $again['uuid'] === $item['uuid'] && media_rev($pdo) === 1, 'the same file: the same item');

// a video route: video + gpx (+ xml)
[$hv] = upload(str_repeat('V', 100), 33);
[$error] = media_add($pdo, ['kind' => 'video', 'name' => 'Alpe', 'files' => [['name' => 'Alpe.mp4', 'hash' => $hv]]], 'web');
check($error !== '', 'a video without its gpx: refused');
[$error, $video, $created] = media_add($pdo, ['kind' => 'video', 'name' => 'Alpe d\'Huez', 'folder' => 'Alpe/Huez',
    'files' => [['name' => 'Alpe.mp4', 'hash' => $hv], ['name' => 'Alpe.gpx', 'hash' => $h]]], 'web');
check($error === '' && $created && $video['folder'] === 'Huez' && count($video['files']) === 2, 'a video route (folder cleaned)');
[$error] = media_add($pdo, ['kind' => 'gpx', 'files' => [['name' => 'a.exe', 'hash' => $h]]], 'web');
check($error !== '', 'other files refused');
[$error] = media_add($pdo, ['kind' => 'gpx', 'files' => [['name' => 'a.gpx', 'hash' => str_repeat('0', 40)]]], 'web');
check($error !== '', 'a file not uploaded: refused');

// pulled by rev
check(count(media_pull($pdo, 0)) === 2 && count(media_pull($pdo, 1)) === 1, 'pulled by rev');

// rename, delete: a tombstone; a file still used by another item stays
check(media_rename($pdo, $item['uuid'], 'Flat route') && media_get($pdo, $item['uuid'])['name'] === 'Flat route', 'renamed');
check(media_delete($pdo, $video['uuid']), 'deleted');
$t = media_get($pdo, $video['uuid']);
check($t['deleted'] && $t['files'] === [], 'a tombstone');
check(!is_file(blob_path($hv)) && is_file(blob_path($h)), 'its video gone, the shared gpx kept');
$pulled = media_pull($pdo, media_rev($pdo) - 1);
check(count($pulled) === 1 && $pulled[0]['deleted'], 'the tombstone is pulled');
check(!media_delete($pdo, $video['uuid']), 'deleted twice: no');

$root->exec('DROP DATABASE artilody_test');
exec('rm -rf ' . escapeshellarg($tmp));
echo $failures === 0 ? "OK: $checks checks\n" : "$failures of $checks checks FAILED\n";
exit($failures === 0 ? 0 : 1);
