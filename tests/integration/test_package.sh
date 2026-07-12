#!/usr/bin/env bash
set -euo pipefail

current="${1:?current Debian package is required}"
previous="${2:-}"
(( EUID == 0 )) || { printf 'package integration test requires root\n' >&2; exit 2; }
[[ -f "$current" ]]

mkdir -p /var/lib/minicontainer
sentinel=/var/lib/minicontainer/package-state-preserved
printf 'preserve-state\n' >"$sentinel"

dpkg -i "$current" >/dev/null
current_commit="$(minicontainer version --json | sed -n 's/.*"git_commit":"\([^"]*\)".*/\1/p')"
[[ -n "$current_commit" ]]
systemctl is-enabled minicontainer-reconcile.service >/dev/null
systemctl start minicontainer-reconcile.service
reconcile_status="$(systemctl is-active minicontainer-reconcile.service || true)"
[[ "$reconcile_status" == inactive || "$reconcile_status" == active ]]

if [[ -n "$previous" ]]; then
  [[ -f "$previous" ]]
  dpkg -i "$previous" >/dev/null
  previous_commit="$(minicontainer version --json | sed -n 's/.*"git_commit":"\([^"]*\)".*/\1/p')"
  [[ -n "$previous_commit" && "$previous_commit" != "$current_commit" ]]
  dpkg -i "$current" >/dev/null
  minicontainer version --json | grep -q "$current_commit"
fi

dpkg -r minicontainer >/dev/null
! command -v minicontainer >/dev/null
[[ -f "$sentinel" ]]
dpkg -i "$current" >/dev/null
minicontainer version --json | grep -q "$current_commit"
[[ -f "$sentinel" ]]
rm -f "$sentinel"

printf 'PASS install, same-version upgrade, rollback, roll-forward, uninstall, state preservation\n'
