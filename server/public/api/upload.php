<?php
// Uploads in pieces (lib/media.php) - from the web interface and the stations:
//   POST /api/upload.php?action=start&size=<bytes>            -> {"id":"..."}
//   POST /api/upload.php?action=chunk&id=..&offset=<n>  (body: up to 8 MB)
//        -> {"received":<n>}; a wrong offset: 409 {"received":<n>} (go on from there)
//   POST /api/upload.php?action=finish&id=..                  -> {"hash":"<sha1>","size":<n>}
// Then the item (api/media.php) names the file by its hash. A file already on
// the server needs no upload: HEAD /blobs/<sha1> answers 200.
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/media.php';

require_station_header();

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    header('Allow: POST');
    json_out(['error' => 'method not allowed'], 405);
}
$action = (string)($_GET['action'] ?? '');
$id = (string)($_GET['id'] ?? '');
$error = '';
try {
    if ($action === 'start') {
        $id = upload_start((int)($_GET['size'] ?? 0), $error);
        $id === '' ? json_out(['error' => $error], 400) : json_out(['id' => $id]);
    }
    if ($action === 'chunk') {
        $data = file_get_contents('php://input', false, null, 0, MEDIA_CHUNK_MAX_BYTES + 1);
        $received = upload_chunk($id, (int)($_GET['offset'] ?? -1), $data === false ? '' : $data, $error);
        if ($error === 'offset') {
            json_out(['error' => 'offset', 'received' => $received], 409);
        }
        $received < 0 ? json_out(['error' => $error], 400) : json_out(['received' => $received]);
    }
    if ($action === 'finish') {
        [$hash, $size] = upload_finish($id, $error);
        $hash === '' ? json_out(['error' => $error], 400) : json_out(['hash' => $hash, 'size' => $size]);
    }
    json_out(['error' => 'action must be start, chunk or finish'], 400);
} catch (Throwable $e) {
    error_log('artilody upload: ' . $e->getMessage());
    json_out(['error' => 'server error'], 500);
}
