<?php
// The media library (lib/media.php): the GPX and video routes, upload (in
// 8 MB pieces from the browser, so videos of several GB work), rename, delete.
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';
require __DIR__ . '/../../lib/media.php';
require __DIR__ . '/../../lib/web.php';

require_admin();
$pdo = artilody_db();
$notice = (string)($_GET['notice'] ?? '');
$error = '';

if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    csrf_check();
    $uuid = strtolower((string)($_POST['uuid'] ?? ''));
    $item = is_uuid($uuid) ? media_get($pdo, $uuid) : null;
    $action = (string)($_POST['action'] ?? '');
    if ($item && $action === 'delete' && media_delete($pdo, $uuid)) {
        header('Location: ' . u('admin/media.php') . '?notice=' . rawurlencode($item['name'] . ' was deleted from the server.'));
        exit;
    }
    if ($item && $action === 'rename' && media_rename($pdo, $uuid, (string)($_POST['name'] ?? ''))) {
        header('Location: ' . u('admin/media.php') . '?notice=' . rawurlencode('Renamed.'));
        exit;
    }
    $error = 'That did not work - reload the page and try again.';
}

$items = array_values(array_filter(media_pull($pdo, 0), fn($m) => !$m['deleted']));
usort($items, fn($a, $b) => [$a['kind'], strcasecmp($a['name'], $b['name'])] <=> [$b['kind'], 0]);
$free = @disk_free_space(blobs_dir());

page_start('Media');
?>
<h1>Media library</h1>
<?php if ($notice !== ''): ?><div class="notice"><?= h($notice) ?></div><?php endif; ?>
<?php if ($error !== ''): ?><div class="error"><?= h($error) ?></div><?php endif; ?>

<div class="card">
  <h2 style="margin-top:0">Upload</h2>
  <div class="row">
    <label style="margin:0"><input type="radio" name="kind" value="gpx" checked> GPX route</label>
    <label style="margin:0"><input type="radio" name="kind" value="video"> Video route</label>
  </div>
  <p class="muted" id="kindHelp"></p>
  <div class="grid">
    <div><label for="upName">Name (empty: from the file)</label><input type="text" id="upName" maxlength="200"></div>
    <div><label for="upFiles">Files</label><input type="file" id="upFiles" multiple></div>
  </div>
  <p class="row"><button id="upButton" type="button">Upload</button>
    <span id="upStatus" class="muted"></span></p>
  <div style="background:#1b2a6b;border-radius:6px;height:10px;overflow:hidden;display:none" id="upBarBox">
    <div id="upBar" style="background:#5fd38d;height:10px;width:0"></div></div>
  <?php if ($free !== false): ?><p class="muted">Free on the server: <?= h(human_size((int)$free)) ?></p><?php endif; ?>
</div>

<div class="card">
  <h2 style="margin-top:0">On the server</h2>
  <?php if (!$items): ?>
    <p class="muted">Nothing yet. Upload here, or on a station: the route menu › Send to server.</p>
  <?php else: ?>
    <table>
      <tr><th>Kind</th><th>Name</th><th>Files</th><th>Size</th><th>From</th><th></th></tr>
      <?php foreach ($items as $m): ?>
        <tr>
          <td><?= $m['kind'] === 'video' ? 'Video' : 'GPX' ?></td>
          <td>
            <form method="post" class="row" style="margin:0;gap:6px">
              <?= csrf_field() ?><input type="hidden" name="uuid" value="<?= h($m['uuid']) ?>">
              <input type="text" name="name" value="<?= h($m['name']) ?>" maxlength="200" style="width:220px">
              <button class="secondary" name="action" value="rename" style="padding:6px 10px">Rename</button>
            </form>
          </td>
          <td class="muted"><?= h(implode(', ', array_column($m['files'], 'name'))) ?></td>
          <td><?= h(human_size($m['sizeBytes'])) ?></td>
          <td class="muted"><?= h($m['uploadedBy']) ?><br><?= h(gmdate('Y-m-d', intdiv($m['updatedMs'], 1000))) ?></td>
          <td>
            <form method="post" style="margin:0">
              <?= csrf_field() ?><input type="hidden" name="uuid" value="<?= h($m['uuid']) ?>">
              <button class="danger" name="action" value="delete" style="padding:6px 10px"
                      onclick="return confirm('Delete <?= h(addslashes($m['name'])) ?> from the server? The stations keep their copies.')">Delete</button>
            </form>
          </td>
        </tr>
      <?php endforeach; ?>
    </table>
  <?php endif; ?>
