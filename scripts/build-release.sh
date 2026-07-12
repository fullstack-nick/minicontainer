#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
commit="$(git -C "$repo_root" rev-parse HEAD)"
build_dir="$repo_root/build/release"
dist_dir="$repo_root/dist"

cmake -S "$repo_root" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DMC_GIT_COMMIT="$commit"
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
cmake --build "$build_dir" --target package

mkdir -p "$dist_dir"
cp "$build_dir/minicontainer_0.0.1_amd64.deb" "$dist_dir/"
(cd "$dist_dir" && sha256sum minicontainer_0.0.1_amd64.deb > minicontainer_0.0.1_amd64.deb.sha256)
printf '{"version":"0.0.1","git_commit":"%s","compiler":"%s"}\n' \
  "$commit" "$(cc --version | head -n1)" > "$dist_dir/build-manifest.json"

