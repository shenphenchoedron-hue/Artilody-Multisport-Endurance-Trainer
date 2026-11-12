#!/usr/bin/env bash
# Installed as /usr/local/sbin/artilody-power; Apache may run it with sudo
# (/etc/sudoers.d/artilody) - the web interface's Shut down / Restart.
#   artilody-power poweroff | reboot
# Waits 3 s (in the background) so the web page is answered first.
set -euo pipefail

case "${1:-}" in
    poweroff|reboot) action="$1" ;;
    *) echo "usage: artilody-power poweroff|reboot" >&2; exit 2 ;;
esac
systemd-run --quiet --on-active=3 --timer-property=AccuracySec=1s /usr/bin/systemctl "$action"
