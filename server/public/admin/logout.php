<?php
declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/web.php';

csrf_check();
$_SESSION = [];
session_destroy();
header('Location: ' . u(''));
