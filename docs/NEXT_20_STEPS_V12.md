# protoPython: Next 20 Steps (v12)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v11) completed (steps 145–164).

**Status**: Completed.

---

## Steps 165–184

| Step | Description | Commit message |
|------|-------------|----------------|
| 165 | Builtin `breakpoint()` (no-op stub) | done |
| 166 | Builtin `globals()` (stub returning empty dict) | done |
| 167 | Builtin `locals()` (stub returning empty dict) | done |
| 168 | `str.center(width[, fillchar])` | done |
| 169 | `str.ljust(width[, fillchar])`, `str.rjust(width[, fillchar])` | done |
| 170 | `str.zfill(width)` | done |
| 171 | `str.partition(sep)`, `str.rpartition(sep)` | done |
| 172 | `list.copy()` method | done |
| 173 | Execution engine: `UNPACK_SEQUENCE` (tuple unpacking) | done |
| 174 | `itertools.islice(iterable, start, stop[, step])` | done (existing) |
| 175 | `itertools.repeat(object[, times])` | done |
| 176 | `bytes.hex()`, `bytes.fromhex(classmethod)` | done |
| 177 | `struct` module stub (pack, unpack) | done |
| 178 | `base64` module stub (b64encode, b64decode) | done |
| 179 | `random` module stub (random, randint) | done |
| 180 | Execution engine test for UNPACK_SEQUENCE | done |
| 181 | Foundation test for breakpoint/globals/locals | done |
| 182 | Update IMPLEMENTATION_PLAN Phase 3 opcode list | done |
| 183 | Create NEXT_20_STEPS_V12.md (this file) | done |
| 184 | Update todo for v12 completion | done |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v12).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R test_execution_engine` when runtime/CLI code changes.

---

## Notes

- **breakpoint**: No-op or minimal stub; real breakpoint would require debugger integration.
- **globals/locals**: Return empty dict stubs; full implementation needs frame access from execution engine.
- **UNPACK_SEQUENCE**: Pops a sequence, unpacks `arg` elements onto the stack (reverse order for correct stack semantics).
- **itertools.islice**: Iterator that yields elements from `start` to `stop` with optional `step`.
- **itertools.repeat**: Infinite or finite iterator yielding the same object.
- **Foundation test**: Prefer `test_foundation_filtered` or a single-test filter if full `test_foundation` is unstable (GC hang).
