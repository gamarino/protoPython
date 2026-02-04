# protoPython: Next 20 Steps (v8)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v7) completed (steps 65–84).

**Status**: In progress.

---

## Steps 85–104

| Step | Description | Commit message |
|------|-------------|----------------|
| 85 | Builtin `pow()`: base, exp, mod | `feat(protoPython): pow builtin` |
| 86 | Builtin `round()`: basic (n, ndigits=0) | `feat(protoPython): round builtin` |
| 87 | str `strip()`, `lstrip()`, `rstrip()` | `feat(protoPython): str strip` |
| 88 | str `replace()` | `feat(protoPython): str replace` |
| 89 | str `startswith()`, `endswith()` | `feat(protoPython): str startswith endswith` |
| 90 | Builtin `zip()` | `feat(protoPython): zip builtin` |
| 91 | Execution engine: `LOAD_ATTR`, `STORE_ATTR` | `feat(protoPython): exec load store attr` |
| 92 | Execution engine: `BUILD_LIST` | `feat(protoPython): exec build list` |
| 93 | Benchmark: `list_append_loop` | `feat(protoPython): benchmark list append` |
| 94 | Benchmark: `str_concat_loop` | `feat(protoPython): benchmark str concat` |
| 95 | Benchmark: `range_iterate` | done |
| 96 | Integrate new benchmarks into harness and report | done |
| 97 | Builtin `vars()`: return object `__dict__` (stub) | done |
| 98 | Builtin `sorted()`: return sorted list | done |
| 99 | os.path stub: `join`, `exists`, `isdir` | `feat(protoPython): os.path stub` |
| 100 | pathlib stub: `Path` class basics | `feat(protoPython): pathlib stub` |
| 101 | collections.abc stub: `Iterable`, `Sequence` | `feat(protoPython): collections.abc stub` |
| 102 | CTest: add `test_execution_engine` for LOAD_ATTR, BUILD_LIST | `chore(protoPython): exec engine tests v8` |
| 103 | Incremental success: run_and_report `--output` persistence | `feat(protoPython): regrtest persistence` |
| 104 | Update IMPLEMENTATION_PLAN and docs for v8 | `docs(protoPython): v8 completion` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md`.
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
