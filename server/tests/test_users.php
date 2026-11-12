<?php
// Tests of lib/users.php against a throw-away database (artilody_test).
// On the server (as root: MariaDB lets root in by its socket):
//   sudo php ~/artilody-server/tests/test_users.php
declare(strict_types=1);

$dir = dirname(__DIR__);
$conf = tempnam(sys_get_temp_dir(), 'artilody-test');
file_put_contents($conf, "<?php return ['dsn' => 'mysql:unix_socket=/run/mysqld/mysqld.sock;dbname=artilody_test;charset=utf8mb4',"
    . " 'user' => 'root', 'password' => ''];");
putenv('ARTILODY_CONFIG=' . $conf);
require $dir . '/lib/bootstrap.php';
require $dir . '/lib/users.php';

$root = new PDO('mysql:unix_socket=/run/mysqld/mysqld.sock;charset=utf8mb4', 'root', '',
    [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
$root->exec('DROP DATABASE IF EXISTS artilody_test');
$root->exec('CREATE DATABASE artilody_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci');
$root->exec('USE artilody_test');
foreach (array_filter(array_map('trim', explode(";\n", preg_replace('/^--.*$/m', '', file_get_contents($dir . '/schema.sql'))))) as $sql) {
    $root->exec($sql);
}
// (the schema applied twice: an update never fails)
foreach (array_filter(array_map('trim', explode(";\n", preg_replace('/^--.*$/m', '', file_get_contents($dir . '/schema.sql'))))) as $sql) {
    $root->exec($sql);
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

$pdo = artilody_db();
$a = 'c11fffc6-cc95-421a-a39d-b65743745edc';
$b = 'f51db3d3-7c56-4aec-bc8d-720622dcdd2a';
$lone = ['uuid' => $a, 'name' => 'Lone', 'weightKg' => 68, 'heightCm' => 0, 'ftp' => 110, 'rowingFtp' => 150,
    'maxHr' => 185, 'restHr' => 60, 'cadence' => 85, 'defaultErg' => true, 'zones' => [0.55, 0.75, 0.87, 0.94, 1.05],
    'avatar3d' => ['model' => 'female', 'jersey' => '#FF0000', 'evil' => '<script>'], 'runner3d' => [], 'rower3d' => [],
    'pictureHash' => null, 'deleted' => false];
$now = now_ms();

check(server_id($pdo) !== '' && is_uuid(server_id($pdo)), 'a server id');
check(users_rev($pdo) === 0, 'no users yet');

// a station sends its profiles
$r = users_push($pdo, ['station' => $b, 'name' => 'iMac', 'nowMs' => $now, 'users' => [$lone + ['updatedMs' => $now - 1000]]]);
check($r['results'][0]['taken'] === true && $r['rev'] === 1, 'a new user is taken');
$u = user_get($pdo, $a);
check($u['name'] === 'Lone' && $u['ftp'] === 110 && $u['updatedBy'] === 'iMac', 'stored');
check($u['avatar3d'] === ['model' => 'female', 'jersey' => '#ff0000', 'shorts' => '', 'helmet' => '', 'shoes' => '', 'hair' => '',
    'bike' => '', 'boat' => ''], 'the avatar cleaned (known keys, lower case)');
check(count(users_pull($pdo, 0)) === 1 && count(users_pull($pdo, 1)) === 0, 'pulled by rev');

// an older change loses, a newer one wins
$r = users_push($pdo, ['name' => 'Pi 2', 'nowMs' => $now, 'users' => [['ftp' => 200] + $lone + ['updatedMs' => $now - 5000]]]);
check($r['results'][0]['taken'] === false && user_get($pdo, $a)['ftp'] === 110 && $r['rev'] === 1, 'older change loses');
check(($r['results'][0]['user']['ftp'] ?? 0) === 110, 'the loser gets the server\'s version');
$r = users_push($pdo, ['name' => 'Pi 2', 'nowMs' => $now, 'users' => [['ftp' => 120] + $lone + ['updatedMs' => $now]]]);
check($r['results'][0]['taken'] === true && user_get($pdo, $a)['ftp'] === 120 && $r['rev'] === 2, 'newer change wins');

// the station's clock is corrected: 1 h behind, its "old" time is really now
$late = $now - 3600 * 1000;
$r = users_push($pdo, ['name' => 'Slow Pi', 'nowMs' => $late, 'users' => [['ftp' => 130] + $lone + ['updatedMs' => $late + 500]]]);
check($r['results'][0]['taken'] === true && user_get($pdo, $a)['ftp'] === 130, 'a clock behind is corrected');
// a clock ahead: never later than the server's now
$r = users_push($pdo, ['name' => 'Fast Pi', 'users' => [['ftp' => 140] + $lone + ['updatedMs' => $now + 3600 * 1000]]]);
check(user_get($pdo, $a)['updatedMs'] <= now_ms(), 'a time in the future is cut to now');

// invalid values are refused
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [['ftp' => 9999] + $lone + ['updatedMs' => now_ms() + 10]]]);
check($r['results'][0]['taken'] === false && isset($r['results'][0]['error']), 'invalid FTP refused');
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [['name' => '<img src=x>'] + $lone + ['updatedMs' => now_ms() + 20]]]);
check($r['results'][0]['taken'] === false && user_get($pdo, $a)['name'] === 'Lone', 'a name with markup: refused');
$r = users_push($pdo, ['users' => [['uuid' => 'not-a-uuid'] + $lone]]);
check($r['results'] === [], 'a bad uuid is skipped');

