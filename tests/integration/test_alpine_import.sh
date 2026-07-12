#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
archive="${2:?Alpine archive is required}"
workspace="$(mktemp -d /tmp/minicontainer-alpine-test.XXXXXX)"
cleanup() {
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT

output="$(MC_STATE_DIR="$workspace/state" "$binary" image import alpine-3.24 "$archive" --json)"
grep -q 'sha256:41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081' \
  <<< "$output"
rootfs="$workspace/state/images/sha256/41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081/rootfs"
test -x "$rootfs/bin/busybox"
test -L "$rootfs/bin/sh"
test -f "$rootfs/etc/alpine-release"
printf 'PASS Alpine 3.24.1 import\n'
