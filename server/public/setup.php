<?php
// First-time setup in the browser (XAMPP on Windows / macOS, and any other web
// server with PHP and MariaDB / MySQL where install.sh is not used). Opened
// on the server computer itself: http://localhost/artilody/setup.php.
//
// It checks PHP, creates the database "artilody" with its own user (a random
// password), applies schema.sql, writes config.php next to lib/ and sets the
// admin password of the web interface. Only from the server computer itself
// (localhost), and only while no configuration exists.
declare(strict_types=1);

require __DIR__ . '/../lib/bootstrap.php';
require __DIR__ . '/../lib/web.php';

$root = dirname(__DIR__);
$configFile = $root . '/config.php';
$local = in_array($_SERVER['REMOTE_ADDR'] ?? '', ['127.0.0.1', '::1'], true);
$done = artilody_configured();

// --- what this computer needs ----------------------------------------------
$checks = [
    ['PHP 8.1 or newer', PHP_VERSION_ID >= 80100, 'PHP ' . PHP_VERSION . ' – install a newer XAMPP (PHP 8.2).'],
    ['PDO MySQL (pdo_mysql)', extension_loaded('pdo_mysql'), 'Enable extension=pdo_mysql in php.ini.'],
    ['mbstring', extension_loaded('mbstring'), 'Enable extension=mbstring in php.ini.'],
    ['fileinfo', extension_loaded('fileinfo'), 'Enable extension=fileinfo in php.ini.'],
    ['zlib', function_exists('gzcompress'), 'PHP without zlib – install a standard XAMPP.'],
    ['The server folder can be written (config.php)', is_writable($root), $root . ' – give the web server write access.'],
    ['public/blobs can be written (media files)', is_writable(__DIR__ . '/blobs'), __DIR__ . '/blobs'],
    ['data/uploads can be written (uploads)', is_writable($root . '/data/uploads'), $root . '/data/uploads'],
];
if (function_exists('apache_get_modules')) {
    $checks[] = ['Apache mod_rewrite (the address without /public/)', in_array('mod_rewrite', apache_get_modules(), true),
        'Enable LoadModule rewrite_module in httpd.conf (XAMPP: on by default).'];
}
// uploads come in 8 MB pieces (media library), pictures up to 4 MB
function ini_bytes(string $v): int
{
    $v = trim($v);
    $n = (int)$v;
    return match (strtolower(substr($v, -1))) { 'g' => $n << 30, 'm' => $n << 20, 'k' => $n << 10, default => $n };
}
$checks[] = ['Uploads of 9 MB (post_max_size, upload_max_filesize)',
    ini_bytes((string)ini_get('post_max_size')) >= 9 << 20 && ini_bytes((string)ini_get('upload_max_filesize')) >= 9 << 20,
    'In php.ini set post_max_size = 16M and upload_max_filesize = 16M, then restart Apache.'];
$checks[] = ['Memory for PHP (memory_limit 128M or more)', ini_get('memory_limit') === '-1' || ini_bytes((string)ini_get('memory_limit')) >= 128 << 20,
    'In php.ini set memory_limit = 256M, then restart Apache.'];
$allOk = !in_array(false, array_column($checks, 1), true);

// the addresses of this computer on the network (for the stations)
function lan_addresses(): array
{
    $ips = [];
    foreach ((array)@gethostbynamel(gethostname()) as $ip) {
        if ($ip !== '127.0.0.1' && !str_starts_with($ip, '169.254.')) {
            $ips[] = $ip;
        }
    }
    // (a UDP "connection" opens no traffic; it only shows the address used for the network)
    $s = function_exists('socket_create') ? @socket_create(AF_INET, SOCK_DGRAM, SOL_UDP) : false;
    if ($s && @socket_connect($s, '192.168.1.1', 9) && @socket_getsockname($s, $ip) && $ip !== '127.0.0.1') {
        $ips[] = $ip;
    }
    return array_values(array_unique($ips));
}
// the address of this folder as the stations type it: "192.168.1.20/artilody"
function station_address(string $ip): string
{
    $path = preg_replace('#/public/$#', '/', base_path());
    return rtrim($ip . $path, '/');
}

$error = '';
$finished = false;
$updated = '';

