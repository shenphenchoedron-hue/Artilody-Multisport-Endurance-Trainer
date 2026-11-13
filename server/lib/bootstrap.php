<?php
// Shared by every page and API endpoint of the Artilody server
// (docs/server-plan.md). Lives outside the web root (public/).
declare(strict_types=1);

const ARTILODY_SERVER_VERSION = '2.1.1';   // (with the app's release)
// The API generation the stations speak; raised only when old stations can
// no longer talk to this server.
const ARTILODY_API = 1;

// The column sizes of `stations` (schema.sql) - what ping.php keeps of a
// station's headers. Change both together.
const STATION_NAME_MAX = 100;      // stations.name VARCHAR(100)
const STATION_VERSION_MAX = 40;    // stations.app_version VARCHAR(40)
const STATION_IP_MAX = 45;         // stations.last_ip VARCHAR(45) (IPv6)

// A station pings every 30 s (ServerConnection::kIntervalMs in the app): seen
// within 3 pings counts as online on the status page.
const STATION_ONLINE_SEC = 3 * 30;

// The configuration (not in git): name, dsn, user, password and - XAMPP -
// blobs_dir / uploads_dir. Looked for in this order:
//   ARTILODY_CONFIG (environment; the tests),
//   config.php next to lib/ (XAMPP and other copies: made by public/setup.php),
//   /etc/artilody/config.php (Debian / Ubuntu / Raspberry Pi: made by install.sh).
function artilody_config_file(): string
{
    $env = getenv('ARTILODY_CONFIG');
    if ($env) {
        return $env;
    }
    $local = dirname(__DIR__) . '/config.php';
    return is_file($local) ? $local : '/etc/artilody/config.php';
}

function artilody_config(): array
{
    static $config = null;
    if ($config === null) {
        $file = artilody_config_file();
        $config = is_readable($file) ? (require $file) : [];
        if (!is_array($config)) {
            $config = [];
        }
    }
    return $config;
}

// The server set up (a configuration found)?
function artilody_configured(): bool
{
    return artilody_config() !== [];
}

// The web root as the browser sees it, ending in "/": "/" (Debian install:
// public/ is the site) or e.g. "/artilody/public/" (XAMPP: in a folder of
// htdocs). From the running script: public/x.php, public/admin/x.php, public/api/x.php.
function base_path(): string
{
    static $base = null;
    if ($base === null) {
        $dir = str_replace('\\', '/', dirname((string)($_SERVER['SCRIPT_NAME'] ?? '/index.php')));
        $dir = preg_replace('#/(admin|api)$#', '', $dir);
        $base = rtrim($dir, '/') . '/';
    }
    return $base;
}

// A link to a page of the site: u('admin/') -> "/admin/" or "/artilody/public/admin/".
function u(string $path): string
{
    return base_path() . ltrim($path, '/');
}

function artilody_db(): PDO
{
    static $pdo = null;
    if ($pdo === null) {
        $c = artilody_config();
        $pdo = new PDO(
            $c['dsn'] ?? 'mysql:host=localhost;dbname=artilody;charset=utf8mb4',
            $c['user'] ?? 'artilody',
            $c['password'] ?? '',
            [
                PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                PDO::ATTR_EMULATE_PREPARES => false,
            ]
        );
    }
    return $pdo;
}

function artilody_server_name(): string
{
    $name = trim((string)(artilody_config()['name'] ?? ''));
    return $name !== '' ? $name : 'Artilody server';
}

function json_out(array $data, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

// ms since 1970 UTC (the server's clock: the newest change wins).
function now_ms(): int
{
    return (int)floor(microtime(true) * 1000);
}

// A random (version 4) uuid, lower case.
function new_uuid(): string
{
    $b = random_bytes(16);
    $b[6] = chr((ord($b[6]) & 0x0f) | 0x40);
    $b[8] = chr((ord($b[8]) & 0x3f) | 0x80);
    return vsprintf('%s%s-%s-%s-%s-%s%s%s', str_split(bin2hex($b), 4));
}

// Lower-case 8-4-4-4-12 hex (the stations' and rows' uuids).
function is_uuid(string $s): bool
{
    return preg_match('/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/', $s) === 1;
}

// A request header, trimmed and cut to $max characters ('' when missing).
function request_header(string $name, int $max): string
{
    $key = 'HTTP_' . strtoupper(str_replace('-', '_', $name));
    return mb_substr(trim((string)($_SERVER[$key] ?? '')), 0, $max);
}

// A POST of the stations' API must name the station (X-Artilody-Station) or
// come from the web interface's own page (X-Artilody-Web): a custom header,
// so a browser asks the server first (CORS preflight, never approved) - no
// web page elsewhere can send these. Stops with 403 otherwise.
function require_station_header(): void
{
    if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
        return;
    }
    $station = strtolower(request_header('X-Artilody-Station', 36));
    if (!is_uuid($station) && request_header('X-Artilody-Web', 8) !== '1') {
        json_out(['error' => 'the X-Artilody-Station header is needed'], 403);
    }
}

// schema.sql applied (setup.php, the tests; install.sh uses the mariadb
// client): statement by statement. "ALTER TABLE t ADD COLUMN IF NOT EXISTS c
// ..." is MariaDB's - on MySQL the column is added when it is missing.
function artilody_apply_schema(PDO $pdo, string $sql): void
{
    $sql = preg_replace('/^--.*$/m', '', str_replace("\r\n", "\n", $sql));
    foreach (array_filter(array_map('trim', explode(";\n", $sql))) as $statement) {
        try {
            $pdo->exec($statement);
        } catch (PDOException $e) {
            if (!preg_match('/^ALTER TABLE\s+(\w+)\s+ADD COLUMN IF NOT EXISTS\s+(\w+)\s+(.*)$/is', $statement, $m)) {
                throw $e;
            }
            $q = $pdo->prepare('SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE()'
                . ' AND TABLE_NAME = ? AND COLUMN_NAME = ?');
            $q->execute([$m[1], $m[2]]);
            if ((int)$q->fetchColumn() === 0) {
                $pdo->exec("ALTER TABLE `{$m[1]}` ADD COLUMN `{$m[2]}` {$m[3]}");
            }
        }
    }
}

function h(string $s): string
{
    return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