</div>

<script>
(() => {
  const CHUNK = <?= MEDIA_CHUNK_MAX_BYTES ?>;
  const BASE = <?= json_encode(base_path(), JSON_HEX_TAG | JSON_UNESCAPED_SLASHES) ?>;
  const help = {
    gpx: 'One .gpx file. The stations fetch GPX routes by themselves.',
    video: 'One video (mp4, m4v, mov, mkv, avi, webm) and its .gpx (optionally its .xml). The stations fetch videos when asked (their library › Download).'
  };
  const $ = (id) => document.getElementById(id);
  const kind = () => document.querySelector('input[name=kind]:checked').value;
  const showHelp = () => { $('kindHelp').textContent = help[kind()]; };
  document.querySelectorAll('input[name=kind]').forEach((r) => r.addEventListener('change', showHelp));
  showHelp();

  async function post(url, body) {
    const r = await fetch(url, { method: 'POST', body, headers: { 'Content-Type': 'application/octet-stream', 'X-Artilody-Web': '1' } });
    const j = await r.json().catch(() => ({}));
    return { ok: r.ok, status: r.status, j };
  }

  async function uploadFile(file, done, total) {
    let r = await post(BASE + 'api/upload.php?action=start&size=' + file.size, '');
    if (!r.ok) throw new Error(file.name + ': ' + (r.j.error || r.status));
    const id = r.j.id;
    let offset = 0;
    while (offset < file.size) {
      r = await post(BASE + 'api/upload.php?action=chunk&id=' + id + '&offset=' + offset, file.slice(offset, offset + CHUNK));
      if (r.status === 409) { offset = r.j.received; continue; }
      if (!r.ok) throw new Error(file.name + ': ' + (r.j.error || r.status));
      offset = r.j.received;
      const pct = (done + offset) / total * 100;
      $('upBar').style.width = pct.toFixed(1) + '%';
      $('upStatus').textContent = file.name + ' – ' + pct.toFixed(0) + ' %';
    }
    $('upStatus').textContent = file.name + ' – checking …';
    r = await post(BASE + 'api/upload.php?action=finish&id=' + id, '');
    if (!r.ok) throw new Error(file.name + ': ' + (r.j.error || r.status));
    return { name: file.name, hash: r.j.hash };
  }

  $('upButton').addEventListener('click', async () => {
    const files = Array.from($('upFiles').files);
    if (!files.length) { $('upStatus').textContent = 'Choose the file(s) first.'; return; }
    $('upButton').disabled = true;
    $('upBarBox').style.display = 'block';
    const total = files.reduce((s, f) => s + f.size, 0);
    try {
      const sent = [];
      let done = 0;
      for (const f of files) { sent.push(await uploadFile(f, done, total)); done += f.size; }
      const name = $('upName').value.trim();
      const r = await fetch(BASE + 'api/media.php', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Artilody-Web': '1' },
        body: JSON.stringify({ kind: kind(), name, folder: name, station: 'web', files: sent }) });
      const j = await r.json().catch(() => ({}));
      if (!r.ok) throw new Error(j.error || r.status);
      location.href = BASE + 'admin/media.php?notice=' + encodeURIComponent(j.item.name + (j.created ? ' was uploaded.' : ' is already on the server.'));
    } catch (e) {
      $('upStatus').textContent = 'Not uploaded: ' + e.message;
      $('upStatus').className = 'bad';
      $('upButton').disabled = false;
    }
  });
})();
</script>
<?php
page_end();
