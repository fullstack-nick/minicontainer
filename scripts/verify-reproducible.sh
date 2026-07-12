#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build-release.sh"
first="$(sha256sum "$repo_root/dist/minicontainer_1.0.0_amd64.deb" | awk '{print $1}')"
"$repo_root/scripts/build-release.sh"
second="$(sha256sum "$repo_root/dist/minicontainer_1.0.0_amd64.deb" | awk '{print $1}')"
printf 'first=%s\nsecond=%s\n' "$first" "$second"
if [[ "$first" != "$second" ]]; then
  printf 'release package is not reproducible\n' >&2
  exit 1
fi
test -s "$repo_root/dist/minicontainer_1.0.0_amd64.spdx.json"
