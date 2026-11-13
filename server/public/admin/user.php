<?php
// Edit / create / delete a user (lib/users.php). A change made here is the
// newest one (the server's clock) - the stations take it at their next look.
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';
require __DIR__ . '/../../lib/web.php';

require_admin();
$pdo = artilody_db();

// which colours each figure has
const FIGURES = [
    'avatar3d' => ['Cyclist (GPX Ride)', ['jersey', 'shorts', 'helmet', 'shoes', 'hair', 'bike']],
    'runner3d' => ['Runner (Treadmill Run)', ['jersey', 'shorts', 'shoes', 'hair']],
    'rower3d' => ['Rower (Rowing)', ['jersey', 'shorts', 'hair', 'boat']],
];

$uuid = strtolower((string)($_GET['uuid'] ?? $_POST['uuid'] ?? ''));
$user = is_uuid($uuid) ? user_get($pdo, $uuid) : null;
if ($user && $user['deleted']) {
    $user = null;
}
$isNew = $user === null;
if ($isNew) {
    $uuid = '';
    [, $defaults] = user_validate(['name' => 'x', 'weightKg' => 75, 'ftp' => 250, 'rowingFtp' => 150,
        'maxHr' => 185, 'restHr' => 60, 'cadence' => 85, 'defaultErg' => true]);
    $user = ['name' => ''] + $defaults + ['pictureHash' => null];
}
$error = '';

