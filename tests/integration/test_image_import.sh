#!/usr/bin/env bash
set -euo pipefail

binary="${1:?minicontainer binary is required}"
MC_SUBUID_START="$(id -u)"
MC_SUBGID_START="$(id -g)"
export MC_SUBUID_START MC_SUBGID_START
workspace="$(mktemp -d /tmp/minicontainer-image-test.XXXXXX)"
cleanup() {
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT

mkdir -p "$workspace/rootfs/bin"
printf '#!/bin/sh\necho imported\n' > "$workspace/rootfs/bin/hello"
chmod 0755 "$workspace/rootfs/bin/hello"
ln -s /bin/hello "$workspace/rootfs/bin/absolute-link"
tar --numeric-owner --owner=0 --group=0 -C "$workspace/rootfs" -czf "$workspace/good.tar.gz" .

MC_STATE_DIR="$workspace/state" "$binary" image import test-image "$workspace/good.tar.gz" --json \
  > "$workspace/import.json"
grep -q '"name":"test-image"' "$workspace/import.json"
test -x "$workspace/state/images/sha256/$(sha256sum "$workspace/good.tar.gz" | awk '{print $1}')/rootfs/bin/hello"
grep -q '^sha256:' "$workspace/state/images/names/test-image"
MC_STATE_DIR="$workspace/state" "$binary" image import test-image "$workspace/good.tar.gz" \
  > /dev/null

python3 - "$workspace/evil.tar" <<'PY'
import io
import tarfile
import sys

with tarfile.open(sys.argv[1], "w") as archive:
    entry = tarfile.TarInfo("../escape")
    payload = b"escaped\n"
    entry.size = len(payload)
    archive.addfile(entry, io.BytesIO(payload))
PY

set +e
MC_STATE_DIR="$workspace/evil-state" "$binary" image import evil "$workspace/evil.tar" \
  > "$workspace/evil.out" 2> "$workspace/evil.err"
status=$?
set -e
test "$status" -eq 2
grep -q 'unsafe archive path' "$workspace/evil.err"
test ! -e "$workspace/escape"

printf 'PASS secure image import integration test\n'
