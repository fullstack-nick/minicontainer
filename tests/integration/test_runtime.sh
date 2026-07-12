#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
if (( EUID != 0 )); then
  printf 'runtime integration test requires root\n' >&2
  exit 2
fi

workspace="$(mktemp -d /tmp/minicontainer-runtime-test.XXXXXX)"
chmod 0755 "$workspace"
cleanup() {
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT
export MC_STATE_DIR="$workspace/state"
export MC_SHIM_PATH="$shim"

"$binary" image import alpine-runtime "$archive" > /dev/null
# The single-quoted program is intentionally expanded by the shell inside the container.
# shellcheck disable=SC2016
output="$("$binary" run --image alpine-runtime --hostname isolated-host -- /bin/sh -c '
  printf "HOST=%s\n" "$(hostname)"
  printf "PID=%s\n" "$$"
  printf "PPID=%s\n" "$PPID"
  printf "UID=%s\n" "$(id -u)"
  printf "UID_MAP="; cat /proc/self/uid_map
  grep " /proc " /proc/mounts
  touch /stage1-write
  test -f /stage1-write
')"
grep -q '^HOST=isolated-host$' <<< "$output"
grep -q '^PID=2$' <<< "$output"
grep -q '^PPID=1$' <<< "$output"
grep -q '^UID=0$' <<< "$output"
grep -q '^UID_MAP=' <<< "$output"
grep -q ' /proc proc rw,nosuid,nodev,noexec' <<< "$output"

set +e
"$binary" run --image alpine-runtime -- /bin/sh -c 'exit 42'
status=$?
set -e
test "$status" -eq 42

if [[ -d "$workspace/state/containers" ]]; then
  test -z "$(find "$workspace/state/containers" -mindepth 1 -print -quit)"
fi
printf 'PASS isolated foreground runtime and cleanup\n'
