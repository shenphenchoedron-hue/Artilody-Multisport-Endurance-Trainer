<?php
// Shut the server down properly (the database is backed up on the way:
// artilody-backup.service) - instead of pulling the plug.
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/web.php';

require_admin();
// (the button needs install.sh's helper: Debian / Ubuntu / Raspberry Pi)
$canPower = is_executable('/usr/local/sbin/artilody-power');
$done = '';
$error = '';
if ($canPower && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    csrf_check();
    $action = ($_POST['action'] ?? '') === 'reboot' ? 'reboot' : 'poweroff';
    // (allowed for Apache by /etc/sudoers.d/artilody; it waits a few seconds so this page is answered)
    exec('sudo -n /usr/local/sbin/artilody-power ' . escapeshellarg($action) . ' 2>&1', $out, $status);
    if ($status === 0) {
        $done = $action;
    } else {
        $error = 'The server could not be shut down: ' . implode(' ', $out);
        error_log('artilody power: ' . $error);
    }
}

page_start('Server');
?>
<h1>Server</h1>
<?php if ($error !== ''): ?><div class="error"><?= h($error) ?></div><?php endif; ?>
<?php if ($done === 'poweroff'): ?>
  <div class="notice">The server is shutting down. Wait until its light stops blinking before you switch the power off.</div>
<?php elseif ($done === 'reboot'): ?>
  <div class="notice">The server is restarting – it is back in about a minute.</div>
<?php elseif (!$canPower): ?>
  <div class="card">
    <h2 style="margin-top:0">Shut down</h2>
    <p>On this computer the server cannot be shut down from here (that works on Debian, Ubuntu and Raspberry Pi servers installed with install.sh).</p>
    <p>When training is over: on the server computer, press <b>Stop</b> next to <b>Apache</b> and <b>MySQL</b> in the XAMPP Control Panel, then shut the computer down the usual way. The stations keep working on their own and send their changes the next time the server is on.</p>
  </div>
<?php else: ?>
  <div class="card">
    <h2 style="margin-top:0">Shut down</h2>
    <p>Shut the server down when training is over. The stations keep working on their own and send their changes the next time the server is on. The database is backed up during shut-down.</p>
    <form method="post" class="row">
      <?= csrf_field() ?>
      <button class="danger" name="action" value="poweroff" onclick="return confirm('Shut the server down?')">Shut down</button>
      <button class="secondary" name="action" value="reboot" onclick="return confirm('Restart the server?')">Restart</button>
    </form>
  </div>
<?php endif; ?>
<?php
page_end();
