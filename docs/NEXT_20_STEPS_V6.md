# protoPython: Next 20 Steps (v6)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v5) completed (steps 25–44).

---

## Steps 45–64

| Step | Description | Commit message |
|------|-------------|----------------|
| 45 | Builtin `repr()`: dispatch to `obj.__repr__()` | `feat(protoPython): repr builtin` |
| 46 | Builtin `format()`: dispatch to `obj.__format__(spec)` | `feat(protoPython): format builtin` |
| 47 | Builtin `open()`: delegate to `_io.open` | `feat(protoPython): open builtin` |
| 48 | Builtin `enumerate()`: return (index, value) iterator | `feat(protoPython): enumerate builtin` |
| 49 | Builtin `reversed()`: dispatch to `__reversed__` or fallback | `feat(protoPython): reversed builtin` |
| 50 | Builtin `sum()`: iterable summation with optional start | `feat(protoPython): sum builtin` |
| 51 | Builtins `all()` and `any()`: short-circuit boolean reduction | `feat(protoPython): all any builtins` |
| 52 | Execution engine: add `LOAD_NAME`, `STORE_NAME` opcodes | `feat(protoPython): exec LOAD_STORE` |
| 53 | Execution engine: add `BINARY_ADD`, `BINARY_SUBTRACT` | `feat(protoPython): exec binary ops` |
| 54 | Execution engine: add `CALL_FUNCTION` (positional only) | `feat(protoPython): exec call function` |
| 55 | functools module stub: `partial` | `feat(protoPython): functools partial` |
| 56 | itertools module stub: `islice`, `count` | `feat(protoPython): itertools stub` |
| 57 | re module stub: `compile`, `match`, `search` (basic) | `feat(protoPython): re module stub` |
| 58 | json module stub: `loads`, `dumps` (basic) | `feat(protoPython): json module stub` |
| 59 | float prototype: `__repr__`, `__str__`, `__bool__`, `__format__` | `feat(protoPython): float prototype` |
| 60 | bool type: proper subclass of int, `True`/`False` in builtins | `feat(protoPython): bool type` |
| 61 | sys.path as mutable list; append in CLI for `--path` | `feat(protoPython): sys.path mutable` |
| 62 | sys.modules dict: register loaded modules | `feat(protoPython): sys.modules` |
| 63 | Bytecode loader: load .pyc marshal header stub | `feat(protoPython): pyc marshal stub` |
| 64 | CTest: add `test_execution_engine` for minimal bytecode | `chore(protoPython): exec engine test` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md`.
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
