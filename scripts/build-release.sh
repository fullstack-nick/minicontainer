#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! git -C "$repo_root" diff --quiet || ! git -C "$repo_root" diff --cached --quiet; then
  printf 'refusing release build from a dirty worktree\n' >&2
  exit 2
fi
commit="$(git -C "$repo_root" rev-parse HEAD)"
SOURCE_DATE_EPOCH="$(git -C "$repo_root" show -s --format=%ct HEAD)"
export SOURCE_DATE_EPOCH
build_dir="${TMPDIR:-/tmp}/minicontainer-release-$commit"
dist_dir="$repo_root/dist"

cmake --fresh -S "$repo_root" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DMC_GIT_COMMIT="$commit"
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
cmake --build "$build_dir" --target package

mkdir -p "$dist_dir"
cp "$build_dir/minicontainer_0.0.1_amd64.deb" "$dist_dir/"
(cd "$dist_dir" && sha256sum minicontainer_0.0.1_amd64.deb > minicontainer_0.0.1_amd64.deb.sha256)
digest="$(sha256sum "$dist_dir/minicontainer_0.0.1_amd64.deb" | cut -d' ' -f1)"
jq -n --arg version '0.0.1' --arg commit "$commit" --arg digest "$digest" \
  --arg compiler "$(cc --version | head -n1)" \
  '{version:$version,git_commit:$commit,sha256:$digest,compiler:$compiler}' \
  > "$dist_dir/build-manifest.json"
syft "file:$dist_dir/minicontainer_0.0.1_amd64.deb" -o spdx-json="$dist_dir/minicontainer_0.0.1_amd64.spdx.json"
