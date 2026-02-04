# protoPython: Next 20 Steps (v19)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v18) completed (steps 285–304).

**Status**: Completed.

---

## Steps 305–324

| Step | Description | Status |
|------|-------------|--------|
| 305 | str.removeprefix(), str.removesuffix() | done |
| 306 | bytes.split(), bytes.join() | done |
| 307 | set.symmetric_difference(), issubset(), issuperset() | done |
| 308 | functools.partial stub / class | done |
| 309 | operator module (add, sub, mul, truediv, eq, lt) | done |
| 310 | itertools.accumulate full implementation | done |
| 311 | re module stubs (compile, match, search, findall, sub) | done |
| 312 | math.isinf, isfinite, isnan, isclose | done |
| 314 | os.getcwd, os.chdir; os.path stubs | done |
| 315 | collections.defaultdict stub | done |
| 316 | builtins.super() stub | done |
| 317 | bytes.replace() | done |
| 318 | Foundation test set.union / set.copy | done |
| 319 | Foundation test math.isclose | done |
| 320 | Foundation test itertools.accumulate | done |
| 321–324 | Docs (NEXT_20_STEPS_V19, IMPLEMENTATION_PLAN, STUBS, todo) | done |

---

## Summary

- **itertools.accumulate**: Full implementation with default addition and optional binary function; supports int, float, string and `__add__` fallback.
- **Foundation tests**: SetUnion (copy + union return non-null; copy length), MathIsclose (same, close, far), ItertoolsAccumulate (list [1,2,3] → 1, 3, 6).
