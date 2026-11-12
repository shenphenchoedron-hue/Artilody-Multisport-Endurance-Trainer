#!/usr/bin/env php
<?php
// Installed as /usr/local/sbin/artilody-admin-password: sets the web
// interface's admin password (asked twice, not shown).
//   sudo artilody-admin-password
declare(strict_types=1);

require '/var/www/artilody/lib/bootstrap.php';

if (PHP_SAPI !== 'cli') {
    exit(1);
}

function ask(string $prompt): string
{
    fwrite(STDOUT, $prompt);
    $tty = stream_isatty(STDIN);
    if ($tty) {
        shell_exec('stty -echo');
    }
    $line = fgets(STDIN);
    if ($tty) {
        shell_exec('stty echo');
        fwrite(STDOUT, "\n");
    }
    return rtrim((string)$line, "\r\n");
}

$a = ask('New admin password: ');
if (mb_strlen($a) < 8) {
    fwrite(STDERR, "At least 8 characters.\n");
    exit(1);
}
if (ask('Again: ') !== $a) {
    fwrite(STDERR, "The two do not match.\n");
    exit(1);
}
$q = artilody_db()->prepare("INSERT INTO meta (k, v) VALUES ('admin_hash', ?) ON DUPLICATE KEY UPDATE v = VALUES(v)");
$q->execute([password_hash($a, PASSWORD_DEFAULT)]);
echo "The admin password is set.\n";
