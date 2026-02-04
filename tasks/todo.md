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

## 7. Next 20 Steps (v4) — completed

See [docs/NEXT_20_STEPS_V4.md](../docs/NEXT_20_STEPS_V4.md). Steps 01–24 completed.

## 8. Next 20 Steps (v5) — completed

See [docs/NEXT_20_STEPS_V5.md](../docs/NEXT_20_STEPS_V5.md). Steps 25–44 completed.

## 9. Next 20 Steps (v6) — completed

See [docs/NEXT_20_STEPS_V6.md](../docs/NEXT_20_STEPS_V6.md). Steps 45–64 completed.

## 10. Next 20 Steps (v7)

See [docs/NEXT_20_STEPS_V7.md](../docs/NEXT_20_STEPS_V7.md). Steps 65–84 completed.

## 11. Next 20 Steps (v8)

See [docs/NEXT_20_STEPS_V8.md](../docs/NEXT_20_STEPS_V8.md). Steps 85–104 planned.
- [x] Step 95: benchmark range_iterate
- [x] Step 96: integrate benchmarks into harness and report
- [x] Step 97: vars builtin (stub)
- [x] Step 98: sorted builtin
- [x] Step 99: os.path stub
- [x] Step 100: pathlib stub
- [x] Step 85: pow builtin
- [x] Step 86: round builtin
- [x] Step 87: str strip lstrip rstrip
- [x] Step 88: str replace
- [x] Step 89: str startswith endswith
- [x] Step 90: zip builtin
- [x] Step 91: LOAD_ATTR STORE_ATTR
- [x] Step 92: BUILD_LIST opcode
- [x] Step 93: benchmark list_append_loop
- [x] Step 94: benchmark str_concat_loop
- [x] Step 45: repr builtin
- [x] Step 46: format builtin
- [x] Step 47: open builtin
- [x] Step 48: enumerate builtin
- [x] Step 49: reversed builtin
- [x] Step 50: sum builtin
- [x] Step 51: all any builtins
- [x] Step 52: exec LOAD_NAME, STORE_NAME
- [x] Step 53: exec BINARY_ADD, BINARY_SUBTRACT
- [x] Step 54: exec CALL_FUNCTION
- [x] Step 55: functools partial
- [x] Step 56: itertools islice, count
- [x] Step 57: re module stub
- [x] Step 58: json module stub
- [x] Step 59: float prototype
- [x] Step 60: bool type
- [x] Step 61: sys.path mutable (--path appends)
- [x] Step 62: sys.modules register loaded
- [x] Step 63: pyc marshal stub
- [x] Step 64: test_execution_engine
- [x] Step 65: range builtin (iterator)
- [x] Step 66: print builtin (sep, end)
- [x] Step 67: exec BINARY_MULTIPLY, BINARY_TRUE_DIVIDE
- [x] Step 68: exec COMPARE_OP
- [x] Step 69: exec POP_JUMP_IF_FALSE, JUMP_ABSOLUTE
- [x] Step 70: dir builtin (stub)
- [x] Step 71: hash builtin
- [x] Step 72: id builtin (already present)
- [x] Step 73: str split
- [x] Step 74: str join
- [x] Step 80: getattr default (already present)
- [x] Step 81: sys.getsizeof
- [x] Step 82: isinstance bool fix
- [x] Step 75: benchmark harness
- [x] Step 76: benchmark startup_empty
- [x] Step 77: benchmark int_sum_loop
- [x] Step 78: benchmark report
- [x] Step 79: benchmark CTest (optional -DPROTOPY_BENCHMARKS=ON)
- [x] Step 83: exec engine tests (LOAD_NAME, BINARY_ADD, CALL_FUNCTION)
- [x] Step 25: Foundation test stability / GC reproducer documentation
- [x] Step 26: Builtins callable, getattr, setattr, hasattr, delattr
- [x] Step 27: Slice object + __getitem__ slice support for list/str
- [x] Step 28: frozenset prototype
- [x] Step 29: bytes __iter__, fromiter constructor stub
- [x] Step 30: defaultdict default_factory __getitem__
- [x] Step 31: File I/O read, write on mock files
- [x] Step 32: REPL parse and execute (module resolve)
- [x] Step 34: raise builtin
- [x] Step 36: __hash__ for int, str, tuple, frozenset
- [x] Step 39: Bytecode parse stub
- [x] Step 41: CTest foundation test filter
- [x] Step 42: logging module stub
- [x] Step 43: operator module
- [x] Step 44: math module stub
- [x] Step 33: Trace integration (--trace wires settrace, invoke on entry/exit)
- [x] Step 35: sys.stats increment (calls)
- [x] Step 37: in operator (builtin alias for contains)
- [x] Step 38: __format__ protocol (object, str, int)
- [x] Step 40: Minimal bytecode exec (LOAD_CONST, RETURN_VALUE)
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
