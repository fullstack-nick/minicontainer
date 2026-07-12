#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
reliability_fixture="${4:?reliability fixture is required}"
network_fixture="${5:?network fixture is required}"
output="${6:-benchmark.json}"
(( EUID == 0 )) || { printf 'benchmark requires root\n' >&2; exit 2; }

workspace="$(mktemp -d /tmp/minicontainer-benchmark.XXXXXX)"
chmod 0755 "$workspace"
cleanup() {
  kill "${host_fixture_pid:-}" 2>/dev/null || true
  for name in bench-daemon bench-cpu bench-http; do "$binary" rm --force "$name" >/dev/null 2>&1 || true; done
  for index in $(seq 1 130); do "$binary" rm --force "bench-$index" >/dev/null 2>&1 || true; done
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT
export MC_STATE_DIR="$workspace/state"
export MC_LOG_DIR="$workspace/logs"
export MC_RUNTIME_DIR="$workspace/run"
export MC_SHIM_PATH="$shim"

"$binary" image import benchmark-image "$archive" >/dev/null
digest="$(sed 's/^sha256://' "$MC_STATE_DIR/images/names/benchmark-image")"
rootfs="$MC_STATE_DIR/images/sha256/$digest/rootfs"
cp "$reliability_fixture" "$rootfs/reliability-fixture"
cp "$network_fixture" "$rootfs/network-fixture"
chown --reference="$rootfs" "$rootfs/reliability-fixture" "$rootfs/network-fixture"
chmod 0755 "$rootfs/reliability-fixture" "$rootfs/network-fixture"

for index in $(seq 1 30); do
  start="$(date +%s%N)"
  "$binary" run --name "bench-$index" --network none --image benchmark-image -- /bin/true
  end="$(date +%s%N)"
  printf '%s\n' "$(((end - start) / 1000))" >>"$workspace/cold-us"
  "$binary" rm "bench-$index"
done

start="$(date +%s%N)"
for index in $(seq 31 130); do
  "$binary" run --name "bench-$index" --network none --image benchmark-image -- /bin/true
  "$binary" rm "bench-$index"
done
end="$(date +%s%N)"
throughput_ns="$((end - start))"

"$binary" create --name bench-daemon --network none --image benchmark-image -- \
  /bin/sh -c 'while :; do sleep 1; done' >/dev/null
"$binary" start bench-daemon >/dev/null
memory_current="$("$binary" stats --no-stream --json bench-daemon | \
  sed -n 's/.*"memory":{"current":\([0-9]*\).*/\1/p')"
for _ in $(seq 1 100); do
  start="$(date +%s%N)"; "$binary" exec bench-daemon -- /bin/true; end="$(date +%s%N)"
  printf '%s\n' "$(((end - start) / 1000))" >>"$workspace/exec-us"
done
"$binary" rm --force bench-daemon

"$binary" create --name bench-cpu --network none --cpus 0.5 --image benchmark-image -- \
  /reliability-fixture cpu 0 >/dev/null
"$binary" start bench-cpu >/dev/null
cpu_before="$("$binary" stats --no-stream --json bench-cpu | \
  sed -n 's/.*"cpu":{"usage_usec":\([0-9]*\).*/\1/p')"
sleep 10
cpu_after="$("$binary" stats --no-stream --json bench-cpu | \
  sed -n 's/.*"cpu":{"usage_usec":\([0-9]*\).*/\1/p')"
"$binary" rm --force bench-cpu

"$network_fixture" & host_fixture_pid=$!
for _ in $(seq 1 100); do curl -fsS http://127.0.0.1:8080/ >/dev/null 2>&1 && break; sleep 0.02; done
for _ in $(seq 1 100); do curl -fsS -o /dev/null -w '%{time_total}\n' http://127.0.0.1:8080/ >>"$workspace/http-host"; done
kill "$host_fixture_pid"; wait "$host_fixture_pid" 2>/dev/null || true; host_fixture_pid=''

"$binary" create --name bench-http --network bridge --publish 127.0.0.1:18080:8080/tcp \
  --image benchmark-image -- /network-fixture >/dev/null
"$binary" start bench-http >/dev/null
for _ in $(seq 1 100); do curl -fsS http://127.0.0.1:18080/ >/dev/null 2>&1 && break; sleep 0.02; done
for _ in $(seq 1 100); do curl -fsS -o /dev/null -w '%{time_total}\n' http://127.0.0.1:18080/ >>"$workspace/http-container"; done
"$binary" rm --force bench-http

python3 - "$workspace" "$output" "$throughput_ns" "$memory_current" \
  "$((cpu_after - cpu_before))" <<'PY'
import json, pathlib, statistics, sys
root, output, throughput_ns, memory, cpu_delta = sys.argv[1:]
root = pathlib.Path(root)
def ints(name): return [int(x) for x in (root/name).read_text().split()]
def floats(name): return [float(x) for x in (root/name).read_text().split()]
def percentile(values, p):
    values = sorted(values); return values[min(len(values)-1, int((len(values)-1)*p))]
cold, execution = ints('cold-us'), ints('exec-us')
host, container = floats('http-host'), floats('http-container')
document = {
  'schema_version': 1,
  'cold_start_us': {'median': int(statistics.median(cold)), 'p95': percentile(cold, .95)},
  'steady_state_memory_bytes': int(memory),
  'run_remove_per_second': round(100 / (int(throughput_ns) / 1e9), 2),
  'exec_us': {'median': int(statistics.median(execution)), 'p95': percentile(execution, .95)},
  'cpu_half_core': {'window_seconds': 10, 'usage_usec': int(cpu_delta),
                    'accuracy_percent': round(int(cpu_delta) / 10_000_000 * 100, 2)},
  'http_us': {'host_median': round(statistics.median(host)*1e6, 2),
              'container_median': round(statistics.median(container)*1e6, 2),
              'overhead_us': round((statistics.median(container)-statistics.median(host))*1e6, 2)}
}
pathlib.Path(output).write_text(json.dumps(document, indent=2) + '\n')
print(json.dumps(document, indent=2))
PY

"$binary" gc >/dev/null
printf 'PASS benchmark suite and cleanup\n'
