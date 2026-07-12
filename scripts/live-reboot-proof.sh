#!/usr/bin/env bash
set -euo pipefail

archive="${1:?Alpine archive is required}"
fixture="${2:?static reliability fixture is required}"
phase="${3:?prepare or verify is required}"
unit=/etc/systemd/system/minicontainer-reboot-demo.service

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
  cat >"$unit" <<'UNIT'
[Unit]
Description=MiniContainer reboot recovery proof workload
After=minicontainer-reconcile.service
Requires=minicontainer-reconcile.service

[Service]
Type=oneshot
ExecStart=/usr/bin/minicontainer start reboot-demo
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNIT
  systemctl daemon-reload
  systemctl enable minicontainer-reboot-demo.service >/dev/null
  printf 'PREPARED reboot-demo\n'
elif [[ "$phase" == verify ]]; then
  reconcile_status="$(systemctl is-active minicontainer-reconcile.service || true)"
  [[ "$reconcile_status" == inactive || "$reconcile_status" == active ]]
  systemctl is-active minicontainer-reboot-demo.service | grep -q active
  minicontainer inspect reboot-demo | grep -q '"status":"running"'
  minicontainer exec reboot-demo -- /bin/true
  systemctl disable minicontainer-reboot-demo.service >/dev/null
  rm -f "$unit"
  systemctl daemon-reload
  minicontainer rm --force reboot-demo
  minicontainer gc >/dev/null
  printf 'PASS boot reconciliation and systemd workload restoration\n'
else
  printf 'phase must be prepare or verify\n' >&2
  exit 2
fi
