#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
fork_pressure_binary="${4:-}"
(( EUID == 0 )) || { printf 'cgroup integration test requires root\n' >&2; exit 2; }

workspace="$(mktemp -d /tmp/minicontainer-cgroup-test.XXXXXX)"
chmod 0755 "$workspace"
cleanup() {
  if [[ -n "${shim_pid:-}" ]]; then kill -TERM "$shim_pid" 2>/dev/null || true; fi
  if [[ -n "${oom_shim:-}" ]]; then kill -TERM "$oom_shim" 2>/dev/null || true; fi
  if [[ -n "${pids_shim:-}" ]]; then kill -TERM "$pids_shim" 2>/dev/null || true; fi
  sleep 0.2
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT
export MC_STATE_DIR="$workspace/state"
export MC_LOG_DIR="$workspace/logs"
export MC_SHIM_PATH="$shim"

"$binary" image import alpine-cgroup "$archive" >/dev/null
digest="$(sed 's/^sha256://' "$MC_STATE_DIR/images/names/alpine-cgroup")"
rootfs="$MC_STATE_DIR/images/sha256/$digest/rootfs"
if [[ -n "$fork_pressure_binary" ]]; then
  cp "$fork_pressure_binary" "$workspace/fork-pressure"
else
  cc -static -O2 -o "$workspace/fork-pressure" \
    "$(dirname "$0")/../fixtures/fork_pressure.c"
fi
cp "$workspace/fork-pressure" "$rootfs/fork-pressure"
chown --reference="$rootfs" "$rootfs/fork-pressure"
chmod 0755 "$rootfs/fork-pressure"
if ! id="$("$binary" run --detach --memory 128MiB --memory-swap 0 --cpus 0.5 \
  --pids-limit 128 --image alpine-cgroup -- /bin/sh -c 'while :; do :; done')"; then
  find "$workspace" -type f -maxdepth 5 -print -exec sed -n '1,120p' {} \; >&2
  exit 1
fi
state="$MC_STATE_DIR/containers/$id/state.json"
for _ in $(seq 1 50); do
  grep -q '"status":"running"' "$state" 2>/dev/null && break
  sleep 0.05
done
grep -q '"status":"running"' "$state"
shim_pid="$(sed -n 's/.*"shim_pid":\([0-9]*\).*/\1/p' "$state")"
cgroup="$(sed -n 's/.*"cgroup_path":"\([^"]*\)".*/\1/p' "$state")"
[[ -d "$cgroup" ]]
[[ "$(<"$cgroup/memory.max")" == 134217728 ]]
[[ "$(<"$cgroup/memory.swap.max")" == 0 ]]
[[ "$(<"$cgroup/cpu.max")" == '50000 100000' ]]
[[ "$(<"$cgroup/pids.max")" == 128 ]]

sleep 1
stats="$("$binary" stats --no-stream --json "$id")"
grep -q '"schema_version":1' <<<"$stats"
grep -q '"max":134217728' <<<"$stats"
grep -q '"swap_max":0' <<<"$stats"
grep -q '"max":128' <<<"$stats"
throttled="$(awk '$1 == "nr_throttled" { print $2 }' "$cgroup/cpu.stat")"
(( throttled > 0 ))

kill -TERM "$shim_pid"
for _ in $(seq 1 100); do
  [[ ! -e "$cgroup" ]] && break
  sleep 0.05
done
[[ ! -e "$cgroup" ]]
shim_pid=''

oom_id="$("$binary" run --detach --memory 24MiB --memory-swap 0 --image alpine-cgroup -- \
  /bin/sh -c 'dd if=/dev/zero of=/tmp/oom bs=1M count=96 2>/dev/null || true; sleep 10')"
oom_state="$MC_STATE_DIR/containers/$oom_id/state.json"
for _ in $(seq 1 100); do
  oom_cgroup="$(sed -n 's/.*"cgroup_path":"\([^"]*\)".*/\1/p' "$oom_state" 2>/dev/null || true)"
  [[ -n "$oom_cgroup" && -e "$oom_cgroup/memory.events" ]] &&
    (( $(awk '$1 == "oom" { print $2 }' "$oom_cgroup/memory.events") > 0 )) && break
  sleep 0.05
done
(( $(awk '$1 == "oom" { print $2 }' "$oom_cgroup/memory.events") > 0 ))
oom_stats="$("$binary" stats --no-stream --json "$oom_id")"
grep -Eq '"oom":[1-9][0-9]*' <<<"$oom_stats"
oom_shim="$(sed -n 's/.*"shim_pid":\([0-9]*\).*/\1/p' "$oom_state")"
kill -TERM "$oom_shim"

pids_id="$("$binary" run --detach --pids-limit 8 --image alpine-cgroup -- /fork-pressure 30)"
pids_state="$MC_STATE_DIR/containers/$pids_id/state.json"
for _ in $(seq 1 100); do
  pids_cgroup="$(sed -n 's/.*"cgroup_path":"\([^"]*\)".*/\1/p' "$pids_state" 2>/dev/null || true)"
  [[ -n "$pids_cgroup" && -e "$pids_cgroup/pids.events" ]] &&
    (( $(awk '$1 == "max" { print $2 }' "$pids_cgroup/pids.events") > 0 )) && break
  sleep 0.05
done
(( $(awk '$1 == "max" { print $2 }' "$pids_cgroup/pids.events") > 0 ))
grep -Eq '^fork_failures=[1-9][0-9]*$' "$MC_LOG_DIR/$pids_id/container.log"
pids_shim="$(sed -n 's/.*"shim_pid":\([0-9]*\).*/\1/p' "$pids_state")"
kill -TERM "$pids_shim"

printf 'PASS cgroup limits, stats, OOM, throttling, PID exhaustion, and cleanup\n'
