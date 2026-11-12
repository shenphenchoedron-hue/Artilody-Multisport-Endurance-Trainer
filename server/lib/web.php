<?php
// The web interface's pages: layout, the admin login (one password, set with
// `sudo artilody-admin-password`, kept as a hash in meta.admin_hash) and the
// forms' CSRF tokens.
declare(strict_types=1);

function web_session(): void
{
    if (session_status() === PHP_SESSION_ACTIVE) {
        return;
    }
    session_name('artilody');
    session_set_cookie_params(['lifetime' => 0, 'path' => '/', 'httponly' => true, 'samesite' => 'Strict']);
    session_start();
}

function admin_hash(PDO $pdo): string
{
    $q = $pdo->query("SELECT v FROM meta WHERE k = 'admin_hash'");
    return (string)($q->fetchColumn() ?: '');
}

function admin_logged_in(): bool
{
    web_session();
    return !empty($_SESSION['admin']);
}

// The page needs the admin: otherwise to the login (and back here after it).
function require_admin(): void
{
    if (!admin_logged_in()) {
        header('Location: ' . u('admin/login.php') . '?next=' . rawurlencode($_SERVER['REQUEST_URI'] ?? '/admin/'));
        exit;
    }
}

function csrf_token(): string
{
    web_session();
    if (empty($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
    }
    return $_SESSION['csrf'];
}

function csrf_field(): string
{
    return '<input type="hidden" name="csrf" value="' . h(csrf_token()) . '">';
}

// A POST from one of our own forms (else stops with 400).
function csrf_check(): void
{
    web_session();
    if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST' || empty($_SESSION['csrf'])
        || !hash_equals($_SESSION['csrf'], (string)($_POST['csrf'] ?? ''))) {
        http_response_code(400);
        exit('Invalid form - reload the page and try again.');
    }
}

// Only paths of this site (after the login).
function safe_next(string $next): string
{
    return preg_match('#^/[A-Za-z0-9/_.\-?=&%]*$#', $next) && !str_starts_with($next, '//')
        && str_starts_with($next, base_path()) ? $next : u('admin/');
}

function page_start(string $title): void
{
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Frame-Options: DENY');
    header('X-Content-Type-Options: nosniff');
    $admin = admin_logged_in();
    ?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= h($title) ?> – <?= h(artilody_server_name()) ?></title>
<style>
  body { font-family: system-ui, sans-serif; background: #1b2a6b; color: #fff; margin: 0; }
  header { background: #15215a; padding: 14px 32px; display: flex; gap: 22px; align-items: center; }
  header a { color: #d6dcfa; text-decoration: none; } header a:hover { color: #fff; }
  header .brand { font-weight: 700; color: #fff; margin-right: auto; }
  main { max-width: 900px; margin: 0 auto; padding: 28px 32px; }
  h1 { margin: 0 0 18px; }
  .card { background: #25378a; border: 1px solid #3a4ea8; border-radius: 14px; padding: 18px 22px; margin-bottom: 18px; }
  .ok { color: #5fd38d; } .bad { color: #ff7b66; } .muted { color: #b9c3f0; }
  table { width: 100%; border-collapse: collapse; }
  th, td { text-align: left; padding: 8px 6px; border-bottom: 1px solid #3a4ea8; vertical-align: middle; }
  th { color: #b9c3f0; font-weight: 600; }
  a { color: #9fb4ff; }
  label { display: block; color: #b9c3f0; font-size: 14px; margin: 10px 0 4px; }
  input[type=text], input[type=number], input[type=password], select {
    background: #1b2a6b; color: #fff; border: 1px solid #4a5fc0; border-radius: 8px; padding: 8px 10px; font-size: 16px; width: 100%; box-sizing: border-box; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(190px, 1fr)); gap: 0 18px; }
  button, .button { background: #3d63ff; color: #fff; border: 0; border-radius: 9px; padding: 9px 18px; font-size: 15px; cursor: pointer; text-decoration: none; display: inline-block; }
  button.secondary, .button.secondary { background: #34479a; }
  button.danger { background: #c8402c; }
  .row { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
  .pic { width: 44px; height: 44px; border-radius: 50%; object-fit: cover; background: #34479a; display: inline-flex; align-items: center; justify-content: center; font-weight: 700; }
  .colors { display: flex; flex-wrap: wrap; gap: 10px 18px; }
  .colors div { display: flex; align-items: center; gap: 6px; }
  .error { background: #6b2020; border: 1px solid #c8402c; padding: 10px 14px; border-radius: 9px; margin-bottom: 14px; }
  .notice { background: #1f5a3c; border: 1px solid #3aa86b; padding: 10px 14px; border-radius: 9px; margin-bottom: 14px; }
</style>
</head>
<body>
<header>
  <a class="brand" href="<?= h(u('')) ?>"><?= h(artilody_server_name()) ?></a>
  <a href="<?= h(u('')) ?>">Status</a>
  <a href="<?= h(u('training.php')) ?>">Training</a>
  <a href="<?= h(u('admin/')) ?>">Users</a>
  <a href="<?= h(u('admin/media.php')) ?>">Media</a>
  <?php if ($admin): ?>
    <a href="<?= h(u('admin/server.php')) ?>">Server</a>
    <form method="post" action="<?= h(u('admin/logout.php')) ?>" style="margin:0"><?= csrf_field() ?><button class="secondary" style="padding:5px 12px">Log out</button></form>
  <?php endif; ?>
</header>
<main>
<?php
}

function page_end(): void
{
    echo "</main>\n</body>\n</html>\n";
}

function initials(string $name): string
{
    $out = '';
    foreach (preg_split('/\s+/u', trim($name), -1, PREG_SPLIT_NO_EMPTY) as $part) {
        $out .= mb_strtoupper(mb_substr($part, 0, 1));
        if (mb_strlen($out) >= 2) {
            break;
        }
    }
    return $out !== '' ? $out : '?';
}
