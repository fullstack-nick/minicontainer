#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
probe_binary="${4:-}"
(( EUID == 0 )) || { printf 'security integration test requires root\n' >&2; exit 2; }

workspace="$(mktemp -d /tmp/minicontainer-security-test.XXXXXX)"
chmod 0755 "$workspace"
host_sentinel="/tmp/minicontainer-host-sentinel.$$"
cleanup() {
  rm -f "$host_sentinel"
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT
printf 'HOST_SECRET_MUST_NOT_ESCAPE\n' >"$host_sentinel"
export MC_STATE_DIR="$workspace/state"
export MC_LOG_DIR="$workspace/logs"
export MC_RUNTIME_DIR="$workspace/run"
export MC_SHIM_PATH="$shim"
mkdir -p "$workspace/share"
printf 'ALLOWLISTED_DATA\n' >"$workspace/share/data.txt"
printf '{"allowed_bind_sources":["%s"]}\n' "$workspace/share" >"$workspace/config.json"
printf '{"version":1,"deny":["getcwd"]}\n' >"$workspace/seccomp.json"
export MC_CONFIG_PATH="$workspace/config.json"

"$binary" image import alpine-security "$archive" >/dev/null
digest="$(sed 's/^sha256://' "$MC_STATE_DIR/images/names/alpine-security")"
rootfs="$MC_STATE_DIR/images/sha256/$digest/rootfs"
if [[ -n "$probe_binary" ]]; then
  cp "$probe_binary" "$workspace/security-probe"
else
  cc -static -O2 -Wall -Wextra -Werror -o "$workspace/security-probe" \
    "$(dirname "$0")/../fixtures/security_probe.c"
fi
cp "$workspace/security-probe" "$rootfs/security-probe"
chown --reference="$rootfs" "$rootfs/security-probe"
chmod 0755 "$rootfs/security-probe"

default_output="$("$binary" run --network none --image alpine-security -- /security-probe)"
grep -q 'effective=0000000000000000 permitted=0000000000000000' <<<"$default_output"
grep -q 'PASS denied dangerous syscalls; allowed workload and FD closure work' <<<"$default_output"

readonly_output="$("$binary" run --read-only --network none --image alpine-security -- \
  /bin/sh -c 'touch /forbidden 2>/dev/null && exit 99; touch /tmp/allowed; cat /proc/self/status')"
grep -q 'NoNewPrivs:[[:space:]]*1' <<<"$readonly_output"
grep -q 'CapBnd:[[:space:]]*0000000000000000' <<<"$readonly_output"

cap_output="$("$binary" run --cap-add CHOWN --network none --image alpine-security -- \
  /bin/sh -c 'grep "^CapEff:" /proc/self/status')"
grep -q '0000000000000001' <<<"$cap_output"

! "$binary" run --cap-add NOT_A_CAPABILITY --network none --image alpine-security -- /bin/true \
  >/dev/null 2>&1
! "$binary" run --network none --image alpine-security -- \
  /bin/cat "$host_sentinel" >/dev/null 2>&1

bind_output="$("$binary" run --bind "$workspace/share:/data:ro" --network none \
  --image alpine-security -- /bin/sh -c \
  'cat /data/data.txt; touch /data/forbidden 2>/dev/null && exit 99; test -f /data/data.txt')"
grep -q 'ALLOWLISTED_DATA' <<<"$bind_output"
"$binary" run --tmpfs /scratch:8m --network none --image alpine-security -- \
  /bin/sh -c 'echo TMPFS_OK >/scratch/result; grep -q TMPFS_OK /scratch/result'
! "$binary" run --bind '/proc:/host-proc:ro' --network none --image alpine-security -- /bin/true \
  >/dev/null 2>&1
ln -s /proc "$rootfs/target-symlink"
! "$binary" run --tmpfs /target-symlink/escape:1m --network none --image alpine-security -- /bin/true \
  >/dev/null 2>&1
custom_output="$("$binary" run --seccomp-profile "$workspace/seccomp.json" --network none \
  --image alpine-security -- /security-probe deny-getcwd)"
grep -q 'PASS custom seccomp deny' <<<"$custom_output"
chmod g+w "$workspace/seccomp.json"
! "$binary" run --seccomp-profile "$workspace/seccomp.json" --network none \
  --image alpine-security -- /bin/true >/dev/null 2>&1

secret_id="$("$binary" create --name secret-state --env API_TOKEN=do-not-print \
  --network none --image alpine-security -- /bin/true)"
[[ "$(stat -c %a "$MC_STATE_DIR/containers/$secret_id/config.json")" == 600 ]]
! "$binary" inspect secret-state | grep -q 'do-not-print'
"$binary" rm secret-state

printf 'PASS no-new-privileges, caps, seccomp, read-only root, safe bind/tmpfs, host isolation\n'
