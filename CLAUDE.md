# Runline

Mirror's Edge movement ported from faith-runner (Rust) to a plain C++ core, then UE 5.8.
Spec: docs/superpowers/specs/2026-10-09-faith-movement-port-design.md

## Build and test
- `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`: configures, builds and runs all RunlineCore tests (no Unreal needed).
- `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 [-Filter Runline.World]`: builds the
  RunlineEditor target (UE at D:\UE_5.8, or $env:UE_ROOT) and runs the Runline.* automation tests headless;
  the log is build/unreal-tests.log.
- `scripts/build-core-ue.ps1`: RunlineCore as a Release static library for Unreal (build/RunlineCore-ue); UBT
  runs it as a pre-build step, so it rarely needs running by hand.
- `powershell -ExecutionPolicy Bypass -File scripts/bake-courses.ps1`: bakes Data/Levels/{rooftops,greybox}.csv into
  /Game/Runline/Maps/{Rooftops,Greybox} (the RunlineBakeCourse commandlet, RunlineEditor module). Re-baking replaces
  hand edits in those maps; commit the .umaps (LFS).
- Play a course (rooftops or greybox) on an empty map; add `?shot=20` to screenshot after 20 s and quit
  (the game window takes keyboard focus, so type nothing meanwhile):
  `& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Unreal\Runline\Runline.uproject" '/Engine/Maps/Entry?game=/Script/Runline.RunlineRunGameMode?course=rooftops' -game -windowed -ResX=1600 -ResY=900`
- Golden data: `cargo run --release` in tools/golden (needs the reference clone at
  ../faith-runner). It rewrites RunlineCore/tests/golden/*.csv, Unreal/Runline/Data/Levels/*.csv and REFERENCE (the faith-runner
  commit used; currently 9470d755). Regenerate only from a clean checkout, and commit all three.
- Controller traces: `bash tools/trace/make-traces.sh` (Git Bash) applies tools/trace/faith-runner-trace.patch
  to a separate worktree ../faith-runner-trace (the reference clone stays clean), runs faith_move's
  tests with FAITH_TRACE_DIR set, and copies the traces named in tools/trace/selected.txt into
  RunlineCore/tests/golden/traces/. Commit them with TRACE_REFERENCE.

## Conventions
- RunlineCore: C++20, no Unreal headers. Metres, Y up. float everywhere, as in the Rust code.
- Port faithfully: same names (CamelCase), same constants, the Rust doc comments carried over.
- Unit/axis conversion to Unreal (cm, Z up) happens only in the Unreal adapter (M2).
- Every ported behaviour has a test; golden tests guard parity with the Rust original.
- Credit faith-runner; never commit Mirror's Edge files or decompiled scripts.
