#!/usr/bin/env bash
# Installs or updates the Artilody server on this Raspberry Pi (Raspberry Pi
# OS / Debian with Apache, PHP and MariaDB from apt). Safe to run again: the
# database, its password and the data are kept.
#
#   sudo bash install.sh        (server/deploy.sh runs it from the Mac)
#
# What it does:
#   - code to /var/www/artilody (public/ is the web root, lib/ is not served)
#   - database "artilody" with its own user "artilody" (password made once,
#     kept in /etc/artilody/config.php, readable by root and Apache only)
#   - schema.sql applied (additive)
#   - Apache: the Artilody site replaces the default page
#   - MariaDB only on this machine (127.0.0.1): the stations talk to PHP,
#     never to the database directly
#   - backup of the database at start-up and proper shut-down
#   - the media library's folders /var/lib/artilody/{blobs,uploads}
#   - the admin tools: artilody-admin-password (the web interface's password)
#     and artilody-power (the web interface's Shut down / Restart, via sudo)
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
APP=/var/www/artilody
CONF_DIR=/etc/artilody
CONF="$CONF_DIR/config.php"
DB=artilody
DB_USER=artilody

if [ "$(id -u)" != 0 ]; then
    echo "Run as root: sudo bash $0" >&2
    exit 1
fi

step() { echo "==> $*"; }

step "Packages"
missing=()
for p in apache2 mariadb-server php libapache2-mod-php php-mysql php-mbstring curl; do
    dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p")
done
if [ ${#missing[@]} -gt 0 ]; then
    apt-get update
    apt-get install -y "${missing[@]}"
fi

step "Code -> $APP"
mkdir -p "$APP"
rm -rf "$APP/public.new" "$APP/lib.new"
cp -r "$SRC/public" "$APP/public.new"
cp -r "$SRC/lib" "$APP/lib.new"
rm -rf "$APP/public" "$APP/lib"
mv "$APP/public.new" "$APP/public"
mv "$APP/lib.new" "$APP/lib"
cp "$SRC/schema.sql" "$APP/schema.sql"
chown -R root:root "$APP"
find "$APP" -type d -exec chmod 755 {} +
find "$APP" -type f -exec chmod 644 {} +

step "Configuration $CONF"
mkdir -p "$CONF_DIR"
chmod 755 "$CONF_DIR"
if [ ! -f "$CONF" ]; then
    pass="$(php -r 'echo bin2hex(random_bytes(20));')"
    # (made 640 root:www-data before the password goes in: never readable by others)
    install -m 640 -o root -g www-data /dev/null "$CONF"
    cat > "$CONF" <<EOF
<?php
// Artilody server configuration - made by install.sh, not in git.
return [
    'name' => 'Artilody server',
    'dsn' => 'mysql:unix_socket=/run/mysqld/mysqld.sock;dbname=$DB;charset=utf8mb4',
    'user' => '$DB_USER',
    'password' => '$pass',
];
EOF
fi
chown root:www-data "$CONF"
chmod 640 "$CONF"
pass="$(php -r '$c = require $argv[1]; echo $c["password"];' "$CONF")"

step "Database $DB"
mariadb <<SQL
CREATE DATABASE IF NOT EXISTS \`$DB\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED BY '$pass';
ALTER USER '$DB_USER'@'localhost' IDENTIFIED BY '$pass';
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES, DROP ON \`$DB\`.* TO '$DB_USER'@'localhost';
FLUSH PRIVILEGES;
SQL
mariadb "$DB" < "$APP/schema.sql"

step "MariaDB only on this machine"
CNF=/etc/mysql/mariadb.conf.d/60-artilody.cnf
want=$'# Artilody (install.sh): the database only on this machine - the stations talk to PHP\n[mysqld]\nbind-address = 127.0.0.1\n'
if [ ! -f "$CNF" ] || [ "$(cat "$CNF")"$'\n' != "$want" ]; then
    printf '%s' "$want" > "$CNF"
    systemctl restart mariadb
fi

step "PHP: profile pictures up to 4 MB, uploads in 8 MB pieces"
PHPV="$(php -r 'echo PHP_MAJOR_VERSION . "." . PHP_MINOR_VERSION;')"
if [ -d "/etc/php/$PHPV/apache2/conf.d" ]; then
    printf '; Artilody (install.sh)\nupload_max_filesize = 16M\npost_max_size = 16M\nmemory_limit = 256M\n' \
        > "/etc/php/$PHPV/apache2/conf.d/99-artilody.ini"
fi

step "Media library folders"
# the files (by sha1, served at /blobs/) and unfinished uploads - Apache writes both
install -d -m 755 -o www-data -g www-data /var/lib/artilody /var/lib/artilody/blobs
install -d -m 750 -o www-data -g www-data /var/lib/artilody/uploads

step "Admin tools"
install -m 755 "$SRC/admin-password.php" /usr/local/sbin/artilody-admin-password
install -m 755 "$SRC/power.sh" /usr/local/sbin/artilody-power
# Apache (www-data) may shut down / restart the server: the web interface's button (admin only)
SUDOERS=/etc/sudoers.d/artilody
printf '# Artilody (install.sh): the web interface may shut the server down\nwww-data ALL=(root) NOPASSWD: /usr/local/sbin/artilody-power poweroff, /usr/local/sbin/artilody-power reboot\n' \
    > "$SUDOERS.new"
chmod 440 "$SUDOERS.new"
visudo -cq -f "$SUDOERS.new"
mv "$SUDOERS.new" "$SUDOERS"

step "Apache site"
cp "$SRC/apache/artilody.conf" /etc/apache2/sites-available/artilody.conf
a2enmod -q headers >/dev/null
a2ensite -q artilody >/dev/null
a2dissite -q 000-default >/dev/null 2>&1 || true
apache2ctl configtest
systemctl restart apache2   # (restart: the PHP limits and modules)

step "Backup at start-up and shut-down"
install -m 755 "$SRC/backup.sh" /usr/local/sbin/artilody-backup
install -m 644 "$SRC/systemd/artilody-backup.service" /etc/systemd/system/artilody-backup.service
systemctl daemon-reload
systemctl enable artilody-backup.service >/dev/null 2>&1
systemctl is-active --quiet artilody-backup.service || systemctl start artilody-backup.service

step "Check"
find "$APP" -name '*.php' -print0 | xargs -0 -n1 php -l >/dev/null
answer="$(curl -fsS -m 5 http://127.0.0.1/api/ping.php || true)"
echo "    ping: ${answer:-no answer}"
case "$answer" in
    *'"db":true'*) echo "    OK - http://$(hostname -I | awk '{print $1}')/" ;;
    *) echo "    the server or its database does not answer" >&2; exit 1 ;;
esac
if [ -z "$(mariadb -N "$DB" -e "SELECT v FROM meta WHERE k = 'admin_hash'")" ]; then
    if [ -t 0 ]; then
        echo
        echo "==> The web interface's admin password (you need it to manage users, media and the server)"
        until /usr/local/sbin/artilody-admin-password; do echo "    Try again."; done
    else
        echo "    No admin password yet - set it with: sudo artilody-admin-password"
    fi
fi
echo
echo "    The stations: Artilody > Settings > Server > on, address: $(hostname -I | awk '{print $1}')"

