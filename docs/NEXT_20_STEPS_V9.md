# protoPython: Next 20 Steps (v9)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v8) completed (steps 85–104).

**Status**: Completed.

---

## Steps 105–124

| Step | Description | Commit message |
|------|-------------|----------------|
| 105 | Builtin `filter(function, iterable)` | done |
| 106 | Builtin `map(function, iterable)` (single iterable) | done |
| 107 | Builtin `next(iter, default=None)` with optional second arg | done |
| 108 | Execution engine: `BINARY_SUBSCR` (subscript) | done |
| 109 | Execution engine: `BUILD_MAP` (dict from stack) | done |
| 110 | Fix or document `list.extend(iterable)` for non-list iterables | done |
| 111 | `typing` stub: `Any`, `List`, `Dict` (type hints) | done |
| 112 | `dataclasses` stub: `dataclass` decorator placeholder | done |
| 113 | `unittest` stub: `TestCase`, `main` placeholder | done |
| 114 | CTest: add regrtest run with `--output` to results dir | done (regrtest_persistence in 103) |
| 115 | Builtin `slice()` constructor | done (existing) |
| 116 | `str.find(sub)` and `str.index(sub)` on str prototype | done |
| 117 | `os.path.realpath`, `normpath` stubs | done |
| 118 | Execution engine test: BUILD_LIST in TestExecutionEngine (if not covered in 102) | done (102) |
| 119 | Builtin `isinstance` / `issubclass` extended for common ABCs | done (existing) |
| 120 | `functools.wraps` stub | done |
| 121 | `contextlib` stub: `contextmanager` placeholder | done |
| 122 | Foundation test for `sorted()` builtin | done |
| 123 | Document v9 scope in IMPLEMENTATION_PLAN | done |
| 124 | Update IMPLEMENTATION_PLAN and todo for v9 completion | done |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v9).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
