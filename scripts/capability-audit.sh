#!/usr/bin/env bash
set -euo pipefail

if (( EUID != 0 )); then
  printf 'ERROR capability audit requires root for administrative runtime checks\n' >&2
  exit 2
fi

fail=0
check() {
  local name="$1"
  shift
  if "$@" >/dev/null 2>&1; then
    printf 'PASS %s\n' "$name"
  else
    printf 'FAIL %s\n' "$name"
    fail=1
  fi
}

check systemd-pid1 test "$(ps -p 1 -o comm=)" = systemd
check cgroups-v2 test -f /sys/fs/cgroup/cgroup.controllers
check user-namespace unshare --user --map-root-user true
check mount-namespace unshare --mount true
check network-namespace unshare --net true
check overlay-filesystem grep -qw overlay /proc/filesystems
check seccomp-library pkg-config --exists libseccomp
check systemd-library pkg-config --exists libsystemd
check delegated-systemd-unit systemd-run --quiet --wait --collect -p Delegate=yes true
check nftables-parser nft --check --file /dev/stdin <<< 'table inet minicontainer_audit { }'

exit "$fail"
