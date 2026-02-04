# protoPython: Next 20 Steps (v14)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v13) completed (steps 185–204).

**Status**: Completed.

---

## Steps 205–224

| Step | Description | Commit message |
|------|-------------|----------------|
| 205 | Builtin `object()` (no-arg constructor) | done |
| 206 | `str.capitalize()` | done |
| 207 | `str.title()` | done |
| 208 | `str.swapcase()` | done |
| 209 | `list.index(x[, start[, end]])` | done |
| 210 | `list.count(x)` | done |
| 211 | `tuple.index(x)`, `tuple.count(x)` | done |
| 212 | Execution engine: `ROT_TWO` | done |
| 213 | Execution engine: `DUP_TOP` | done |
| 214 | `itertools.dropwhile` | done |
| 215 | `itertools.tee` | done |
| 216 | `functools.partial` (verify) | skipped (existing) |
| 217 | `traceback` module stub | done |
| 218 | `io.StringIO` stub | done |
| 219 | Execution engine test for ROT_TWO, DUP_TOP | done |
| 220 | Foundation test for str.capitalize | done |
| 221 | Update IMPLEMENTATION_PLAN Phase 3 opcode list | done |
| 222 | Create NEXT_20_STEPS_V14.md | done |
| 223 | Update todo for v14 | done |
| 224 | Final v14 review | done |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v14).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R test_execution_engine` when runtime/CLI code changes.
