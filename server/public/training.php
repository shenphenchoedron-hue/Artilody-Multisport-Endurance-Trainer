<?php
// Training analysis (read only, no login): the users, then one user's
// training - totals, weekly hours / TSS, FTP over time, best power and the
// rides (lib/rides.php). A ride's details: ride.php.
declare(strict_types=1);

require __DIR__ . '/../lib/bootstrap.php';
require __DIR__ . '/../lib/users.php';
require __DIR__ . '/../lib/rides.php';
require __DIR__ . '/../lib/web.php';

$pdo = artilody_db();
$uuid = strtolower((string)($_GET['user'] ?? ''));
$user = is_uuid($uuid) ? user_get($pdo, $uuid) : null;
$sportFilter = in_array($_GET['sport'] ?? '', RIDE_SPORTS, true) ? $_GET['sport'] : '';

function avatar_html(array $u, int $size = 44): string
{
    $style = "width:{$size}px;height:{$size}px;font-size:" . (int)($size * 0.36) . 'px';
    return $u['pictureHash']
        ? '<img class="pic" style="' . $style . '" src="' . h(u('api/picture.php')) . '?hash=' . h($u['pictureHash']) . '" alt="">'
        : '<span class="pic" style="' . $style . '">' . h(initials($u['name'])) . '</span>';
}

if (!$user) {
    // --- the users ---
    $users = array_values(array_filter(users_pull($pdo, 0), fn($u) => !$u['deleted']));
    usort($users, fn($a, $b) => strcasecmp($a['name'], $b['name']));
    $stats = [];
    foreach ($pdo->query('SELECT user_uuid, COUNT(*) AS n, MAX(started_at) AS last, SUM(duration) AS secs'
        . ' FROM rides WHERE deleted = 0 GROUP BY user_uuid') as $r) {
        $stats[$r['user_uuid']] = $r;
    }
    page_start('Training');
    ?>
    <h1>Training</h1>
    <div class="card">
      <?php if (!$users): ?>
        <p class="muted">No users yet.</p>
      <?php else: ?>
        <table>
          <tr><th></th><th>User</th><th>Rides</th><th>Total time</th><th>Last ride</th></tr>
          <?php foreach ($users as $u): $s = $stats[$u['uuid']] ?? null; ?>
            <tr>
              <td><?= avatar_html($u) ?></td>
              <td><a href="<?= h(u('training.php')) ?>?user=<?= h($u['uuid']) ?>"><?= h($u['name']) ?></a></td>
              <td><?= (int)($s['n'] ?? 0) ?></td>
              <td><?= $s ? h(number_format((int)$s['secs'] / 3600, 1)) . ' h' : '–' ?></td>
              <td class="muted"><?= $s ? h(date('Y-m-d', (int)$s['last'])) : '–' ?></td>
            </tr>
          <?php endforeach; ?>
        </table>
      <?php endif; ?>
    </div>
    <?php
    page_end();
    exit;
}

// --- one user ---
$all = rides_of_user($pdo, $uuid, 5000);
$rides = $sportFilter === '' ? $all : array_values(array_filter($all, fn($r) => $r['sport'] === $sportFilter));
$now = time();
$period = function (int $days) use ($rides, $now): array {
    $sel = array_filter($rides, fn($r) => $r['startedAt'] >= $now - $days * 86400);
    return ['n' => count($sel), 'h' => array_sum(array_column($sel, 'durationSec')) / 3600,
        'tss' => array_sum(array_column($sel, 'tss')), 'km' => array_sum(array_column($sel, 'distanceM')) / 1000,
        'kj' => array_sum(array_column($sel, 'kj'))];
};

// the last 16 weeks (Monday to Sunday)
$weeks = [];
$monday = strtotime('monday this week', $now);
for ($i = 15; $i >= 0; --$i) {
    $weeks[date('Y-m-d', $monday - $i * 7 * 86400)] = ['h' => 0.0, 'tss' => 0.0];
}
foreach ($rides as $r) {
    $k = date('Y-m-d', strtotime('monday this week', $r['startedAt']));
    if (isset($weeks[$k])) {
        $weeks[$k]['h'] += $r['durationSec'] / 3600;
        $weeks[$k]['tss'] += $r['tss'];
    }
}
// FTP and average power over time (cycling)
$ftp = [];
foreach (array_reverse($all) as $r) {
    if ($r['sport'] === 'cycling' && $r['ftp'] > 0) {
        $ftp[] = [$r['startedAt'], $r['ftp']];
    }
}
// best power: all time and the last 90 days
$bestAll = $best90 = [];
foreach ($rides as $r) {
    foreach ((array)$r['bests'] as $s => $w) {
        $bestAll[$s] = max($bestAll[$s] ?? 0, (int)$w);
        if ($r['startedAt'] >= $now - 90 * 86400) {
            $best90[$s] = max($best90[$s] ?? 0, (int)$w);
        }
    }
}
$page = max(0, (int)($_GET['page'] ?? 0));
$perPage = 40;
$list = array_slice($rides, $page * $perPage, $perPage);

page_start($user['name'] . ' – Training');
?>
<div class="row" style="gap:16px;margin-bottom:12px">
  <?= avatar_html($user, 64) ?>
  <div>
    <h1 style="margin:0"><?= h($user['name']) ?></h1>
    <div class="muted">FTP <?= (int)$user['ftp'] ?> W · <?= h((string)$user['weightKg']) ?> kg ·
      <?= $user['weightKg'] > 0 ? number_format($user['ftp'] / $user['weightKg'], 2) : '–' ?> W/kg</div>
  </div>
  <div style="margin-left:auto" class="row">
    <?php foreach (['' => 'All', 'cycling' => 'Rides', 'rowing' => 'Rows', 'running' => 'Runs'] as $k => $label): ?>
      <a class="button <?= $sportFilter === $k ? '' : 'secondary' ?>" style="padding:6px 12px"
         href="<?= h(u('training.php')) ?>?user=<?= h($uuid) ?><?= $k !== '' ? '&sport=' . $k : '' ?>"><?= $label ?></a>
    <?php endforeach; ?>
  </div>
