<?php
// The admin's login (lib/web.php).
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/web.php';

$next = safe_next((string)($_GET['next'] ?? $_POST['next'] ?? u('admin/')));
$error = '';
$hash = '';
try {
    $hash = admin_hash(artilody_db());
} catch (Throwable $e) {
    error_log('artilody login: ' . $e->getMessage());
    $error = 'The database does not answer.';
}

if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST' && $hash !== '') {
    csrf_check();
    if (password_verify((string)($_POST['password'] ?? ''), $hash)) {
        session_regenerate_id(true);
        $_SESSION['admin'] = true;
        header('Location: ' . $next);
        exit;
    }
    sleep(1);   // (slows guessing)
    $error = 'Wrong password.';
}

page_start('Log in');
?>
<h1>Log in</h1>
<div class="card" style="max-width:420px">
  <?php if ($error !== ''): ?><div class="error"><?= h($error) ?></div><?php endif; ?>
  <?php if ($hash === '' && $error === ''): ?>
    <?php if (!artilody_configured()): ?>
    <p>The server is not set up yet. On the server computer, open <code>http://localhost<?= h(preg_replace('#/public/$#', '/', base_path())) ?>setup.php</code>.</p>
    <?php endif; ?>
    <p>No admin password is set yet. Set it on the server (in a terminal):</p>
    <pre style="white-space:pre-wrap">sudo artilody-admin-password</pre>
    <p class="muted">From a Mac: <code>ssh -t lone@<?= h($_SERVER['SERVER_ADDR'] ?? 'server') ?> sudo artilody-admin-password</code></p>
  <?php else: ?>
    <form method="post">
      <?= csrf_field() ?>
      <input type="hidden" name="next" value="<?= h($next) ?>">
      <label for="password">Admin password</label>
      <input type="password" id="password" name="password" autofocus autocomplete="current-password">
      <p><button>Log in</button></p>
    </form>
  <?php endif; ?>
</div>
<?php
page_end();
