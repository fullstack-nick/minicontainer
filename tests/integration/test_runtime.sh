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
if [[ -z "${MC_TEST_INHERIT_PROC:-}" ]]; then
  grep -q '^UID_MAP=' <<< "$output"
grep -q ' /proc proc rw,nosuid,nodev,noexec' <<< "$output"
fi

export HOST_ONLY_SHOULD_DISAPPEAR=yes
set +e
# The single-quoted program is intentionally expanded by the shell inside the container.
# shellcheck disable=SC2016
contract="$("$binary" run --image alpine-runtime --env DEMO=value --workdir /tmp \
  --user 1234:2345 -- /bin/sh -c '
    printf "DEMO=%s\n" "$DEMO"
    printf "WORKDIR=%s\n" "$PWD"
    printf "IDENTITY=%s:%s\n" "$(id -u)" "$(id -g)"
    test -z "${HOST_ONLY_SHOULD_DISAPPEAR+x}"
    touch user-write
')"
status=$?
set -e
if [[ "$status" -ne 0 ]]; then
  printf 'workload contract exited with %s\n' "$status" >&2
  exit 1
fi
grep -q '^DEMO=value$' <<< "$contract"
grep -q '^WORKDIR=/tmp$' <<< "$contract"
grep -q '^IDENTITY=1234:2345$' <<< "$contract"

set +e
"$binary" run --image alpine-runtime -- /bin/sh -c 'exit 42'
status=$?
set -e
test "$status" -eq 42

signal_output="$workspace/signal.out"
"$binary" run --image alpine-runtime -- /bin/sh -c \
  'trap "exit 33" TERM; echo READY; while :; do sleep 1; done' > "$signal_output" &
launcher=$!
for _ in $(seq 1 50); do
  grep -q '^READY$' "$signal_output" 2>/dev/null && break
  sleep 0.1
done
grep -q '^READY$' "$signal_output"
kill -TERM "$launcher"
set +e
wait "$launcher"
status=$?
set -e
test "$status" -eq 33

if [[ -d "$workspace/state/containers" ]]; then
  test -z "$(find "$workspace/state/containers" -mindepth 1 -print -quit)"
fi
printf 'PASS isolated foreground runtime and cleanup\n'
