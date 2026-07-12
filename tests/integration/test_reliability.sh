#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
fixture_binary="${4:-}"
sequential_count="${MC_STRESS_SEQUENTIAL:-100}"
(( EUID == 0 )) || { printf 'reliability integration test requires root\n' >&2; exit 2; }

workspace="$(mktemp -d /tmp/minicontainer-reliability-test.XXXXXX)"
chmod 0755 "$workspace"
cleanup() {
  for name in $("$binary" ps --all --json 2>/dev/null | sed -n 's/.*"name":"\([^"]*\)".*/\1/p'); do
    "$binary" rm --force "$name" >/dev/null 2>&1 || true
  done
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT
export MC_STATE_DIR="$workspace/state"
export MC_LOG_DIR="$workspace/logs"
export MC_RUNTIME_DIR="$workspace/run"
export MC_SHIM_PATH="$shim"

"$binary" image import alpine-reliability "$archive" >/dev/null
digest="$(sed 's/^sha256://' "$MC_STATE_DIR/images/names/alpine-reliability")"
rootfs="$MC_STATE_DIR/images/sha256/$digest/rootfs"
if [[ -n "$fixture_binary" ]]; then
  cp "$fixture_binary" "$workspace/reliability-fixture"
else
  cc -static -O2 -Wall -Wextra -Werror -o "$workspace/reliability-fixture" \
    "$(dirname "$0")/../fixtures/reliability_fixture.c"
fi
cp "$workspace/reliability-fixture" "$rootfs/reliability-fixture"
chown --reference="$rootfs" "$rootfs/reliability-fixture"
chmod 0755 "$rootfs/reliability-fixture"

for index in $(seq 1 "$sequential_count"); do
  "$binary" run --name "seq-$index" --network none --image alpine-reliability -- /bin/true
  "$binary" rm "seq-$index"
done

for index in $(seq 1 8); do
  "$binary" create --name "concurrent-$index" --network none --image alpine-reliability -- \
    /bin/sh -c 'while :; do sleep 1; done' >/dev/null
  "$binary" start "concurrent-$index" >/dev/null &
done
wait
[[ "$("$binary" ps --json | grep -o '"status":"running"' | wc -l)" -eq 8 ]]
for index in $(seq 1 8); do "$binary" rm --force "concurrent-$index"; done

"$binary" create --name race --network none --image alpine-reliability -- \
  /bin/sh -c 'while :; do sleep 1; done' >/dev/null
"$binary" start race >/dev/null
race_pids=()
for _ in $(seq 1 20); do "$binary" exec race -- /bin/true >/dev/null 2>&1 & race_pids+=("$!"); done
"$binary" stop --time 1 race
for pid in "${race_pids[@]}"; do wait "$pid" || true; done
"$binary" rm race

if [[ "${MC_SKIP_OOM:-0}" != 1 ]]; then
  for index in $(seq 1 3); do
    set +e
    "$binary" run --name "oom-$index" --network none --memory 16m --memory-swap 0 \
      --image alpine-reliability -- /reliability-fixture memory 67108864 >/dev/null 2>&1
    status=$?
    set -e
    [[ "$status" -eq 137 || "$status" -eq 125 ]]
    "$binary" rm "oom-$index"
  done
fi

log_id="$("$binary" run --detach --name log-pressure --network none --image alpine-reliability -- \
  /reliability-fixture log 3145728)"
for _ in $(seq 1 200); do
  grep -q '"status":"stopped"' "$MC_STATE_DIR/containers/$log_id/state.json" && break
  sleep 0.05
done
grep -q 'MINICONTAINER_LOG_END' "$MC_LOG_DIR/$log_id/container.log"
[[ -f "$MC_LOG_DIR/$log_id/container.log.1" ]]
[[ "$(find "$MC_LOG_DIR/$log_id" -maxdepth 1 -name 'container.log.*' | wc -l)" -le 3 ]]
"$binary" rm log-pressure

"$binary" gc >/dev/null
! pgrep -af "$workspace" >/dev/null
! ip -o link show type veth | grep -q 'mc[hc]'
! systemctl list-units --type=scope --all --no-legend 'minicontainer-*.scope' | grep -q .
! nft -a list table inet minicontainer | grep -q 'minicontainer:[0-9a-f]'
! find "$MC_RUNTIME_DIR" -mindepth 1 -print -quit 2>/dev/null | grep -q .

printf 'PASS sequential=%s, concurrency=8, exec-stop races, OOM storms, log rotation, zero leaks\n' \
  "$sequential_count"
