# protoPython: Next 20 Steps (v13)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v12) completed (steps 165–184).

**Status**: Completed.

---

## Steps 185–204

| Step | Description | Commit message |
|------|-------------|----------------|
| 185 | Builtin `input([prompt])` (stub returning empty str) | `feat(builtins): add input stub` |
| 186 | Execution engine: `LOAD_GLOBAL` | `feat(exec): add LOAD_GLOBAL opcode` |
| 187 | Execution engine: `STORE_GLOBAL` | done |
| 188 | Execution engine: `BUILD_SLICE` | done |
| 189 | `dict.popitem()` | done |
| 190 | `str.expandtabs([tabsize])` | done |
| 191 | `int.from_bytes(bytes, byteorder)` (stub/basic) | done |
| 192 | `int.to_bytes(length, byteorder)` (stub/basic) | done |
| 193 | `float.as_integer_ratio()` | done |
| 194 | `itertools.cycle(iterable)` | done |
| 195 | `itertools.takewhile(predicate, iterable)` | done |
| 196 | `time` module stub (time, sleep) | done |
| 197 | `datetime` module stub (datetime class) | done |
| 198 | `warnings` module stub (warn) | done |
| 199 | Execution engine test for LOAD_GLOBAL/STORE_GLOBAL | done |
| 200 | Execution engine test for BUILD_SLICE | done |
| 201 | Foundation test for dict.popitem | done |
| 202 | Update IMPLEMENTATION_PLAN Phase 3 opcode list | done |
| 203 | Create NEXT_20_STEPS_V13.md (this file) | done |
| 204 | Update todo for v13 completion | done |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v13).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R test_execution_engine` when runtime/CLI code changes.

---

## Notes

- **input**: Stub returning empty string; full implementation requires I/O and stdin handling.
- **LOAD_GLOBAL**: Load name from module/global namespace (names[index]); fallback to builtins.
- **STORE_GLOBAL**: Store TOS into global namespace at names[index].
- **BUILD_SLICE**: Pop `arg` values (2 or 3: start, stop[, step]), build slice object, push.
- **dict.popitem**: Remove and return an arbitrary (key, value) pair; raise KeyError if empty.
- **int.from_bytes / to_bytes**: Stub or basic implementation for common cases (little/big endian).
- **itertools.cycle**: Infinite iterator cycling over iterable.
- **itertools.takewhile**: Yield elements while predicate is true.
- **Foundation test**: Prefer `test_foundation_filtered` or a single-test filter if full `test_foundation` is unstable (GC hang).
