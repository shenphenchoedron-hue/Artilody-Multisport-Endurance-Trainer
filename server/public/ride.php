<?php
// One ride (read only, no login): its numbers, the chart (power, target,
// heart rate, cadence, speed), zones, best power and laps (lib/rides.php).
declare(strict_types=1);

require __DIR__ . '/../lib/bootstrap.php';
require __DIR__ . '/../lib/users.php';
require __DIR__ . '/../lib/rides.php';
require __DIR__ . '/../lib/web.php';

$pdo = artilody_db();
$uuid = strtolower((string)($_GET['uuid'] ?? ''));
$ride = is_uuid($uuid) ? ride_get($pdo, $uuid) : null;
if (!$ride || $ride['deleted']) {
    http_response_code(404);
    page_start('Not found');
    echo '<h1>No such session</h1><p><a href="' . h(u('training.php')) . '">Training</a></p>';
    page_end();
    exit;
}
$user = user_get($pdo, $ride['userUuid']);
$s = $ride['samples'];
$hasSpeed = count(array_filter(array_column($s, 5), fn($v) => $v !== null && $v > 0)) > 0;
$isRun = $ride['sport'] === 'running';
$tiles = array_filter([
    'Time' => fmt_duration($ride['durationSec']),
    'Distance' => $ride['distanceM'] > 0 ? number_format($ride['distanceM'] / 1000, 2) . ' km' : null,
    'Avg power' => $ride['avgPower'] > 0 ? round($ride['avgPower']) . ' W' : null,
    'NP' => $ride['np'] > 0 ? round($ride['np']) . ' W' : null,
    'Max power' => $ride['maxPower'] > 0 ? $ride['maxPower'] . ' W' : null,
    'Avg HR' => $ride['avgHr'] > 0 ? round($ride['avgHr']) . ' BPM' : null,
    'Max HR' => $ride['maxHr'] > 0 ? $ride['maxHr'] . ' BPM' : null,
    ($ride['sport'] === 'rowing' ? 'Stroke rate' : 'Cadence') => $ride['avgCadence'] > 0 ? round($ride['avgCadence']) . ($ride['sport'] === 'rowing' ? ' SPM' : ' RPM') : null,
    'TSS' => $ride['tss'] > 0 ? (string)round($ride['tss']) : null,
    'IF' => $ride['ifValue'] > 0 ? number_format($ride['ifValue'], 2) : null,
    ($isRun ? 'kcal' : 'Work') => $ride['kj'] > 0 ? round($ride['kj']) . ($isRun ? '' : ' kJ') : null,
    'FTP' => $ride['ftp'] > 0 && !$isRun ? $ride['ftp'] . ' W' : null,
]);
$zoneNames = $isRun ? ['Z1', 'Z2', 'Z3', 'Z4', 'Z5', 'Z6'] : ['Recovery', 'Endurance', 'Tempo', 'Threshold', 'VO2max', 'Anaerobic'];
$zoneColors = ['#9aa5c8', '#5b8cff', '#5fd38d', '#f0c040', '#ff9e40', '#ff5f5f'];
$zoneTotal = max(1, array_sum($ride['zones']));

page_start(($ride['workoutName'] ?: sport_label($ride['sport'])) . ' – ' . date('Y-m-d', $ride['startedAt']));
?>
<p class="muted" style="margin:0 0 4px">
  <a href="<?= h(u('training.php')) ?>?user=<?= h($ride['userUuid']) ?>"><?= h($user['name'] ?? 'Unknown user') ?></a> ·
  <?= h(sport_label($ride['sport'])) ?> · <?= h(date('l j F Y, H:i', $ride['startedAt'])) ?>
  <?= $ride['stationName'] !== '' ? ' · ' . h($ride['stationName']) : '' ?>
</p>
<h1><?= h($ride['workoutName'] !== '' ? $ride['workoutName'] : sport_label($ride['sport'])) ?></h1>

<div class="grid" style="grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:12px;margin-bottom:18px">
  <?php foreach ($tiles as $label => $value): ?>
    <div class="card" style="margin:0;padding:12px 14px">
      <div class="muted" style="font-size:13px"><?= h($label) ?></div>
      <div style="font-size:22px;font-weight:700"><?= h($value) ?></div>
    </div>
  <?php endforeach; ?>
</div>

