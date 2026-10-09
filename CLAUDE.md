# Faith (working title)

Mirror's Edge movement ported from faith-runner (Rust) to a plain C++ core, then UE 5.7.
Spec: docs/superpowers/specs/2026-10-09-faith-movement-port-design.md

## Build and test
- `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`: configures, builds and runs all FaithCore tests (no Unreal needed).
- Golden data: `cargo run --release` in tools/golden (needs the reference clone at
  ../faith-runner). It rewrites FaithCore/tests/golden/*.csv; commit them.

## Conventions
- FaithCore: C++20, no Unreal headers. Metres, Y up. float everywhere, as in the Rust code.
- Port faithfully: same names (CamelCase), same constants, the Rust doc comments carried over.
- Unit/axis conversion to Unreal (cm, Z up) happens only in the Unreal adapter (M2).
- Every ported behaviour has a test; golden tests guard parity with the Rust original.
- Credit faith-runner; never commit Mirror's Edge files or decompiled scripts.
