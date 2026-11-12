#!/usr/bin/env bash
# Copies the server code from this computer to the server Pi and installs it
# there (install.sh). Needs SSH with a key (ssh-copy-id) and sudo on the Pi
# (it asks for the Pi's password when sudo has not been used there lately).
#
#   server/deploy.sh user@host        (e.g. pi@192.168.1.20)
set -euo pipefail

HOST="${1:?usage: server/deploy.sh user@host (e.g. pi@192.168.1.20)}"
DIR="$(cd "$(dirname "$0")" && pwd)"

rsync -az --delete --exclude deploy.sh "$DIR/" "$HOST:artilody-server/"
ssh -t "$HOST" 'sudo bash ~/artilody-server/install.sh'
