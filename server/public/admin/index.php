<?php
// The users (lib/users.php): the list, with links to edit / create one.
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/users.php';
require __DIR__ . '/../../lib/web.php';

require_admin();
$users = array_values(array_filter(users_pull(artilody_db(), 0), fn($u) => !$u['deleted']));
usort($users, fn($a, $b) => strcasecmp($a['name'], $b['name']));
$notice = (string)($_GET['notice'] ?? '');

page_start('Users');
?>
<div class="row" style="justify-content:space-between">
  <h1>Users</h1>
  <a class="button" href="<?= h(u('admin/user.php')) ?>">+ New user</a>
</div>
<?php if ($notice !== ''): ?><div class="notice"><?= h($notice) ?></div><?php endif; ?>
<div class="card">
  <?php if (!$users): ?>
    <p class="muted">No users yet. A station sends its profiles the first time it connects (Settings › Server), or create one here.</p>
  <?php else: ?>
    <table>
      <tr><th></th><th>Name</th><th>Weight</th><th>FTP</th><th>W/kg</th><th>Rowing FTP</th><th>Changed</th></tr>
      <?php foreach ($users as $u): ?>
        <tr>
          <td>
            <?php if ($u['pictureHash']): ?>
              <img class="pic" src="<?= h(u('api/picture.php')) ?>?hash=<?= h($u['pictureHash']) ?>" alt="">
            <?php else: ?>
              <span class="pic"><?= h(initials($u['name'])) ?></span>
            <?php endif; ?>
          </td>
          <td><a href="<?= h(u('admin/user.php')) ?>?uuid=<?= h($u['uuid']) ?>"><?= h($u['name']) ?></a></td>
          <td><?= h(rtrim(rtrim(number_format($u['weightKg'], 1, '.', ''), '0'), '.')) ?> kg</td>
          <td><?= (int)$u['ftp'] ?> W</td>
          <td><?= $u['weightKg'] > 0 ? number_format($u['ftp'] / $u['weightKg'], 2) : '–' ?></td>
          <td><?= (int)$u['rowingFtp'] ?> W</td>
          <td class="muted"><?= h(gmdate('Y-m-d H:i', intdiv($u['updatedMs'], 1000))) ?> UTC · <?= h($u['updatedBy']) ?></td>
        </tr>
      <?php endforeach; ?>
    </table>
  <?php endif; ?>
</div>
<?php
page_end();
