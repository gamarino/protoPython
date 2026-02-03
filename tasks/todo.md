# protoPython Task List

Derived from [IMPLEMENTATION_PLAN.md](../docs/IMPLEMENTATION_PLAN.md). See also [DESIGN.md](../DESIGN.md) and workspace [tasks/todo.md](../../tasks/todo.md).

## 1. Core Runtime & Object Model
- [ ] Phase 2: Full Method Coverage — all dunder methods for built-in types; first batch (list/dict `__getitem__`, `__setitem__`, `__len__`) [x]; second batch (list `__iter__`/`__next__`, dict key `__iter__`) [x]; third batch (list/dict `__contains__`) [x]; fourth batch (list/dict `__eq__`) [x]; fifth batch (list ordering dunders) [x]; sixth batch (list/dict `__repr__`, `__str__`) [x]; seventh batch (list/dict `__bool__`) [x]; eighth batch (tuple `__len__`) [x]; ninth batch (list slicing via slice list) [x]; tenth batch (tuple `__getitem__`, `__iter__`, `__contains__`, `__bool__`) [x]; eleventh batch (string `__iter__`, `__contains__`, `__bool__`, upper/lower) [x]; dict mutation helpers (`update`, `clear`, `copy`) [x]
## 1.1 Dict missing-key behavior
- [x] `__getitem__` returns `PROTO_NONE` for missing keys until exceptions are implemented
## 1.2 Dict views
- [x] `keys()`, `values()`, `items()` backed by `__keys__` and `__data__`
## 1.3 List methods
- [x] `pop()` and `extend()` implemented on list prototype
## 1.4 Dict accessors
- [x] `get()` and `setdefault()` implemented on dict prototype
## 1.5 protopy CLI
- [x] Exit codes and flags (`--module`, `--script`, `--path`, `--stdlib`) with tests
- [ ] Phase 3: Execution Engine — ProtoContext integration for bytecode execution (protopy)
- [x] Phase 4: GIL-less Concurrency — audit documented in [docs/GIL_FREE_AUDIT.md](../docs/GIL_FREE_AUDIT.md); fixes TBD

## 2. Standard Library Integration
- [x] Phase 2: Identify remaining C modules to replace (list in [docs/C_MODULES_TO_REPLACE.md](../docs/C_MODULES_TO_REPLACE.md))
- [ ] Phase 3: Native Optimization — replace performance-critical Python modules with C++

## 3. Compatibility & Testing
- [ ] Incremental Success — track compatibility percentage (run `test/regression/run_and_report.py` with `PROTOPY_BIN=<build>/src/runtime/protopy`; persist with `--output <path>` or `REGRTEST_RESULTS=<path>`; CTest: `regrtest_protopy_script`)
- [ ] Bug-for-Bug Compatibility — emulate CPython edge cases where safe

## 4. Debugging & IDE Support
- [ ] Protocol Support — DAP (line stepping, variable inspection, breakpoints)
- [ ] IDE Integration — VS Code (debugpy-like), PyCharm
- [ ] Step-through C++ Boundaries — debug across Python/C++ boundary

## 5. Compiler & Deployment (protopyc)
- [ ] AOT Compilation — .py to .so/.dll via C++
- [ ] Static Analysis — type hints for optimization
- [ ] Self-Hosting — protopyc compiles itself

## 6. Installation & Distribution
- [ ] Packaging — pip and wheels
- [ ] Virtual Environments — full venv support
- [ ] Drop-in replacement — protopy aliased to python

## 7. Next 20 Steps (v4)

See [docs/NEXT_20_STEPS_V4.md](../docs/NEXT_20_STEPS_V4.md). Steps 01–04 done; 05–24 planned.
- [x] Step 01: Stabilize foundation tests (CTest timeout 60s, docs/TESTING.md, test_minimal/test_protocore_minimal reproducers; root fix in protoCore GC pending)
- [x] Step 02: Tuple dunders (`__getitem__`, iter, contains, bool)
- [x] Step 03: String dunders & helpers (iter, contains, upper/lower)
- [x] Step 04: Dict mutation helpers (`update`, `clear`, `copy`)
- [x] Step 05: List mutation helpers (`insert`, `remove`, `clear`)
- [x] Step 06: Set prototype (`add`, `remove`, iter, len, contains)
- [x] Step 07: Exception scaffolding (Exception, KeyError, ValueError)
- [x] Step 08: Raise container exceptions (dict/list operations)
- [x] Step 09: Execution hook (`PythonEnvironment::executeModule`)
- [x] Step 10: Bytecode loader stub and CLI `--bytecode-only`
- [x] Step 11: Compatibility dashboard (history + pass %)
- [x] Step 12: Regression gating CTest target
- [x] Step 13: sys module parity (argv, exit, version info)
- [x] Step 14: Import resolution cache + invalidation
- [x] Step 15: Native math/operator helpers
- [x] Step 16: File I/O enhancements (modes, buffering, context)
- [x] Step 17: Logging/tracing utilities + CLI `--trace`
- [x] Step 18: Debug Adapter Protocol skeleton
- [x] Step 19: Interactive REPL mode (`protopy --repl`)
- [x] Step 20: Performance instrumentation (`sys.stats`)
- [x] Step 21: Type introspection (`type()`, isinstance expansion)
- [x] Step 22: String formatting (`format`, `__format__`)
- [x] Step 23: Bytes/bytearray prototype (basic)
- [x] Step 24: Collections extensions (defaultdict, OrderedDict basics)

---
*Updated from plan implementation. Mark items complete as work progresses.*
