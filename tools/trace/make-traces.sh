#!/usr/bin/env bash
# Records faith-runner's own tests as per-frame traces for RunlineCore's replay tests.
# Works in a separate worktree of the reference clone (../faith-runner-trace) with the trace
# patch applied, so the reference clone itself stays clean.
# Usage (Git Bash, from anywhere): bash tools/trace/make-traces.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
ref="$root/../faith-runner"
wt="$root/../faith-runner-trace"
sha="$(sed -n 's/^faith-runner \([0-9a-f]\{40\}\).*/\1/p' "$root/RunlineCore/tests/golden/REFERENCE")"
patch="$root/tools/trace/faith-runner-trace.patch"
[ -n "$sha" ] || { echo "can't read the faith-runner SHA from REFERENCE" >&2; exit 1; }
[ -d "$wt" ] || git -C "$ref" worktree add --detach "$wt" "$sha"
git -C "$wt" checkout -q --detach "$sha"
git -C "$wt" reset -q --hard
git -C "$wt" clean -fdq
git -C "$wt" apply "$patch"
out="$(mktemp -d)"
( cd "$wt" && FAITH_TRACE_DIR="$(cygpath -w "$out")" PATH="$HOME/.cargo/bin:$PATH" cargo test -q -p faith_move --release )
# Gather the selected traces in a staging folder first, so a missing one leaves the committed
# traces untouched.
stage="$out/selected"
mkdir -p "$stage"
tr -d '\r' < "$root/tools/trace/selected.txt" | while read -r name; do   # tr: tolerate CRLF checkouts
  { [ -z "$name" ] || [ "${name#\#}" != "$name" ]; } && continue
  [ -f "$out/$name.trace" ] || { echo "no trace recorded for $name" >&2; exit 1; }
  cp "$out/$name.trace" "$stage/"
done
dest="$root/RunlineCore/tests/golden/traces"
mkdir -p "$dest"
rm -f "$dest"/*.trace
cp "$stage"/*.trace "$dest/"
printf 'faith-runner %s + trace patch %s\n' "$sha" "$(sha256sum "$patch" | cut -c1-16)" > "$dest/TRACE_REFERENCE"
echo "recorded $(ls "$out"/*.trace | wc -l) traces, kept $(ls "$dest"/*.trace | wc -l)"
[ -z "$(git -C "$ref" status --porcelain)" ] || { echo "reference clone is dirty" >&2; exit 1; }
