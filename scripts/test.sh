#!/usr/bin/env bash
# Configure, build and run every RunlineCore test on Linux or macOS (scripts/test.ps1 on Windows).
# Usage: bash scripts/test.sh [doctest options, e.g. -tc="replay*"]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/RunlineCore"
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S "$root/RunlineCore" -B "$build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build"
"$build/RunlineCoreTests" "$@"
