#!/usr/bin/env bash
# Backup of the Artilody server's database, installed as
# /usr/local/sbin/artilody-backup. Run by artilody-backup.service when the
# server starts and when it is shut down properly (the server only runs
# during training: no nightly jobs). Keeps the newest $KEEP backups.
#   sudo artilody-backup [label]
set -euo pipefail

DIR=/var/backups/artilody
KEEP=30
LABEL="${1:-manual}"

mkdir -p "$DIR"
chmod 700 "$DIR"
file="$DIR/artilody-$(date +%Y%m%d-%H%M%S)-$LABEL.sql.gz"
mariadb-dump --single-transaction --routines --databases artilody | gzip > "$file.tmp"
mv "$file.tmp" "$file"
# (ls lists newest first; everything after the newest $KEEP goes)
ls -1t "$DIR"/artilody-*.sql.gz | tail -n +$((KEEP + 1)) | xargs -r rm -f --
echo "artilody-backup: $file"
