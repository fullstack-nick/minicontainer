#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
archive="${2:?Alpine archive is required}"
network_fixture="${3:?network fixture is required}"
duration="${4:-1800}"
output="${5:-/var/lib/minicontainer-soak/result.json}"
(( EUID == 0 )) || { printf 'soak requires root\n' >&2; exit 2; }

start="$(date +%s)"; deadline="$((start + duration))"; iterations=0; failures=0; max_memory=0
mkdir -p "$(dirname "$output")"
binary_commit="$($binary version --json | sed -n 's/.*"git_commit":"\([^"]*\)".*/\1/p')"
cleanup() { "$binary" rm --force soak-http >/dev/null 2>&1 || true; }
trap cleanup EXIT

"$binary" rm --force soak-http >/dev/null 2>&1 || true
"$binary" image import soak-image "$archive" >/dev/null
digest="$(sed 's/^sha256://' /var/lib/minicontainer/images/names/soak-image)"
rootfs="/var/lib/minicontainer/images/sha256/$digest/rootfs"
cp "$network_fixture" "$rootfs/network-fixture"
chown --reference="$rootfs" "$rootfs/network-fixture"
chmod 0755 "$rootfs/network-fixture"
"$binary" create --name soak-http --network bridge --publish 127.0.0.1:18080:8080/tcp \
  --memory 96m --cpus 0.5 --pids-limit 64 --image soak-image -- /network-fixture >/dev/null
"$binary" start soak-http >/dev/null

ready=0
for _ in $(seq 1 100); do
  if [[ "$(curl -fsS --max-time 1 http://127.0.0.1:18080/ 2>/dev/null || true)" == \
        MINICONTAINER_HTTP ]]; then
    ready=1
    break
  fi
  sleep 0.05
done
(( ready == 1 )) || { printf 'soak workload failed readiness\n' >&2; exit 1; }

while (( $(date +%s) < deadline )); do
  if [[ "$(curl -fsS --max-time 3 http://127.0.0.1:18080/ 2>/dev/null || true)" != \
        MINICONTAINER_HTTP ]]; then failures=$((failures + 1)); fi
  if ! "$binary" exec soak-http -- /bin/true >/dev/null 2>&1; then failures=$((failures + 1)); fi
  stats="$($binary stats --no-stream --json soak-http 2>/dev/null || true)"
  memory="$(sed -n 's/.*"memory":{"current":\([0-9]*\).*/\1/p' <<<"$stats")"
  if [[ -z "$memory" ]]; then failures=$((failures + 1)); memory=0; fi
  (( memory > max_memory )) && max_memory="$memory"
  iterations=$((iterations + 1))
  printf 'iteration=%s failures=%s memory=%s\n' "$iterations" "$failures" "$memory" \
    >"${output%.json}.progress"
  sleep_for=60
  remaining="$((deadline - $(date +%s)))"
  (( remaining < sleep_for )) && sleep_for="$remaining"
  (( sleep_for > 0 )) && sleep "$sleep_for"
done

end="$(date +%s)"
status="$($binary inspect soak-http)"
grep -q '"status":"running"' <<<"$status" || failures=$((failures + 1))
kernel_findings="$(journalctl -k --since "@$start" --no-pager 2>/dev/null | \
  grep -Eic 'kernel panic|BUG:|general protection fault|out of memory|oom-kill' || true)"
"$binary" rm --force soak-http
"$binary" gc >/dev/null

python3 - "$output" "$binary_commit" "$start" "$end" "$iterations" "$failures" \
  "$max_memory" "$kernel_findings" <<'PY'
import json, pathlib, sys
path, commit, start, end, iterations, failures, memory, kernel = sys.argv[1:]
doc = {'schema_version': 1, 'git_commit': commit, 'start_unix': int(start),
       'end_unix': int(end), 'duration_seconds': int(end)-int(start),
       'iterations': int(iterations), 'failures': int(failures),
       'max_memory_bytes': int(memory), 'kernel_findings': int(kernel),
       'result': 'PASS' if int(failures) == 0 and int(kernel) == 0 else 'FAIL'}
pathlib.Path(path).write_text(json.dumps(doc, indent=2) + '\n')
print(json.dumps(doc, indent=2))
raise SystemExit(0 if doc['result'] == 'PASS' else 1)
PY
rm -f "${output%.json}.progress"