// already set up: after copying a newer server folder over it, its database is updated here
if ($local && $done && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST' && ($_POST['action'] ?? '') === 'update') {
    csrf_check();
    try {
        $pdo = artilody_db();
        artilody_apply_schema($pdo, (string)file_get_contents($root . '/schema.sql'));
        $updated = 'The database is up to date (server ' . ARTILODY_SERVER_VERSION . ').';
    } catch (Throwable $e) {
        $error = 'The update did not work: ' . $e->getMessage();
    }
}
$form = ['dbUser' => 'root', 'dbHost' => '127.0.0.1', 'dbPort' => '3306', 'name' => 'Artilody server'];

if ($local && !$done && $allOk && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    csrf_check();
    $form = array_merge($form, array_map(fn($v) => trim((string)$v), array_intersect_key($_POST, $form + ['dbPass' => 1])));
    $dbPass = (string)($_POST['dbPass'] ?? '');
    $admin = (string)($_POST['admin'] ?? '');
    if (mb_strlen($admin) < 8) {
        $error = 'The admin password must have at least 8 characters.';
    } elseif ($admin !== (string)($_POST['admin2'] ?? '')) {
        $error = 'The two admin passwords are not the same.';
    } elseif (!preg_match('/^[A-Za-z0-9._-]{1,64}$/', $form['dbHost']) || !ctype_digit($form['dbPort'])) {
        $error = 'The database address is not valid.';
    } else {
        try {
            $dsnHost = 'mysql:host=' . $form['dbHost'] . ';port=' . $form['dbPort'] . ';charset=utf8mb4';
            $pdo = new PDO($dsnHost, $form['dbUser'], $dbPass, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
            $pass = bin2hex(random_bytes(20));
            $pdo->exec('CREATE DATABASE IF NOT EXISTS `artilody` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci');
            foreach (['localhost', '127.0.0.1', '::1'] as $h) {
                $pdo->exec("CREATE USER IF NOT EXISTS 'artilody'@'$h' IDENTIFIED BY '$pass'");
                $pdo->exec("ALTER USER 'artilody'@'$h' IDENTIFIED BY '$pass'");
                $pdo->exec("GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES, DROP ON `artilody`.* TO 'artilody'@'$h'");
            }
            $pdo->exec('FLUSH PRIVILEGES');
            $pdo->exec('USE `artilody`');
            artilody_apply_schema($pdo, (string)file_get_contents($root . '/schema.sql'));
            $q = $pdo->prepare("INSERT INTO meta (k, v) VALUES ('admin_hash', ?) ON DUPLICATE KEY UPDATE v = VALUES(v)");
            $q->execute([password_hash($admin, PASSWORD_DEFAULT)]);
            $config = "<?php\n// Artilody server configuration - made by setup.php, not in git.\nreturn " . var_export([
                'name' => mb_substr($form['name'] !== '' ? $form['name'] : 'Artilody server', 0, 60),
                'dsn' => 'mysql:host=' . $form['dbHost'] . ';port=' . $form['dbPort'] . ';dbname=artilody;charset=utf8mb4',
                'user' => 'artilody',
                'password' => $pass,
                'blobs_dir' => str_replace('\\', '/', __DIR__ . '/blobs'),
                'uploads_dir' => str_replace('\\', '/', $root . '/data/uploads'),
            ], true) . ";\n";
            if (@file_put_contents($configFile, $config, LOCK_EX) === false) {
                throw new RuntimeException('config.php could not be written in ' . $root);
            }
            @chmod($configFile, 0640);
            $finished = true;
        } catch (Throwable $e) {
            $error = $e instanceof PDOException
                ? 'The database did not accept it: ' . $e->getMessage() . ' – is MySQL started in the XAMPP Control Panel, and are the user and password right?'
                : $e->getMessage();
        }
    }
}

page_start('Setup');
?>
<h1>Set up the Artilody server</h1>
<?php if ($finished): ?>
  <div class="notice">The server is set up.</div>
  <div class="card">
    <h2 style="margin-top:0">Next: the stations</h2>
    <p>On every training computer (station): open Artilody › <b>Settings</b> › <b>Server</b>, switch it on and type this address:</p>
    <?php $ips = lan_addresses(); ?>
    <?php if ($ips): ?>
      <ul><?php foreach ($ips as $ip): ?><li style="font-size:20px"><code><?= h(station_address($ip)) ?></code></li><?php endforeach; ?></ul>
      <p class="muted">(If more than one is shown, it is the one that starts like the stations' own addresses, often 192.168.…)</p>
    <?php else: ?>
      <p>This computer's address on the network, followed by <code><?= h(station_address('')) ?></code> – see the network manual, “Find the server's address”.</p>
    <?php endif; ?>
    <p><a class="button" href="<?= h(u('')) ?>">Open the status page</a> <a class="button secondary" href="<?= h(u('admin/')) ?>">Log in (users, media)</a></p>
  </div>
<?php elseif ($done): ?>
  <?php if ($updated !== ''): ?><div class="notice"><?= h($updated) ?></div><?php endif; ?>
  <?php if ($error !== ''): ?><div class="error"><?= h($error) ?></div><?php endif; ?>
  <div class="card">
    <p>This server is already set up (server <?= h(ARTILODY_SERVER_VERSION) ?>). <a href="<?= h(u('')) ?>">Status page</a> · <a href="<?= h(u('admin/')) ?>">Log in</a></p>
    <?php if ($local): ?>
      <form method="post"><?= csrf_field() ?><input type="hidden" name="action" value="update">
        <p>Copied a newer server folder over this one? Update the database (nothing is lost):</p>
        <p><button>Update the database</button></p></form>
    <?php endif; ?>
    <p class="muted">To set it up from the start: delete <code>config.php</code> in the server folder (the database “artilody” and its data stay).</p>
  </div>
<?php elseif (!$local): ?>
  <div class="card">
    <p>For safety the setup only opens <b>on the server computer itself</b>.</p>
    <p>On the computer where XAMPP runs, open the browser and go to <code>http://localhost<?= h(preg_replace('#/public/$#', '/', base_path())) ?>setup.php</code></p>
  </div>
<?php else: ?>
  <div class="card">
    <h2 style="margin-top:0">1. Check</h2>
    <table>
      <?php foreach ($checks as [$what, $ok, $fix]): ?>
        <tr><td><?= $ok ? '<span class="ok">✔</span>' : '<span class="bad">✘</span>' ?></td><td><?= h($what) ?></td>
          <td class="muted"><?= $ok ? '' : h($fix) ?></td></tr>
      <?php endforeach; ?>
    </table>
    <?php if (!$allOk): ?><p class="bad">Fix the red lines, then reload this page.</p><?php endif; ?>
  </div>
  <?php if ($allOk): ?>
  <form method="post" class="card">
    <?= csrf_field() ?>
    <h2 style="margin-top:0">2. Database</h2>
    <p class="muted">The database's own administrator. XAMPP: user <b>root</b> and an <b>empty</b> password (leave it empty), unless you have changed it.</p>
    <?php if ($error !== ''): ?><div class="error"><?= h($error) ?></div><?php endif; ?>
    <div class="grid">
      <div><label for="dbUser">Database administrator</label><input type="text" id="dbUser" name="dbUser" value="<?= h($form['dbUser']) ?>"></div>
      <div><label for="dbPass">Its password (XAMPP: empty)</label><input type="password" id="dbPass" name="dbPass" autocomplete="off"></div>
      <div><label for="dbHost">Database address</label><input type="text" id="dbHost" name="dbHost" value="<?= h($form['dbHost']) ?>"></div>
      <div><label for="dbPort">Port</label><input type="text" id="dbPort" name="dbPort" value="<?= h($form['dbPort']) ?>"></div>
    </div>
    <h2>3. The server</h2>
    <div class="grid">
      <div><label for="name">Name shown on the stations</label><input type="text" id="name" name="name" maxlength="60" value="<?= h($form['name']) ?>"></div>
      <div><label for="admin">Admin password (web interface, at least 8 characters)</label><input type="password" id="admin" name="admin" autocomplete="new-password" required></div>
      <div><label for="admin2">Admin password again</label><input type="password" id="admin2" name="admin2" autocomplete="new-password" required></div>
    </div>
    <p class="muted">Write the admin password down – you need it to manage users, media and the server.</p>
    <p><button>Set up the server</button></p>
  </form>
  <?php endif; ?>
<?php endif; ?>
<?php
page_end();
