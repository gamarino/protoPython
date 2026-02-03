# protoPython: Next 20 Steps (v4)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Completed**: Steps 01–24 (all planned steps).

---

## Steps 05–20 (from v3)

| Step | Description | Commit message |
|------|-------------|----------------|
| 05 | List mutation helpers (`insert`, `remove`, `clear`) | `feat(protoPython): add list mutation helpers` |
| 06 | Set prototype (`add`, `remove`, iter, len, contains) | `feat(protoPython): introduce set prototype` |
| 07 | Exception scaffolding (Exception, KeyError, ValueError) | `feat(protoPython): scaffold exception hierarchy` |
| 08 | Raise container exceptions (dict/list operations) | `feat(protoPython): raise container exceptions` |
| 09 | Execution hook (`PythonEnvironment::executeModule`) | `feat(protoPython): add execution hook scaffolding` |
| 10 | Bytecode loader stub + CLI `--bytecode-only` | `feat(protoPython): add bytecode loader stub` |
| 11 | Compatibility dashboard (history + pass %) | `feat(protoPython): add compatibility dashboard` |
| 12 | Regression gating CTest target | `chore(protoPython): gate regressions in ctest` |
| 13 | sys module parity (argv, exit, version info) | `feat(protoPython): expand sys module` |
| 14 | Import resolution cache + invalidation | `feat(protoPython): add import cache` |
| 15 | Native math/operator helpers | `feat(protoPython): add math/operator natives` |
| 16 | File I/O enhancements (modes, buffering, context) | `feat(protoPython): enhance file io` |
| 17 | Logging/tracing utilities + CLI `--trace` | `feat(protoPython): add logging utilities` |
| 18 | Debug Adapter Protocol skeleton | `feat(protoPython): scaffold DAP support` |
| 19 | Interactive REPL mode (`protopy --repl`) | `feat(protoPython): add repl mode` |
| 20 | Performance instrumentation (`sys.stats`) | `feat(protoPython): add runtime metrics` |

## Steps 21–24 (new)

| Step | Description | Commit message |
|------|-------------|----------------|
| 21 | Type introspection (`type()`, `isinstance` expansion) | `feat(protoPython): expand type introspection` |
| 22 | String formatting (`format`, `__format__`) | `feat(protoPython): add string formatting` |
| 23 | Bytes/bytearray prototype (basic) | `feat(protoPython): add bytes prototype` |
| 24 | Collections extensions (defaultdict, OrderedDict basics) | `feat(protoPython): add defaultdict and OrderedDict` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md`.
3. Add any step-specific doc (e.g. `docs/SET_SUPPORT.md`).
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation` and `ctest -R protopy_cli` when runtime/CLI code changes.
