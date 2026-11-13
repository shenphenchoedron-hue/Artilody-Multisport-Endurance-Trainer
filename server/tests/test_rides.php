<?php
// Tests of lib/rides.php against a throw-away database (artilody_test).
//   sudo php ~/artilody-server/tests/test_rides.php
declare(strict_types=1);

$dir = dirname(__DIR__);
$conf = tempnam(sys_get_temp_dir(), 'artilody-test');
file_put_contents($conf, "<?php return ['dsn' => 'mysql:unix_socket=/run/mysqld/mysqld.sock;dbname=artilody_test;charset=utf8mb4',"
    . " 'user' => 'root', 'password' => ''];");
putenv('ARTILODY_CONFIG=' . $conf);
require $dir . '/lib/bootstrap.php';
require $dir . '/lib/users.php';
require $dir . '/lib/rides.php';

$root = new PDO('mysql:unix_socket=/run/mysqld/mysqld.sock;charset=utf8mb4', 'root', '', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
$root->exec('DROP DATABASE IF EXISTS artilody_test');
$root->exec('CREATE DATABASE artilody_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci');
$root->exec('USE artilody_test');
foreach ([1, 2] as $_) {
    foreach (array_filter(array_map('trim', explode(";\n", preg_replace('/^--.*$/m', '', file_get_contents($dir . '/schema.sql'))))) as $sql) {
        $root->exec($sql);
    }
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
$user = 'c11fffc6-cc95-421a-a39d-b65743745edc';
$station = 'bc0d03d2-5cb0-42f0-b6a4-1106abbfaed9';
$samples = [];
for ($t = 0; $t < 400; ++$t) {
    $samples[] = [$t, $t < 300 ? 200 : 300, 210, 90, 140, $t % 2 ? 8.5 : null];
}
$ride = ['uuid' => 'a0000000-0000-4000-8000-000000000001', 'userUuid' => $user, 'sport' => 'cycling', 'workoutName' => 'Sweet spot',
    'startedAt' => 1763000000, 'durationSec' => 400, 'blocks' => 3, 'erg' => true, 'ftp' => 250, 'avgPower' => 225, 'maxPower' => 300,
    'np' => 230, 'avgHr' => 140, 'maxHr' => 150, 'avgCadence' => 90, 'kj' => 90, 'ifValue' => 0.92, 'tss' => 9.4,
    'zones' => [10, 20, 30, 40, 300, 0], 'distanceM' => 3400, 'samples' => $samples, 'laps' => [[1, 200.5, 1700], [2, 199.5, 1700]],
    'trace' => ['uuid' => 'b0000000-0000-4000-8000-000000000001', 'sport' => 'gpx', 'routeKey' => 'abc', 'routeName' => 'Flad',
        'startM' => 0, 'loop' => false, 'completed' => true, 'durationSec' => 400, 'distanceM' => 3400, 'createdAt' => '2026-11-12T10:00:00',
        'points' => [[0, 0], [1, 8.5], [400, 3400]]]];

check(rides_rev($pdo) === 0, 'no rides yet');
$r = rides_push($pdo, ['station' => $station, 'name' => 'iMac', 'rides' => [$ride]]);
check($r['results'][0]['taken'] === true && $r['rev'] === 1, 'a ride is taken');
$got = ride_get($pdo, $ride['uuid']);
check($got['workoutName'] === 'Sweet spot' && $got['stationName'] === 'iMac' && $got['userUuid'] === $user, 'stored');
check(count($got['samples']) === 400 && $got['samples'][1][5] === 8.5 && $got['samples'][0][5] === null, 'samples kept (speed null)');
check($got['laps'][0] === [1, 200.5, 1700] && $got['trace']['routeKey'] === 'abc' && count($got['trace']['points']) === 3, 'laps and trace');
check($got['bests']['5'] === 300 && $got['bests']['60'] === 300 && $got['bests']['300'] === 233, 'best power computed');
check(!isset($got['bests']['1200']), 'no 20 min best on a short ride');

// again: not changed, still taken
$r = rides_push($pdo, ['station' => $station, 'rides' => [['workoutName' => 'Other'] + $ride]]);
check($r['results'][0]['taken'] === true && $r['rev'] === 1 && ride_get($pdo, $ride['uuid'])['workoutName'] === 'Sweet spot', 'sent twice: unchanged');

// the list leaves the data out; by rev
$list = rides_pull($pdo, 0);
check(count($list) === 1 && !isset($list[0]['samples']) && $list[0]['tss'] === 9.4, 'the list: summaries');
check(rides_pull($pdo, 1) === [], 'nothing after rev 1');
check(count(rides_of_user($pdo, $user)) === 1, 'a user\'s rides');

// bad ones
$r = rides_push($pdo, ['rides' => [['uuid' => 'nope'], ['uuid' => 'a0000000-0000-4000-8000-000000000002', 'userUuid' => 'x']]]);
check(count($r['results']) === 1 && $r['results'][0]['taken'] === false, 'no user: refused');
$r = rides_push($pdo, ['rides' => [['uuid' => 'a0000000-0000-4000-8000-000000000003', 'userUuid' => $user, 'sport' => 'skiing',
    'samples' => [[0, 99999, 0, 0, 0], 'junk', [1, 100, 0, 0, 0]]]]]);
$odd = ride_get($pdo, 'a0000000-0000-4000-8000-000000000003');
check($odd['sport'] === 'cycling' && count($odd['samples']) === 2 && $odd['samples'][0][1] === 5000, 'odd values cleaned');

// deleted: a tombstone, pulled; sending it again answers deleted
$rev = rides_rev($pdo);
$r = rides_push($pdo, ['rides' => [['uuid' => $ride['uuid'], 'deleted' => true]]]);
check($r['results'][0]['deleted'] === true, 'deleted');
$t = ride_get($pdo, $ride['uuid']);
check($t['deleted'] && $t['samples'] === [] && $t['trace'] === null, 'a tombstone without data');
$pulled = rides_pull($pdo, $rev);
check(count($pulled) === 1 && $pulled[0]['deleted'], 'the tombstone is pulled');
$r = rides_push($pdo, ['rides' => [$ride]]);
check(($r['results'][0]['deleted'] ?? false) === true && ride_get($pdo, $ride['uuid'])['deleted'], 'sent again after deletion: stays deleted');
// deleting one never here: a tombstone (never taken later)
$r = rides_push($pdo, ['rides' => [['uuid' => 'a0000000-0000-4000-8000-000000000009', 'deleted' => true]]]);
$r = rides_push($pdo, ['rides' => [['uuid' => 'a0000000-0000-4000-8000-000000000009'] + $ride]]);
check(($r['results'][0]['deleted'] ?? false) === true, 'deleted before it arrived: not taken');
check(count(rides_of_user($pdo, $user)) === 1, 'deleted ones are not listed');

// a user joined into another: its rides follow (a new rev each)
$keep = 'f51db3d3-7c56-4aec-bc8d-720622dcdd2a';
$r9 = ['uuid' => 'a0000000-0000-4000-8000-0000000000b9', 'deleted' => false] + $ride;
unset($r9['trace']);
rides_push($pdo, ['rides' => [$r9]]);
$rev = rides_rev($pdo);
$pdo->beginTransaction();
user_merge($pdo, $user, $keep, 'Lone', 'test');
$pdo->commit();
check(ride_get($pdo, $r9['uuid'])['userUuid'] === $keep && ride_get($pdo, $r9['uuid'])['rev'] > $rev, 'rides follow a joined user');

$root->exec('DROP DATABASE artilody_test');
unlink($conf);
echo $failures === 0 ? "OK: $checks checks\n" : "$failures of $checks checks FAILED\n";
exit($failures === 0 ? 0 : 1);