if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    csrf_check();
    if (($_POST['action'] ?? '') === 'delete' && !$isNew) {
        $pdo->beginTransaction();
        [, $c] = user_validate($user);
        user_write($pdo, $uuid, $c, null, true, now_ms(), 'web');
        $pdo->commit();
        header('Location: ' . u('admin/') . '?notice=' . rawurlencode($user['name'] . ' was deleted.'));
        exit;
    }
    $in = [
        'name' => $_POST['name'] ?? '',
        'weightKg' => $_POST['weightKg'] ?? '',
        'heightCm' => ($_POST['heightCm'] ?? '') === '' ? 0 : $_POST['heightCm'],
        'ftp' => $_POST['ftp'] ?? '',
        'rowingFtp' => $_POST['rowingFtp'] ?? '',
        'maxHr' => $_POST['maxHr'] ?? '',
        'restHr' => $_POST['restHr'] ?? '',
        'cadence' => $_POST['cadence'] ?? '',
        'defaultErg' => ($_POST['defaultErg'] ?? '') === '1',
        'zones' => $user['zones'],
    ];
    foreach (FIGURES as $key => [, $colors]) {
        $a = $user[$key];   // (colours the form does not show are kept)
        $a['model'] = (string)($_POST[$key . '_model'] ?? $a['model']);
        foreach ($colors as $c) {
            $a[$c] = !empty($_POST[$key . '_' . $c . '_on']) ? (string)($_POST[$key . '_' . $c] ?? '') : '';
        }
        $in[$key] = $a;
    }
    [$error, $clean] = user_validate($in);
    if ($error === '' && user_named($pdo, $clean['name'], $isNew ? '' : $uuid)) {
        $error = 'There is already a user called ' . $clean['name'] . ' - choose another name.';
    }

    $picture = $user['pictureHash'];
    if ($error === '' && !empty($_POST['removePicture'])) {
        $picture = null;
    }
    $file = $_FILES['picture'] ?? null;
    if ($error === '' && $file && ($file['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_NO_FILE) {
        if ($file['error'] !== UPLOAD_ERR_OK || !is_uploaded_file($file['tmp_name'])) {
            $error = 'The picture could not be uploaded (at most ' . (PICTURE_MAX_BYTES >> 20) . ' MB).';
        } else {
            $hash = picture_store($pdo, (string)file_get_contents($file['tmp_name']), $error);
            if ($hash !== '') {
                $picture = $hash;
            }
        }
    }
    if ($error === '') {
        $id = $isNew ? new_uuid() : $uuid;
        $pdo->beginTransaction();
        user_write($pdo, $id, $clean, $picture, false, now_ms(), 'web');
        $pdo->commit();
        header('Location: ' . u('admin/') . '?notice=' . rawurlencode($clean['name'] . ' was saved.'));
        exit;
    }
    $user = $in + ['pictureHash' => $picture];
    foreach (FIGURES as $key => $_) {
        $user[$key] = avatar_clean($in[$key]);
    }
}

$v = fn(string $k) => h((string)$user[$k]);
page_start($isNew ? 'New user' : $user['name']);
?>
<h1><?= $isNew ? 'New user' : h($user['name']) ?></h1>
<?php if ($error !== ''): ?><div class="error"><?= h($error) ?></div><?php endif; ?>
<form method="post" enctype="multipart/form-data">
  <?= csrf_field() ?>
  <input type="hidden" name="uuid" value="<?= h($uuid) ?>">
  <div class="card">
    <div class="grid">
      <div><label for="name">Name</label><input type="text" id="name" name="name" maxlength="100" required value="<?= $v('name') ?>"></div>
      <div><label for="weightKg">Weight (kg)</label><input type="number" id="weightKg" name="weightKg" min="20" max="250" step="0.1" required value="<?= $v('weightKg') ?>"></div>
      <div><label for="heightCm">Height (cm, empty: not set)</label><input type="number" id="heightCm" name="heightCm" min="100" max="230" step="0.1" value="<?= $user['heightCm'] > 0 ? $v('heightCm') : '' ?>"></div>
      <div><label for="ftp">FTP (W)</label><input type="number" id="ftp" name="ftp" min="50" max="600" required value="<?= $v('ftp') ?>"></div>
      <div><label for="rowingFtp">Rowing FTP (W)</label><input type="number" id="rowingFtp" name="rowingFtp" min="50" max="600" required value="<?= $v('rowingFtp') ?>"></div>
      <div><label for="cadence">Preferred cadence (RPM)</label><input type="number" id="cadence" name="cadence" min="40" max="150" required value="<?= $v('cadence') ?>"></div>
      <div><label for="maxHr">Max heart rate (BPM)</label><input type="number" id="maxHr" name="maxHr" min="60" max="240" required value="<?= $v('maxHr') ?>"></div>
      <div><label for="restHr">Resting heart rate (BPM)</label><input type="number" id="restHr" name="restHr" min="30" max="120" required value="<?= $v('restHr') ?>"></div>
      <div><label for="defaultErg">Default ride mode</label>
        <select id="defaultErg" name="defaultErg">
          <option value="1" <?= $user['defaultErg'] ? 'selected' : '' ?>>ERG</option>
          <option value="0" <?= $user['defaultErg'] ? '' : 'selected' ?>>Free Ride</option>
        </select></div>
    </div>
  </div>

  <div class="card">
    <h2 style="margin-top:0">Profile picture</h2>
    <div class="row">
      <?php if ($user['pictureHash']): ?>
        <img class="pic" style="width:72px;height:72px" src="<?= h(u('api/picture.php')) ?>?hash=<?= h($user['pictureHash']) ?>" alt="">
        <label style="margin:0"><input type="checkbox" name="removePicture" value="1"> Remove</label>
      <?php else: ?>
        <span class="pic" style="width:72px;height:72px;font-size:24px"><?= h(initials($user['name'])) ?></span>
      <?php endif; ?>
      <input type="file" name="picture" accept="image/jpeg,image/png,image/webp,image/gif">
    </div>
    <p class="muted">JPEG, PNG, WebP or GIF, at most <?= PICTURE_MAX_BYTES >> 20 ?> MB.</p>
  </div>

  <?php foreach (FIGURES as $key => [$title, $colors]): $a = $user[$key]; ?>
    <div class="card">
      <h2 style="margin-top:0"><?= h($title) ?></h2>
      <div class="row" style="margin-bottom:12px">
        <label style="margin:0" for="<?= $key ?>_model">Figure</label>
        <select id="<?= $key ?>_model" name="<?= $key ?>_model" style="width:auto">
          <option value="male" <?= $a['model'] === 'male' ? 'selected' : '' ?>>Male</option>
          <option value="female" <?= $a['model'] === 'female' ? 'selected' : '' ?>>Female</option>
        </select>
      </div>
      <div class="colors">
        <?php foreach ($colors as $c): $on = $a[$c] !== ''; ?>
          <div>
            <input type="checkbox" name="<?= $key ?>_<?= $c ?>_on" value="1" <?= $on ? 'checked' : '' ?> title="Own colour (off: the figure's own)">
            <input type="color" name="<?= $key ?>_<?= $c ?>" value="<?= h($on ? $a[$c] : '#888888') ?>">
            <span><?= h(ucfirst($c)) ?></span>
          </div>
        <?php endforeach; ?>
      </div>
      <p class="muted">Ticked: this colour; not ticked: the figure's own.</p>
    </div>
  <?php endforeach; ?>

  <div class="row" style="justify-content:space-between">
    <div class="row">
      <button name="action" value="save">Save</button>
      <a class="button secondary" href="<?= h(u('admin/')) ?>">Cancel</a>
    </div>
    <?php if (!$isNew): ?>
      <button class="danger" name="action" value="delete" formnovalidate
              onclick="return confirm('Delete <?= h(addslashes($user['name'])) ?>? The stations remove the profile too (a station keeps it if it has rides with it).')">Delete user</button>
    <?php endif; ?>
  </div>
</form>
<?php
page_end();
