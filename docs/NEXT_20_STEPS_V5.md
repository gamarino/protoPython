# protoPython: Next 20 Steps (v5)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v4) completed (steps 01–24).

---

## Steps 25–44

| Step | Description | Commit message |
|------|-------------|----------------|
| 25 | Foundation test stability: document GC reproducer and add `--no-gc` workaround if feasible | `fix(protoPython): document gc reproducer` |
| 26 | Builtins: `callable`, `getattr`, `setattr`, `hasattr`, `delattr` | `feat(protoPython): add attr builtins` |
| 27 | Slice object prototype + `__getitem__` slice support for list/str | `feat(protoPython): add slice support` |
| 28 | frozenset prototype (immutable set, `__hash__`-capable) | `feat(protoPython): add frozenset prototype` |
| 29 | bytes/bytearray: `__iter__`, `fromiter` constructor stub | `feat(protoPython): extend bytes prototype` |
| 30 | defaultdict: integrate `default_factory` with `__getitem__` for missing keys | `feat(protoPython): defaultdict getitem` |
| 31 | File I/O: `read()`, `write()` on mock file objects | `feat(protoPython): file read write` |
| 32 | REPL: parse and execute single expressions via PythonModuleProvider | `feat(protoPython): repl execute` |
| 33 | Trace: wire `--trace` to `sys.settrace` and invoke on entry/exit | `feat(protoPython): trace integration` |
| 34 | `raise` support: builtin `raise` that sets pending exception | `feat(protoPython): raise builtin` |
| 35 | sys.stats: increment `calls` and `objects_created` in hot paths | `feat(protoPython): populate sys.stats` |
| 36 | `__hash__` for int, str, tuple, frozenset | `feat(protoPython): add __hash__` |
| 37 | `in` operator: builtin `__contains__` dispatch for `x in y` | `feat(protoPython): in operator` |
| 38 | `__format__` protocol: object fallback, str/ int formatting | `feat(protoPython): __format__ protocol` |
| 39 | Bytecode loader: parse .py AST or marshal stub | `feat(protoPython): bytecode parse stub` |
| 40 | Execution engine: execute simple bytecode (LOAD_CONST, RETURN_VALUE) | `feat(protoPython): minimal bytecode exec` |
| 41 | CTest: add foundation test filter for non-hanging tests | `chore(protoPython): foundation test filter` |
| 42 | logging module stub: `basicConfig`, `getLogger`, `info` | `feat(protoPython): logging stub` |
| 43 | operator module: `add`, `sub`, `mul`, `eq`, `lt` | `feat(protoPython): operator module` |
| 44 | math module stub: `sqrt`, `floor`, `ceil`, `pi` | `feat(protoPython): math module stub` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md`.
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation` and `ctest -R protopy_cli` when runtime/CLI code changes.
