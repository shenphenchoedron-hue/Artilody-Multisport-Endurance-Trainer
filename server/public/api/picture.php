<?php
// Profile pictures (lib/users.php):
//   GET  /api/picture.php?hash=<sha1>  -> the picture
//   POST /api/picture.php  (the picture's bytes as the body)  -> {"hash":"<sha1>"}
//        a station sends a profile's picture before the user that shows it
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';

require_station_header();

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
try {
    $pdo = artilody_db();
    if ($method === 'GET' || $method === 'HEAD') {
        $hash = strtolower((string)($_GET['hash'] ?? ''));
        $p = preg_match('/^[0-9a-f]{40}$/', $hash) ? picture_get($pdo, $hash) : null;
        if (!$p) {
            json_out(['error' => 'no such picture'], 404);
        }
        header('Content-Type: ' . $p['type']);
        header('Content-Length: ' . strlen($p['data']));
        header('Cache-Control: public, max-age=31536000, immutable');   // (by its hash: never changes)
        header('X-Content-Type-Options: nosniff');
        echo $p['data'];
        exit;
    }
    if ($method === 'POST') {
        $data = file_get_contents('php://input', false, null, 0, PICTURE_MAX_BYTES + 1);
        $error = '';
        $hash = picture_store($pdo, $data === false ? '' : $data, $error);
        if ($hash === '') {
            json_out(['error' => $error], 400);
        }
        json_out(['hash' => $hash]);
    }
    header('Allow: GET, HEAD, POST');
    json_out(['error' => 'method not allowed'], 405);
} catch (Throwable $e) {
    error_log('artilody picture: ' . $e->getMessage());
    json_out(['error' => 'server error'], 500);
}
