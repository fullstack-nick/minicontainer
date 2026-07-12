#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
destination="$repo_root/.cache/fixtures/alpine-minirootfs-3.24.1-x86_64.tar.gz"
expected="41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081"
url="https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/alpine-minirootfs-3.24.1-x86_64.tar.gz"

mkdir -p "$(dirname "$destination")"
if [[ ! -f "$destination" ]]; then
  curl --fail --location --proto '=https' --tlsv1.2 --output "$destination.tmp" "$url"
  mv "$destination.tmp" "$destination"
fi
printf '%s  %s\n' "$expected" "$destination" | sha256sum --check --status
printf '%s\n' "$destination"
