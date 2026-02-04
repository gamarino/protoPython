# protoPython: Next 20 Steps (v7)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v6) completed (steps 45–64).

---

## Steps 65–84

| Step | Description | Commit message |
|------|-------------|----------------|
| 65 | Builtin `range()`: start, stop, step; iterator with `__iter__`/`__next__` | `feat(protoPython): range builtin` |
| 66 | Builtin `print()`: positional args, sep, end, flush (basic) | `feat(protoPython): print builtin` |
| 67 | Execution engine: `BINARY_MULTIPLY`, `BINARY_TRUE_DIVIDE` | `feat(protoPython): exec multiply divide` |
| 68 | Execution engine: `COMPARE_OP` (eq, lt, le, gt, ge, ne) | `feat(protoPython): exec compare op` |
| 69 | Execution engine: `POP_JUMP_IF_FALSE`, `JUMP_ABSOLUTE` | `feat(protoPython): exec jump opcodes` |
| 70 | Builtin `dir()`: return list of object attribute names | `feat(protoPython): dir builtin` |
| 71 | Builtin `hash()`: dispatch to `obj.__hash__()` | `feat(protoPython): hash builtin` |
| 72 | Builtin `id()`: return object identity (stub) | `feat(protoPython): id builtin` |
| 73 | str `split()` method: split by separator | `feat(protoPython): str split` |
| 74 | str `join()` method: join iterable of strings | `feat(protoPython): str join` |
| 75 | Benchmark harness: script that runs protopy vs cpython, records wall time | `feat(protoPython): benchmark harness` |
| 76 | Benchmark: `startup_empty` (import minimal module) | `feat(protoPython): benchmark startup` |
| 77 | Benchmark: `int_sum_loop` (sum 1..N) | `feat(protoPython): benchmark int sum` |
| 78 | Benchmark report: human-readable markdown table (§7.2 format) | `feat(protoPython): benchmark report` |
| 79 | CTest: add benchmark target (optional, off by default) | `chore(protoPython): benchmark ctest` |
| 80 | `getattr` third-argument default when attr missing | `feat(protoPython): getattr default` |
| 81 | `sys.getsizeof` stub (return 0 or placeholder) | `feat(protoPython): sys getsizeof` |
| 82 | Fix `isinstance(x, bool)` when bool subclass of int | `fix(protoPython): isinstance bool` |
| 83 | CTest: add `test_execution_engine` for LOAD_NAME, BINARY_ADD, CALL_FUNCTION | `chore(protoPython): exec engine tests` |
| 84 | Update IMPLEMENTATION_PLAN and docs for v7 completion | `docs(protoPython): v7 completion` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md`.
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
