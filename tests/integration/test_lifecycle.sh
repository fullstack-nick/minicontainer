#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
(( EUID == 0 )) || { printf 'lifecycle integration test requires root\n' >&2; exit 2; }

workspace="$(mktemp -d /tmp/minicontainer-lifecycle-test.XXXXXX)"
chmod 0755 "$workspace"
trap 'chmod -R u+w "$workspace" 2>/dev/null || true; rm -rf -- "$workspace"' EXIT
export MC_STATE_DIR="$workspace/state"
export MC_LOG_DIR="$workspace/logs"
export MC_SHIM_PATH="$shim"

"$binary" image import alpine-lifecycle "$archive" >/dev/null
id="$("$binary" create --name durable-demo --image alpine-lifecycle -- /bin/sh -c \
  'if [ -f /persist ]; then echo SECOND; else echo FIRST; touch /persist; fi')"
[[ "$id" =~ ^[0-9a-f]{32}$ ]]
grep -q '"status":"created"' "$MC_STATE_DIR/containers/$id/state.json"
test -f "$MC_STATE_DIR/containers/$id/config.json"
"$binary" inspect durable-demo | grep -q '"status":"created"'
"$binary" ps --all --json | grep -q '"name":"durable-demo"'

started="$("$binary" start durable-demo)"
[[ "$started" == "$id" ]]
for _ in $(seq 1 100); do
  grep -q '"status":"stopped"' "$MC_STATE_DIR/containers/$id/state.json" && break
  sleep 0.05
done
grep -q '^FIRST$' "$MC_LOG_DIR/$id/container.log"

"$binary" start "${id:0:12}" >/dev/null
for _ in $(seq 1 100); do
  [[ "$(grep -c '^SECOND$' "$MC_LOG_DIR/$id/container.log" 2>/dev/null || true)" -ge 1 ]] && break
  sleep 0.05
done
grep -q '^SECOND$' "$MC_LOG_DIR/$id/container.log"

signal_id="$("$binary" create --name signal-demo --image alpine-lifecycle -- /bin/sh -c \
  'trap "exit 33" TERM; echo READY; while :; do sleep 1; done')"
"$binary" start signal-demo >/dev/null
for _ in $(seq 1 100); do
  grep -q '^READY$' "$MC_LOG_DIR/$signal_id/container.log" 2>/dev/null && break
  sleep 0.05
done
"$binary" kill --signal TERM signal-demo
for _ in $(seq 1 100); do
  grep -q '"exit_code":33' "$MC_STATE_DIR/containers/$signal_id/state.json" && break
  sleep 0.05
done
grep -q '"exit_code":33' "$MC_STATE_DIR/containers/$signal_id/state.json"

force_id="$("$binary" create --name force-demo --image alpine-lifecycle -- /bin/sh -c \
  'trap "" TERM; echo READY; while :; do sleep 1; done')"
"$binary" start force-demo >/dev/null
for _ in $(seq 1 100); do
  grep -q '^READY$' "$MC_LOG_DIR/$force_id/container.log" 2>/dev/null && break
  sleep 0.05
done
"$binary" stop --time 1 force-demo
for _ in $(seq 1 100); do
  grep -q '"exit_code":137' "$MC_STATE_DIR/containers/$force_id/state.json" && break
  sleep 0.05
done
grep -q '"exit_code":137' "$MC_STATE_DIR/containers/$force_id/state.json"

set +e
"$binary" create --name durable-demo --image alpine-lifecycle -- /bin/true >/dev/null 2>&1
duplicate_status=$?
set -e
[[ "$duplicate_status" -ne 0 ]]

rmforce_id="$("$binary" create --name rmforce-demo --image alpine-lifecycle -- /bin/sh -c \
  'trap "" TERM; while :; do sleep 1; done')"
"$binary" start rmforce-demo >/dev/null
"$binary" rm --force rmforce-demo
test ! -e "$MC_STATE_DIR/containers/$rmforce_id"
test ! -e "$MC_LOG_DIR/$rmforce_id"
"$binary" rm durable-demo
test ! -e "$MC_STATE_DIR/containers/$id"

printf 'PASS lifecycle persistence, resolution, name conflicts, pidfd signals, stop, and rm\n'
