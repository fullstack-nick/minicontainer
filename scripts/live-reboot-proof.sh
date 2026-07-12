#!/usr/bin/env bash
set -euo pipefail

archive="${1:?Alpine archive is required}"
fixture="${2:?static reliability fixture is required}"
phase="${3:?prepare or verify is required}"
if [[ "$phase" == prepare ]]; then
  minicontainer rm --force reboot-demo >/dev/null 2>&1 || true
  minicontainer image import reboot-demo-image "$archive" >/dev/null
  digest="$(sed 's/^sha256://' /var/lib/minicontainer/images/names/reboot-demo-image)"
  rootfs="/var/lib/minicontainer/images/sha256/$digest/rootfs"
  cp "$fixture" "$rootfs/reliability-fixture"
  chown --reference="$rootfs" "$rootfs/reliability-fixture"
  chmod 0755 "$rootfs/reliability-fixture"
  minicontainer create --name reboot-demo --network none --image reboot-demo-image -- \
    /bin/sh -c 'while :; do sleep 1; done' >/dev/null
  minicontainer start reboot-demo >/dev/null
  state=/var/lib/minicontainer/containers/$(minicontainer inspect reboot-demo | \
    sed -n 's/.*"id":"\([0-9a-f]*\)".*/\1/p')/state.json
  shim_pid="$(sed -n 's/.*"shim_pid":\([0-9]*\).*/\1/p' "$state")"
  kill -KILL "$shim_pid"
  sleep 0.1
  grep -q '"status":"running"' "$state"
  printf 'PREPARED stale-running reboot-demo\n'
elif [[ "$phase" == verify ]]; then
  reconcile_status="$(systemctl is-active minicontainer-reconcile.service || true)"
  [[ "$reconcile_status" == inactive || "$reconcile_status" == active ]]
  minicontainer inspect reboot-demo | grep -q '"status":"stopped"'
  systemd-run --quiet --unit=minicontainer-reboot-demo --property=Type=oneshot \
    --property=RemainAfterExit=yes /usr/bin/minicontainer start reboot-demo
  for _ in $(seq 1 100); do
    systemctl is-active minicontainer-reboot-demo.service 2>/dev/null | grep -q active && break
    sleep 0.05
  done
  systemctl is-active minicontainer-reboot-demo.service | grep -q active
  minicontainer inspect reboot-demo | grep -q '"status":"running"'
  minicontainer exec reboot-demo -- /bin/true
  printf 'RESTORED reboot-demo after boot reconciliation\n'
  if ! minicontainer rm --force reboot-demo; then
    sleep 1
    minicontainer gc >/dev/null
    minicontainer rm reboot-demo
  fi
  systemctl stop minicontainer-reboot-demo.service || true
  systemctl reset-failed minicontainer-reboot-demo.service 2>/dev/null || true
  minicontainer gc >/dev/null
  printf 'PASS boot reconciliation and systemd workload restoration\n'
else
  printf 'phase must be prepare or verify\n' >&2
  exit 2
fi