</div>

<div class="grid" style="grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:14px;margin-bottom:18px">
  <?php foreach (['Last 7 days' => 7, 'Last 28 days' => 28, 'Last 365 days' => 365] as $label => $days): $p = $period($days); ?>
    <div class="card" style="margin:0">
      <div class="muted"><?= $label ?></div>
      <div style="font-size:26px;font-weight:700"><?= number_format($p['h'], 1) ?> h</div>
      <div class="muted"><?= $p['n'] ?> sessions · TSS <?= round($p['tss']) ?> · <?= number_format($p['km'], 0) ?> km</div>
    </div>
  <?php endforeach; ?>
</div>

<div class="card">
  <h2 style="margin-top:0">Weekly hours and TSS</h2>
  <canvas id="weekly" style="width:100%;height:240px"></canvas>
  <div class="muted"><span style="color:#5b8cff">■</span> hours (left) · <span style="color:#ff9e40">●</span> TSS (right)</div>
</div>

<?php if ($ftp): ?>
<div class="card">
  <h2 style="margin-top:0">FTP over time</h2>
  <canvas id="ftp" style="width:100%;height:200px"></canvas>
</div>
<?php endif; ?>

<?php if ($bestAll): ?>
<div class="card">
  <h2 style="margin-top:0">Best power</h2>
  <table>
    <tr><th></th><?php foreach (RIDE_BEST_SECONDS as $s): ?><th><?= $s < 60 ? $s . ' s' : ($s / 60) . ' min' ?></th><?php endforeach; ?></tr>
    <tr><td class="muted">All time</td><?php foreach (RIDE_BEST_SECONDS as $s): ?><td><?= isset($bestAll[$s]) ? $bestAll[$s] . ' W' : '–' ?></td><?php endforeach; ?></tr>
    <tr><td class="muted">Last 90 days</td><?php foreach (RIDE_BEST_SECONDS as $s): ?><td><?= isset($best90[$s]) ? $best90[$s] . ' W' : '–' ?></td><?php endforeach; ?></tr>
  </table>
</div>
<?php endif; ?>

<div class="card">
  <h2 style="margin-top:0">Sessions (<?= count($rides) ?>)</h2>
  <?php if (!$rides): ?>
    <p class="muted">No sessions yet. The stations send them by themselves (Settings › Server on).</p>
  <?php else: ?>
    <table>
      <tr><th>Date</th><th></th><th>Workout</th><th>Time</th><th>Distance</th><th>Avg W</th><th>NP</th><th>HR</th><th>TSS</th><th>Station</th></tr>
      <?php foreach ($list as $r): ?>
        <tr>
          <td><a href="<?= h(u('ride.php')) ?>?uuid=<?= h($r['uuid']) ?>"><?= h(date('Y-m-d H:i', $r['startedAt'])) ?></a></td>
          <td><?= h(sport_label($r['sport'])) ?></td>
          <td><?= h($r['workoutName'] !== '' ? $r['workoutName'] : '–') ?></td>
          <td><?= h(fmt_duration($r['durationSec'])) ?></td>
          <td><?= $r['distanceM'] > 0 ? number_format($r['distanceM'] / 1000, 1) . ' km' : '–' ?></td>
          <td><?= $r['avgPower'] > 0 ? round($r['avgPower']) : '–' ?></td>
          <td><?= $r['np'] > 0 ? round($r['np']) : '–' ?></td>
          <td><?= $r['avgHr'] > 0 ? round($r['avgHr']) : '–' ?></td>
          <td><?= $r['tss'] > 0 ? round($r['tss']) : '–' ?></td>
          <td class="muted"><?= h($r['stationName']) ?></td>
        </tr>
      <?php endforeach; ?>
    </table>
    <p class="row">
      <?php if ($page > 0): ?><a class="button secondary" href="?user=<?= h($uuid) ?>&sport=<?= h($sportFilter) ?>&page=<?= $page - 1 ?>">‹ Newer</a><?php endif; ?>
      <?php if (($page + 1) * $perPage < count($rides)): ?><a class="button secondary" href="?user=<?= h($uuid) ?>&sport=<?= h($sportFilter) ?>&page=<?= $page + 1 ?>">Older ›</a><?php endif; ?>
    </p>
  <?php endif; ?>
</div>

<script src="<?= h(u('assets/charts.js')) ?>"></script>
<script>
const weeks = <?= json_encode(array_map(fn($k, $v) => [date('j M', strtotime($k)), round($v['h'], 2), round($v['tss'])], array_keys($weeks), $weeks)) ?>;
const ftp = <?= json_encode($ftp) ?>;
function draw() {
  barChart(document.getElementById('weekly'), {
    labels: weeks.map((w) => w[0]),
    bars: [{ name: 'Hours', color: '#5b8cff', values: weeks.map((w) => w[1]) }],
    line: { name: 'TSS', color: '#ff9e40', values: weeks.map((w) => w[2]) },
  });
  const f = document.getElementById('ftp');
  if (f) lineChart(f, { series: [{ name: 'FTP', color: '#5fd38d', points: ftp, width: 2 }],
    leftMin: Math.max(0, Math.floor((Math.min(...ftp.map((p) => p[1])) - 20) / 10) * 10),
    xFormat: (x) => new Date(x * 1000).toLocaleDateString(undefined, { day: 'numeric', month: 'short' }) });
}
draw();
window.addEventListener('resize', draw);
</script>
<?php
page_end();