<?php if ($s): ?>
<div class="card">
  <div id="legend" style="margin-bottom:8px"></div>
  <canvas id="chart" style="width:100%;height:320px"></canvas>
</div>
<?php endif; ?>

<?php if (array_sum($ride['zones']) > 0): ?>
<div class="card">
  <h2 style="margin-top:0"><?= $isRun ? 'Speed zones' : 'Power zones' ?></h2>
  <?php foreach ($ride['zones'] as $i => $secs): ?>
    <div class="row" style="gap:10px;margin:4px 0">
      <span style="width:110px" class="muted"><?= h($zoneNames[$i] ?? 'Z' . ($i + 1)) ?></span>
      <span style="flex:1;background:#1b2a6b;border-radius:4px;height:14px;overflow:hidden">
        <span style="display:block;height:14px;width:<?= round($secs / $zoneTotal * 100, 1) ?>%;background:<?= $zoneColors[$i] ?? '#888' ?>"></span></span>
      <span style="width:120px;text-align:right"><?= h(fmt_duration((int)$secs)) ?> · <?= round($secs / $zoneTotal * 100) ?> %</span>
    </div>
  <?php endforeach; ?>
</div>
<?php endif; ?>

<?php if ((array)$ride['bests']): ?>
<div class="card">
  <h2 style="margin-top:0">Best power</h2>
  <table><tr>
    <?php foreach ((array)$ride['bests'] as $sec => $w): ?><th><?= $sec < 60 ? $sec . ' s' : ($sec / 60) . ' min' ?></th><?php endforeach; ?>
  </tr><tr>
    <?php foreach ((array)$ride['bests'] as $w): ?><td><?= (int)$w ?> W</td><?php endforeach; ?>
  </tr></table>
</div>
<?php endif; ?>

<?php if ($ride['laps']): ?>
<div class="card">
  <h2 style="margin-top:0">Laps</h2>
  <table>
    <tr><th>Lap</th><th>Time</th><th>Distance</th></tr>
    <?php foreach ($ride['laps'] as $l): ?>
      <tr><td><?= (int)$l[0] ?></td><td><?= h(fmt_duration((int)round($l[1]))) ?></td><td><?= $l[2] > 0 ? number_format($l[2] / 1000, 2) . ' km' : '–' ?></td></tr>
    <?php endforeach; ?>
  </table>
</div>
<?php endif; ?>

<?php if ($s): ?>
<script src="<?= h(u('assets/charts.js')) ?>"></script>
<script>
const s = <?= json_encode($s) ?>;
const pts = (i, f) => s.map((r) => [r[0], r[i] == null ? null : (f ? f(r[i]) : r[i])]);
const series = [
  <?php if (!$isRun): ?>{ name: 'Power (W)', color: '#5b8cff', points: pts(1) },
  { name: 'Target (W)', color: '#9aa5c8', points: pts(2), hidden: <?= max(array_column($s, 2)) > 0 ? 'false' : 'true' ?> },<?php endif; ?>
  { name: 'Heart rate (BPM)', color: '#ff5f5f', points: pts(4), axis: 'right', hidden: <?= max(array_column($s, 4)) > 0 ? 'false' : 'true' ?> },
  { name: '<?= $ride['sport'] === 'rowing' ? 'Stroke rate (SPM)' : 'Cadence (RPM)' ?>', color: '#f0c040', points: pts(3), axis: 'right', hidden: true },
  <?php if ($hasSpeed): ?>{ name: 'Speed (km/h)', color: '#5fd38d', points: pts(5, (v) => v * 3.6), axis: '<?= $isRun ? 'left' : 'right' ?>', hidden: <?= $isRun ? 'false' : 'true' ?> },<?php endif; ?>
];
// the time axis: h:mm:ss for an hour or more, else m:ss
const fmt = (x) => { x = Math.round(x); const h = Math.floor(x / 3600), m = Math.floor(x % 3600 / 60), sec = String(x % 60).padStart(2, '0');
  return h ? h + ':' + String(m).padStart(2, '0') + ':' + sec : m + ':' + sec; };
function draw() { lineChart(document.getElementById('chart'), { series, xFormat: fmt }); }
legend(document.getElementById('legend'), series, (i, hidden) => { series[i].hidden = hidden; draw(); });
draw();
window.addEventListener('resize', draw);
</script>
<?php endif; ?>
<?php
page_end();
