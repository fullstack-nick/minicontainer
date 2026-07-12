#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
commit="$(git -C "$repo_root" rev-parse HEAD)"
quality_dir="${TMPDIR:-/tmp}/minicontainer-quality-$commit"

cmake --fresh -S "$repo_root" -B "$quality_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build "$quality_dir"
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir "$quality_dir" --output-on-failure

valgrind --quiet --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=all --track-fds=yes --error-exitcode=1 \
  "$repo_root/build/dev-gcc/unit_tests"

mapfile -t sources < <(find "$repo_root/src" -type f -name '*.c' -print | sort)
clang-tidy -p "$quality_dir" "${sources[@]}"

cppcheck --enable=warning,performance,portability --error-exitcode=1 \
  --inline-suppr --suppress=missingIncludeSystem --std=c17 \
  -I "$repo_root/include" "$repo_root/src" "$repo_root/tests"

printf 'PASS ASan, UBSan, Valgrind, clang-tidy, and cppcheck\n'