// pictures: stored once by their sha1, a user may only show a stored one
$png = base64_decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==');
$err = '';
$hash = picture_store($pdo, $png, $err);
check($hash === sha1($png) && $err === '', 'a PNG is stored');
check(picture_store($pdo, 'not a picture', $err) === '' && $err !== '', 'not a picture: refused');
check(picture_get($pdo, $hash)['type'] === 'image/png', 'the picture with its type');
usleep(2000);
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [['pictureHash' => str_repeat('a', 40)] + $lone + ['updatedMs' => now_ms()]]]);
check($r['results'][0]['taken'] === false, 'an unknown picture: refused');
usleep(2000);
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [['pictureHash' => $hash] + $lone + ['updatedMs' => now_ms()]]]);
check($r['results'][0]['taken'] === true && user_get($pdo, $a)['pictureHash'] === $hash, 'a stored picture is shown');

// deleted on a station: a tombstone, pulled by the others
usleep(2000);
$rev = users_rev($pdo);
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [['uuid' => $a, 'deleted' => true, 'updatedMs' => now_ms()]]]);
$u = user_get($pdo, $a);
check($r['results'][0]['taken'] === true && $u['deleted'] === true && $u['name'] === 'Lone' && $u['pictureHash'] === null, 'deleted: a tombstone');
$pulled = users_pull($pdo, $rev);
check(count($pulled) === 1 && $pulled[0]['deleted'] === true, 'the tombstone is pulled');
// an older edit from a station that did not know does not bring it back...
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [$lone + ['updatedMs' => now_ms() - 60000]]]);
check($r['results'][0]['taken'] === false && user_get($pdo, $a)['deleted'] === true, 'an older edit does not undelete');
// ...a newer one does (the newest change wins)
usleep(2000);
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [$lone + ['updatedMs' => now_ms()]]]);
check($r['results'][0]['taken'] === true && user_get($pdo, $a)['deleted'] === false, 'a newer edit brings it back');
// deleting one the server never knew: nothing
$r = users_push($pdo, ['nowMs' => now_ms(), 'users' => [['uuid' => $b, 'deleted' => true, 'updatedMs' => now_ms()]]]);
check($r['results'][0]['taken'] === false && user_get($pdo, $b) === null, 'unknown deletion ignored');

$root->exec('DROP DATABASE artilody_test');
unlink($conf);
echo $failures === 0 ? "OK: $checks checks\n" : "$failures of $checks checks FAILED\n";
exit($failures === 0 ? 0 : 1);
