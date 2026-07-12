#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace="$(mktemp -d /tmp/minicontainer-gif.XXXXXX)"
trap 'rm -rf -- "$workspace"' EXIT
base=(-size 960x420 xc:#0d1117 -font DejaVu-Sans-Mono -pointsize 20 -gravity northwest)

convert "${base[@]}" -fill '#58a6ff' -annotate +38+42 \
  '$ minicontainer version --json' -fill '#c9d1d9' -annotate +38+82 \
  '{"version":"1.0.0","git_commit":"c2d811e..."}' "$workspace/1.png"
convert "$workspace/1.png" -fill '#58a6ff' -annotate +38+142 \
  '$ minicontainer run --detach --name demo --image alpine -- service-loop' \
  -fill '#c9d1d9' -annotate +38+182 \
  'container started with seven namespaces + cgroup v2' "$workspace/2.png"
convert "$workspace/2.png" -fill '#58a6ff' -annotate +38+242 \
  '$ minicontainer exec demo -- hostname' -fill '#7ee787' -annotate +38+282 \
  'demo' "$workspace/3.png"
convert "$workspace/3.png" -fill '#58a6ff' -annotate +38+342 \
  '$ minicontainer rm --force demo' -fill '#7ee787' -annotate +38+382 \
  'No daemon. No residual resources.' "$workspace/4.png"
convert -delay 90 "$workspace/1.png" "$workspace/2.png" "$workspace/3.png" \
  -delay 180 "$workspace/4.png" -loop 0 "$repo_root/docs/minicontainer-demo.gif"
