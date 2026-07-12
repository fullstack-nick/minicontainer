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
export MC_RUNTIME_DIR="$workspace/run"

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
[[ "$("$binary" logs --tail 1 durable-demo)" == "SECOND" ]]

follow_id="$("$binary" create --name follow-demo --image alpine-lifecycle -- /bin/sh -c \
  'echo FOLLOW_A; sleep 0.2; echo FOLLOW_B')"
"$binary" start follow-demo >/dev/null
follow_output="$("$binary" logs --follow follow-demo)"
grep -q '^FOLLOW_A$' <<<"$follow_output"
grep -q '^FOLLOW_B$' <<<"$follow_output"

signal_id="$("$binary" create --name signal-demo --image alpine-lifecycle -- /bin/sh -c \
  'trap "exit 33" TERM; echo READY; while :; do sleep 1; done')"
"$binary" start signal-demo >/dev/null
for _ in $(seq 1 100); do
  grep -q '^READY$' "$MC_LOG_DIR/$signal_id/container.log" 2>/dev/null && break
  sleep 0.05
done
control="$MC_RUNTIME_DIR/$signal_id/control.sock"
[[ "$(stat -c %a "$control")" == 600 ]]
chmod 0711 "$MC_RUNTIME_DIR" "$MC_RUNTIME_DIR/$signal_id"
chmod 0666 "$control"
peer_response="$(runuser -u nobody -- python3 - "$control" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.connect(sys.argv[1])
s.sendall(b'{"version":1,"operation":"signal","signal":15}')
print(s.recv(256).decode())
PY
)"
grep -q '"ok":false' <<<"$peer_response"
grep -q 'permission denied' <<<"$peer_response"
chmod 0700 "$MC_RUNTIME_DIR" "$MC_RUNTIME_DIR/$signal_id"
chmod 0600 "$control"
grep -q '"status":"running"' "$MC_STATE_DIR/containers/$signal_id/state.json"
grep -Eq '"namespaces":\{"user":[1-9][0-9]*' "$MC_STATE_DIR/containers/$signal_id/state.json"
exec_output="$("$binary" exec signal-demo -- /bin/sh -c \
  'echo EXEC_OK; echo PID=$$; hostname; for n in user pid mnt uts ipc net cgroup; do printf "%s=" "$n"; readlink "/proc/self/ns/$n"; done; touch /exec-persist')"
grep -q '^EXEC_OK$' <<<"$exec_output"
grep -Eq '^PID=[2-9][0-9]*$' <<<"$exec_output"
[[ "$(sed -n '3p' <<<"$exec_output")" == "mc-${signal_id:0:12}" ]]
init_pid="$(sed -n 's/.*"init_pid":\([0-9]*\).*/\1/p' \
  "$MC_STATE_DIR/containers/$signal_id/state.json")"
for namespace in user pid mnt uts ipc net cgroup; do
  exec_namespace="$(sed -n "s/^$namespace=//p" <<<"$exec_output")"
  [[ "$exec_namespace" == "$(readlink "/proc/$init_pid/ns/$namespace")" ]]
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

crash_id="$("$binary" create --name crash-demo --image alpine-lifecycle -- /bin/sh -c \
  'echo READY; while :; do sleep 1; done')"
"$binary" start crash-demo >/dev/null
for _ in $(seq 1 100); do
  grep -q '^READY$' "$MC_LOG_DIR/$crash_id/container.log" 2>/dev/null && break
  sleep 0.05
done
crash_shim="$(sed -n 's/.*"shim_pid":\([0-9]*\).*/\1/p' \
  "$MC_STATE_DIR/containers/$crash_id/state.json")"
kill -KILL "$crash_shim"
sleep 0.2
"$binary" gc | grep -q 'reconciled=1'
grep -q '"status":"stopped"' "$MC_STATE_DIR/containers/$crash_id/state.json"
grep -q '"exit_code":125' "$MC_STATE_DIR/containers/$crash_id/state.json"
"$binary" start crash-demo >/dev/null
"$binary" stop --time 1 crash-demo

concurrent_id="$("$binary" create --name concurrent-demo --image alpine-lifecycle -- \
  /bin/sh -c 'while :; do sleep 1; done')"
set +e
"$binary" start concurrent-demo >"$workspace/start-a.out" 2>"$workspace/start-a.err" & start_a=$!
"$binary" start concurrent-demo >"$workspace/start-b.out" 2>"$workspace/start-b.err" & start_b=$!
wait "$start_a"; status_a=$?
wait "$start_b"; status_b=$?
set -e
[[ "$status_a" -eq 0 && "$status_b" -ne 0 || "$status_b" -eq 0 && "$status_a" -ne 0 ]]
grep -q '"status":"running"' "$MC_STATE_DIR/containers/$concurrent_id/state.json"
"$binary" stop --time 1 concurrent-demo

"$binary" run --name cli-crash-demo --image alpine-lifecycle -- /bin/sh -c \
  'echo READY; while :; do sleep 1; done' >"$workspace/cli-crash.out" 2>&1 &
cli_pid=$!
for _ in $(seq 1 100); do
  cli_crash_id="$(find "$MC_STATE_DIR/containers" -name config.json -exec grep -l \
    '"name":"cli-crash-demo"' {} \; | sed -n 's#.*/containers/\([^/]*\)/config.json#\1#p')"
  [[ -n "$cli_crash_id" ]] && grep -q '^READY$' "$workspace/cli-crash.out" 2>/dev/null && break
  sleep 0.05
done
kill -KILL "$cli_pid"
set +e; wait "$cli_pid"; set -e
grep -q '"status":"running"' "$MC_STATE_DIR/containers/$cli_crash_id/state.json"
"$binary" stop --time 1 cli-crash-demo
grep -q '"status":"stopped"' "$MC_STATE_DIR/containers/$cli_crash_id/state.json"
"$binary" rm cli-crash-demo

printf 'PASS lifecycle, exec, pidfd control, forced stop, rm, crash reconciliation, and restart\n'
