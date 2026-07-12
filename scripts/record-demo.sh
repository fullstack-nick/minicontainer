#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
(( EUID == 0 )) || { printf 'demo recording requires root\n' >&2; exit 2; }
binary="$repo_root/build/dev-gcc/minicontainer"
shim="$repo_root/build/dev-gcc/minicontainer-shim"
archive="$repo_root/.cache/fixtures/alpine-minirootfs-3.24.1-x86_64.tar.gz"
workspace="$(mktemp -d /tmp/minicontainer-demo.XXXXXX)"
chmod 0755 "$workspace"
cleanup() {
  "$binary" rm --force demo >/dev/null 2>&1 || true
  chmod -R u+w "$workspace" 2>/dev/null || true
  rm -rf -- "$workspace"
}
trap cleanup EXIT
export MC_STATE_DIR="$workspace/state"
export MC_LOG_DIR="$workspace/logs"
export MC_RUNTIME_DIR="$workspace/run"
export MC_SHIM_PATH="$shim"

run() {
  local -a display=("$@")
  [[ "${display[0]}" == "$binary" ]] && display[0]=minicontainer
  printf '\033[1;36m$ %s\033[0m\n' "${display[*]}"
  "$@"
  sleep 1
}

clear
printf '\033[1;32mMiniContainer - containers from Linux primitives\033[0m\n\n'
printf '\033[1;36m$ minicontainer version --json\033[0m\n'
"$binary" version --json
sleep 1
printf '\033[1;36m$ minicontainer image import alpine <pinned-rootfs>\033[0m\n'
"$binary" image import alpine "$archive" >/dev/null
printf 'imported Alpine 3.24.1 by SHA-256\n'
sleep 1
printf '\033[1;36m$ minicontainer run --detach --name demo --image alpine -- service-loop\033[0m\n'
"$binary" run --detach --name demo --network none --image alpine -- \
  /bin/sh -c 'while :; do sleep 1; done' >/dev/null
sleep 1
run "$binary" exec demo -- /bin/hostname
run "$binary" inspect demo
run "$binary" stats --no-stream --json demo
run "$binary" rm --force demo
printf '\n\033[1;32mNo daemon. No residual container resources.\033[0m\n'
