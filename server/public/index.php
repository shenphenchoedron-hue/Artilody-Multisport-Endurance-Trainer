<?php
// The server's front page: is it running, does the database answer, which
// stations have been in touch. (Users and shut-down: /admin/, with the login.)
declare(strict_types=1);

require __DIR__ . '/../lib/bootstrap.php';
require __DIR__ . '/../lib/web.php';

$dbOk = false;
$dbError = '';
$stations = [];
$userCount = 0;
try {
    $pdo = artilody_db();
    $stations = $pdo
        ->query('SELECT name, app_version, last_ip, first_seen, last_seen,'
            . ' TIMESTAMPDIFF(SECOND, last_seen, UTC_TIMESTAMP()) AS ago FROM stations ORDER BY last_seen DESC')
        ->fetchAll();
    $userCount = (int)$pdo->query('SELECT COUNT(*) FROM users WHERE deleted = 0')->fetchColumn();
    $dbOk = true;
} catch (Throwable $e) {
    $dbError = 'The database does not answer (see the Apache error log).';
    error_log('artilody index: ' . $e->getMessage());
}

function ago(int $s): string
{
    if ($s < STATION_ONLINE_SEC) {
        return 'just now';
    }
    if ($s < 3600) {
        return intdiv($s, 60) . ' min ago';
    }
    if ($s < 86400) {
        return intdiv($s, 3600) . ' h ago';
    }
    return intdiv($s, 86400) . ' days ago';
}

if (!artilody_configured()) {
    page_start('Not set up');
    echo '<h1>Not set up yet</h1><div class="card"><p>On the server computer itself, open <code>http://localhost'
        . h(preg_replace('#/public/$#', '/', base_path())) . 'setup.php</code> in the browser.</p>'
        . '<p class="muted">(Debian, Ubuntu and Raspberry Pi: install with <code>sudo bash install.sh</code> instead – see the network manual.)</p></div>';
    page_end();
    exit;
}

page_start('Status');
?>
<h1>Status</h1>
<div class="muted" style="margin:-10px 0 18px">Artilody server <?= h(ARTILODY_SERVER_VERSION) ?> · API <?= ARTILODY_API ?></div>

<div class="card">
  <strong>Status:</strong>
  <?php if ($dbOk): ?>
    <span class="ok">running, database OK</span> · <?= $userCount ?> user<?= $userCount === 1 ? '' : 's' ?>
  <?php else: ?>
    <span class="bad"><?= h($dbError) ?></span>
  <?php endif; ?>
</div>

<div class="card">
  <h2 style="margin-top:0">Stations</h2>
  <?php if (!$stations): ?>
    <p class="muted">No station has been in touch yet. In the app: Settings › Server › on, address of this server.</p>
  <?php else: ?>
    <table>
      <tr><th>Station</th><th>App</th><th>Address</th><th>Last seen</th><th>First seen</th></tr>
      <?php foreach ($stations as $s): ?>
        <tr>
          <td><?= h($s['name'] !== '' ? $s['name'] : '(unnamed)') ?></td>
          <td><?= h($s['app_version']) ?></td>
          <td><?= h($s['last_ip']) ?></td>
          <td class="<?= (int)$s['ago'] < STATION_ONLINE_SEC ? 'ok' : 'muted' ?>"><?= h(ago((int)$s['ago'])) ?></td>
          <td class="muted"><?= h(substr((string)$s['first_seen'], 0, 16)) ?> UTC</td>
        </tr>
      <?php endforeach; ?>
    </table>
  <?php endif; ?>
</div>
<?php
page_end();
