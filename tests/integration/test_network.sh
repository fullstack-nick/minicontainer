#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
shim="${2:?minicontainer shim is required}"
archive="${3:?Alpine archive is required}"
fixture_binary="${4:-}"
(( EUID == 0 )) || { printf 'network integration test requires root\n' >&2; exit 2; }

workspace="$(mktemp -d /tmp/minicontainer-network-test.XXXXXX)"
chmod 0755 "$workspace"
cleanup() {
  if [[ -n "${host_listener:-}" ]]; then kill "$host_listener" 2>/dev/null || true; fi
  for name in net-a net-b net-c net-none net-crash; do
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

"$binary" image import alpine-network "$archive" >/dev/null
digest="$(sed 's/^sha256://' "$MC_STATE_DIR/images/names/alpine-network")"
rootfs="$MC_STATE_DIR/images/sha256/$digest/rootfs"
if [[ -n "$fixture_binary" ]]; then
  cp "$fixture_binary" "$workspace/network-fixture"
else
  cc -static -O2 -o "$workspace/network-fixture" \
    "$(dirname "$0")/../fixtures/network_fixture.c"
fi
cp "$workspace/network-fixture" "$rootfs/network-fixture"
chown --reference="$rootfs" "$rootfs/network-fixture"
chmod 0755 "$rootfs/network-fixture"
id_a="$("$binary" create --name net-a --network bridge \
  --publish 127.0.0.1:18080:8080/tcp --publish 127.0.0.1:19090:9090/udp \
  --image alpine-network -- /bin/sh -c \
  'exec /network-fixture')"
"$binary" start net-a >/dev/null
for _ in $(seq 1 100); do
  grep -q '"status":"running"' "$MC_STATE_DIR/containers/$id_a/state.json" && break
  sleep 0.05
done
grep -q '"ipv4_host":170655746' "$MC_STATE_DIR/containers/$id_a/state.json"
host_a="mch${id_a:0:8}"
ip link show "$host_a" | grep -q 'master mcbr0'
network_a="$("$binary" exec net-a -- /bin/sh -c \
  'ip -4 addr show dev eth0; ip route; ping -c 1 -W 1 10.44.0.1')"
grep -q '10.44.0.2/24' <<<"$network_a"
grep -q 'default via 10.44.0.1 dev eth0' <<<"$network_a"
for _ in $(seq 1 50); do
  [[ "$(curl --silent --max-time 1 http://127.0.0.1:18080/ 2>/dev/null || true)" == \
    MINICONTAINER_HTTP ]] && break
  sleep 0.05
done
[[ "$(curl --silent --max-time 1 http://127.0.0.1:18080/)" == MINICONTAINER_HTTP ]]
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
s.sendto(b'MINICONTAINER_UDP', ('127.0.0.1', 19090))
assert s.recv(128) == b'MINICONTAINER_UDP'
PY
! curl --silent --max-time 1 http://127.0.0.1:18081/ >/dev/null 2>&1
nft -a list table inet minicontainer | grep -q "minicontainer:$id_a"

set +e
"$binary" create --name net-conflict --network bridge \
  --publish 127.0.0.1:18080:8181/tcp --image alpine-network -- /bin/true >/dev/null 2>&1
collision_status=$?
set -e
[[ "$collision_status" -eq 4 ]]
! "$binary" inspect net-conflict >/dev/null 2>&1
set +e
"$binary" create --name net-udp-conflict --network bridge \
  --publish 127.0.0.1:19090:9191/udp --image alpine-network -- /bin/true >/dev/null 2>&1
udp_collision_status=$?
set -e
[[ "$udp_collision_status" -eq 4 ]]
python3 -m http.server 18083 --bind 127.0.0.1 >"$workspace/host-listener.log" 2>&1 &
host_listener=$!
sleep 0.1
set +e
"$binary" create --name net-host-conflict --network bridge \
  --publish 127.0.0.1:18083:8080/tcp --image alpine-network -- /bin/true >/dev/null 2>&1
host_collision_status=$?
set -e
kill "$host_listener"; wait "$host_listener" 2>/dev/null || true
host_listener=''
[[ "$host_collision_status" -eq 4 ]]

crash_id="$("$binary" create --name net-crash --network bridge \
  --publish 127.0.0.1:18082:8080/tcp --image alpine-network -- /network-fixture)"
"$binary" start net-crash >/dev/null
crash_host="mch${crash_id:0:8}"
nft -a list table inet minicontainer | grep -q "minicontainer:$crash_id"
crash_shim="$(sed -n 's/.*"shim_pid":\([0-9]*\).*/\1/p' \
  "$MC_STATE_DIR/containers/$crash_id/state.json")"
kill -KILL "$crash_shim"
sleep 0.2
"$binary" gc >/dev/null
! ip link show "$crash_host" >/dev/null 2>&1
! nft -a list table inet minicontainer | grep -q "minicontainer:$crash_id"
"$binary" rm net-crash

id_b="$("$binary" create --name net-b --network bridge --image alpine-network -- \
  /bin/sh -c 'while :; do sleep 1; done')"
"$binary" start net-b >/dev/null
grep -q '"ipv4_host":170655747' "$MC_STATE_DIR/containers/$id_b/state.json"
"$binary" exec net-b -- ping -c 1 -W 1 10.44.0.2 >/dev/null
if [[ "${MC_SKIP_EXTERNAL_NETWORK:-0}" != 1 ]]; then
  "$binary" exec net-b -- /bin/sh -c \
    'wget -qO- -T 8 http://example.com | grep -q "Example Domain"'
fi

id_none="$("$binary" create --name net-none --network none --image alpine-network -- \
  /bin/sh -c 'while :; do sleep 1; done')"
"$binary" start net-none >/dev/null
none_links="$("$binary" exec net-none -- ip link show)"
grep -q 'LOOPBACK' <<<"$none_links"
! grep -q 'eth0' <<<"$none_links"
set +e
"$binary" exec net-none -- ping -c 1 -W 1 10.44.0.1 >/dev/null 2>&1
none_ping=$?
set -e
[[ "$none_ping" -ne 0 ]]

"$binary" rm --force net-a
! ip link show "$host_a" >/dev/null 2>&1
! nft -a list table inet minicontainer | grep -q "minicontainer:$id_a"
! curl --silent --max-time 1 http://127.0.0.1:18080/ >/dev/null 2>&1
id_c="$("$binary" create --name net-c --network bridge --image alpine-network -- /bin/true)"
grep -q '"ipv4_host":170655746' "$MC_STATE_DIR/containers/$id_c/config.json"

nft list chain inet minicontainer forward | grep -q 'policy drop'
nft list chain inet minicontainer postrouting | grep -q 'masquerade'
printf 'PASS bridge/veth, IPAM, TCP/UDP publish, NAT, collisions, network-none, and cleanup\n'
