# Conformity Test Suite (Phase 1)

Tests defined by `TEST_PLAN.md` (in the protoJS repo) for semantic correctness on the immutable protoCore engine. Combined progress status is in **protoJS/CONFORMITY_PROGRESS.md**.

## Layout

- **builtins/** — Phase 1.1: int, str, list, dict isolation (no in-place mutation; new roots propagated).
- **import/** — Phase 1.2: cross-module visibility, module identity, wrapper vs content.
- **bootstrap/** — Phase 1.3: manifest of minimal CPython `Lib/test` subset and runner.

## Running (protoPython)

From repo root, run each script with the protoPython binary, e.g.:

```bash
./build/protoPython tests/conformity/builtins/test_int_conformity.py
```

Or use the runner (requires protoPython on PATH or set `PROTO_PYTHON`):

```bash
python tests/conformity/run_conformity.py
```

Exit code 0 = all run tests passed; non-zero = failure or missing binary.

## Immutability verification

Run the `const_cast` check (from repo root):

```bash
./tests/conformity/scripts/check_const_cast.sh
```

Fails if forbidden `const_cast` is found in module/import paths.
