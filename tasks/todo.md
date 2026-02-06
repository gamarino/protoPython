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
- [x] L-Shape architecture — context stack (ContextScope, promote), ProtoSpace singleton, TLS for env lookup, lock-free bootstrap diagnostic; see [docs/L_SHAPE_ARCHITECTURE.md](../docs/L_SHAPE_ARCHITECTURE.md)
- [x] L-Shape Execution Engine refactor — singleton enforcement (PythonEnvironment), escape analysis (hasDynamicLocalsAccess, MAPPED classification, slot fallback log), L_SHAPE_ARCHITECTURE work-stealing doc, POST_REFACTOR_AUDIT.md; see [docs/POST_REFACTOR_AUDIT.md](../docs/POST_REFACTOR_AUDIT.md)
- [x] For-loop / block suite parsing — Indent/Dedent in tokenizer; SuiteNode and parseSuite() so def/for/if bodies compile multiple statements; see [docs/FOR_LOOP_SUITE_FIX.md](../docs/FOR_LOOP_SUITE_FIX.md)
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

## 11. Next 20 Steps (v8) — completed

See [docs/NEXT_20_STEPS_V8.md](../docs/NEXT_20_STEPS_V8.md). Steps 85–104 completed.

## 12. Next 20 Steps (v9) — completed

See [docs/NEXT_20_STEPS_V9.md](../docs/NEXT_20_STEPS_V9.md). Steps 105–124 completed.

## 13. Next 20 Steps (v10) — completed

See [docs/NEXT_20_STEPS_V10.md](../docs/NEXT_20_STEPS_V10.md). Steps 125–144 completed.

## 14. Next 20 Steps (v11) — completed

See [docs/NEXT_20_STEPS_V11.md](../docs/NEXT_20_STEPS_V11.md). Steps 145–164 completed.

## 15. Next 20 Steps (v12) — completed

See [docs/NEXT_20_STEPS_V12.md](../docs/NEXT_20_STEPS_V12.md). Steps 165–184 done.

## 16. Next 20 Steps (v13) — completed

See [docs/NEXT_20_STEPS_V13.md](../docs/NEXT_20_STEPS_V13.md). Steps 185–204 done.

## 17. Next 20 Steps (v14) — completed

See [docs/NEXT_20_STEPS_V14.md](../docs/NEXT_20_STEPS_V14.md). Steps 205–224 done.

## 18. Next 20 Steps (v15) — completed

See [docs/NEXT_20_STEPS_V15.md](../docs/NEXT_20_STEPS_V15.md). Steps 225–244 done.

## 19. Next 20 Steps (v16) — completed

See [docs/NEXT_20_STEPS_V16.md](../docs/NEXT_20_STEPS_V16.md). Steps 245–264 done.

## 20. Next 20 Steps (v17) — completed

See [docs/NEXT_20_STEPS_V17.md](../docs/NEXT_20_STEPS_V17.md). Steps 265–284 done.

## 21. Next 20 Steps (v18) — completed

See [docs/NEXT_20_STEPS_V18.md](../docs/NEXT_20_STEPS_V18.md). Steps 285–304 done.

## 22. Stub completion — done

See [docs/STUBS.md](../docs/STUBS.md). Set union/intersection/difference implemented; itertools.accumulate full implementation; other itertools stubs return empty iterator; math.isclose; stdlib enhancements (tempfile, subprocess, os).

## 23. Next 20 Steps (v19) — completed

See [docs/NEXT_20_STEPS_V19.md](../docs/NEXT_20_STEPS_V19.md). Steps 305–324: itertools.accumulate full impl; foundation tests SetUnion, MathIsclose, ItertoolsAccumulate; docs updated.

## 24. Next 20 Steps (v20) — completed

See [docs/NEXT_20_STEPS_V20.md](../docs/NEXT_20_STEPS_V20.md). Steps 325–344: BINARY_POWER, BINARY_FLOOR_DIVIDE; math.log/log10/exp; operator.pow/floordiv/mod; str.isidentifier; bytes.count; list.__mul__/__rmul__; hasattr/delattr; functools.lru_cache, atexit, heapq, json stubs; foundation tests MathLog, ListRepeat, HasattrDelattr; STUBS and todo updated.

## 25. Next 20 Steps (v21) — completed

See [docs/NEXT_20_STEPS_V21.md](../docs/NEXT_20_STEPS_V21.md). Steps 345–364: UNARY_NEGATIVE, UNARY_NOT; math.sqrt/sin/cos; operator.neg/not_; str.isprintable; bytes.isdigit/isalpha; list.__reversed__/reversed(); dict.__or__/__ror__; getattr/slice repr; csv, xml.etree stubs; foundation tests MathSqrt, DictUnion, OperatorNegOrGetattr; STUBS and todo updated.

## 26. Next 20 Steps (v22) — completed

See [docs/NEXT_20_STEPS_V22.md](../docs/NEXT_20_STEPS_V22.md). Steps 365–384: UNARY_INVERT, POP_TOP; math.tan; operator.invert; str/bytes.isascii; setattr, callable; list __iadd__, dict __ior__/__iror__; html, difflib stubs; foundation and engine tests; STUBS and todo updated.

## 27. Next 20 Steps (v23) — completed

See [docs/NEXT_20_STEPS_V23.md](../docs/NEXT_20_STEPS_V23.md). Steps 385–404: UNARY_POSITIVE, NOP, INPLACE_ADD; math.asin/acos/atan; operator.lshift/rshift; bytes removeprefix/removesuffix; repr, id (existing); int.bit_count; shutil, mimetypes stubs; foundation and engine tests; STUBS and todo updated.

## 28. Next 20 Steps (v24) — completed

See [docs/NEXT_20_STEPS_V24.md](../docs/NEXT_20_STEPS_V24.md). Steps 405–424: BINARY_LSHIFT, BINARY_RSHIFT, INPLACE_SUBTRACT; math.atan2/degrees/radians; operator.and_/or_/xor; str.split maxsplit, len/bool, float.hex (existing); email, runpy stubs; foundation and engine tests; STUBS and todo updated.

## 29. Next 20 Steps (v25) — completed

See [docs/NEXT_20_STEPS_V25.md](../docs/NEXT_20_STEPS_V25.md). Steps 425–444: BINARY_AND, BINARY_OR, BINARY_XOR, INPLACE_MULTIPLY; math.hypot/fmod/log2/log1p; operator.index; str/bytes rsplit maxsplit, builtins (existing); urllib.parse, zoneinfo stubs; foundation and engine tests; STUBS and todo updated.

## 30. Next 20 Steps (v26) — completed

See [docs/NEXT_20_STEPS_V26.md](../docs/NEXT_20_STEPS_V26.md). Steps 445–464: INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_MODULO, INPLACE_POWER; math.remainder, math.erf; plistlib, quopri stubs; foundation and execution-engine tests.

- [x] Steps 445–464

## 31. Next 20 Steps (v27) — completed

See [docs/NEXT_20_STEPS_V27.md](../docs/NEXT_20_STEPS_V27.md). Steps 465–484: INPLACE_LSHIFT, INPLACE_RSHIFT, INPLACE_AND, INPLACE_OR, INPLACE_XOR; math.erfc, math.gamma, math.lgamma; operator (existing); binascii, uu stubs; foundation and execution-engine tests.

- [x] Steps 465–484

## 32. Next 20 Steps (v28) — completed

See [docs/NEXT_20_STEPS_V28.md](../docs/NEXT_20_STEPS_V28.md). Steps 485–504: ROT_THREE, ROT_FOUR; math.dist, math.perm, math.comb; operator (existing); configparser, getpass stubs; foundation and execution-engine tests.

- [x] Steps 485–504

## 33. Next 20 Steps (v29) — completed

See [docs/NEXT_20_STEPS_V29.md](../docs/NEXT_20_STEPS_V29.md). Steps 505–524: DUP_TOP_TWO; math.factorial, math.prod; operator (existing); pickle, logging stubs; foundation and execution-engine tests.

- [x] Steps 505–524

## 34. Next 20 Steps (v30) — completed

See [docs/NEXT_20_STEPS_V30.md](../docs/NEXT_20_STEPS_V30.md). Steps 525–544: math.isqrt, math.acosh/asinh/atanh; multiprocessing, ssl stubs; foundation tests MathIsqrt, MathAcosh; STUBS and todo updated.

- [x] Steps 525–544

## 35. Next 20 Steps (v31) — completed

See [docs/NEXT_20_STEPS_V31.md](../docs/NEXT_20_STEPS_V31.md). Steps 545–564: math.cosh, sinh, tanh; math.nan, inf; shelve, locale stubs; foundation tests MathCosh, MathSinh; STUBS and todo updated.

- [x] Steps 545–564

## 36. Next 20 Steps (v32) — completed

See [docs/NEXT_20_STEPS_V32.md](../docs/NEXT_20_STEPS_V32.md). Steps 565–584: math.ulp, math.nextafter; ctypes, sqlite3 stubs; foundation tests MathUlp, MathNextafter; STUBS and todo updated.

- [x] Steps 565–584

- [x] Step 145: ord builtin
- [x] Step 146: chr builtin
- [x] Step 147: bin builtin
- [x] Step 148: oct, hex builtins
- [x] Step 149: list.sort
- [x] Step 150: BUILD_TUPLE
- [x] Step 151: GET_ITER
- [x] Step 152: FOR_ITER
- [x] Step 153: argparse stub
- [x] Step 154: getopt stub
- [x] Step 155: tempfile stub
- [x] Step 156: subprocess stub
- [x] Step 157: str.encode, bytes.decode
- [x] Step 158: float.is_integer
- [x] Step 159: foundation test map
- [x] Step 160: exec engine test BUILD_TUPLE
- [x] Step 161: GET_ITER/FOR_ITER test
- [x] Steps 162–164: v11 completion docs
- [x] Step 125: divmod builtin
- [x] Step 126: ascii builtin
- [x] Step 127: exec STORE_SUBSCR
- [x] Step 128: str.count
- [x] Step 129: list.reverse
- [x] Step 130: dict.pop
- [x] Step 131: typing Optional, Union
- [x] Step 132: hashlib stub
- [x] Step 133: copy stub
- [x] Step 134: itertools.chain
- [x] Step 135/140: STORE_SUBSCR note
- [x] Step 136: int.bit_length
- [x] Step 137: str.rsplit
- [x] Step 138: foundation test filter
- [x] Step 139: IMPLEMENTATION_PLAN opcodes
- [x] Steps 141–144: v10 completion docs
- [x] Step 105: filter builtin
- [x] Step 106: map builtin
- [x] Step 107: next(iter, default=None)
- [x] Step 108: exec BINARY_SUBSCR
- [x] Step 109: exec BUILD_MAP
- [x] Step 110: list.extend documented (list-like only)
- [x] Step 111: typing stub (Any, List, Dict)
- [x] Step 112: dataclasses stub
- [x] Step 113: unittest stub
- [x] Step 114: CTest regrtest output (regrtest_persistence)
- [x] Step 115: slice builtin (existing)
- [x] Step 116: str.find, str.index
- [x] Step 117: os.path.realpath, normpath
- [x] Step 118: BUILD_LIST test (102)
- [x] Step 119: isinstance/issubclass (existing)
- [x] Step 120: functools.wraps stub
- [x] Step 121: contextlib stub
- [x] Step 122: foundation test sorted
- [x] Step 123: document v9 scope
- [x] Step 124: v9 completion docs
- [x] Step 95: benchmark range_iterate
- [x] Step 96: integrate benchmarks into harness and report
- [x] Step 97: vars builtin (stub)
- [x] Step 98: sorted builtin
- [x] Step 99: os.path stub
- [x] Step 100: pathlib stub
- [x] Step 101: collections.abc stub
- [x] Step 102: CTest exec engine LOAD_ATTR, STORE_ATTR, BUILD_LIST
- [x] Step 103: regrtest persistence (--output, REGRTEST_RESULTS, CTest regrtest_persistence)
- [x] Step 104: docs v8 completion
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

## 31. Next 20 Steps (v27) — completed

See [docs/NEXT_20_STEPS_V27.md](../docs/NEXT_20_STEPS_V27.md). Steps 465–484: INPLACE_LSHIFT, INPLACE_RSHIFT, INPLACE_AND, INPLACE_OR, INPLACE_XOR; math.erfc, math.gamma, math.lgamma; operator (existing); binascii, uu stubs; foundation and execution-engine tests.

- [x] Steps 465–484

## 32. Next 20 Steps (v33) — completed

See [docs/NEXT_20_STEPS_V33.md](../docs/NEXT_20_STEPS_V33.md). Steps 585–604: math.ldexp, math.frexp, math.modf, constant math.e; pprint, dis stubs; foundation test MathLdexpFrexpModfE; STUBS, todo, IMPLEMENTATION_PLAN, TESTING updated.

- [x] Steps 585–604

## 33. Next 20 Steps (v34) — completed

See [docs/NEXT_20_STEPS_V34.md](../docs/NEXT_20_STEPS_V34.md). Steps 605–624: math.cbrt, math.exp2, math.expm1, math.fma; ast, tokenize stubs; foundation test MathCbrtExp2Expm1Fma; STUBS, todo, IMPLEMENTATION_PLAN, TESTING updated.

- [x] Steps 605–624

## 34. Next 20 Steps (v35) — completed

See [docs/NEXT_20_STEPS_V35.md](../docs/NEXT_20_STEPS_V35.md). Steps 625–644: math.sumprod; inspect, types stubs; foundation test MathSumprod; STUBS, todo, IMPLEMENTATION_PLAN, TESTING updated.

- [x] Steps 625–644

## 35. Next 20 Steps (v36) — completed

See [docs/NEXT_20_STEPS_V36.md](../docs/NEXT_20_STEPS_V36.md). Steps 645–664: Concurrent execution model (GIL_FREE_AUDIT); protoCore compatibility (INSTALLATION); STUBS v36 section; IMPLEMENTATION_PLAN, todo, TESTING updated.

- [x] Steps 645–664

## 36. Next 20 Steps (v37) — completed

See [docs/NEXT_20_STEPS_V37.md](../docs/NEXT_20_STEPS_V37.md). Steps 665–684: atexit wired to shutdown; shutil.copyfile/copy; traceback; difflib.unified_diff; foundation tests AtexitRegister, ShutilCopyfile.

- [x] Steps 665–684

## 37. Next 20 Steps (v38) — completed

See [docs/NEXT_20_STEPS_V38.md](../docs/NEXT_20_STEPS_V38.md). Steps 685–704: _thread native module resolution fix; statistics.mean/median/stdev; urllib.parse.quote/unquote/urljoin; dataclasses default values; unittest assertRaises/assertIn/main; contextlib full impl; foundation tests.

- [x] Steps 685–704

## 38. Next 20 Steps (v39) — completed

See [docs/NEXT_20_STEPS_V39.md](../docs/NEXT_20_STEPS_V39.md). Steps 705–724: _os.environ_keys; os.environ from environment; argparse; configparser; getpass; foundation tests OsEnviron, ArgparseBasic, ConfigParserRead.

- [x] Steps 705–724

## 39. Next 20 Steps (v40) — completed

See [docs/NEXT_20_STEPS_V40.md](../docs/NEXT_20_STEPS_V40.md). Steps 725–744: difflib.SequenceMatcher; ast.literal_eval; inspect; decimal.Decimal; fractions.Fraction; types; foundation tests DifflibSequenceMatcher, AstLiteralEval, DecimalBasic.

- [x] Steps 725–744

## 40. Next 20 Steps (v41) — completed

See [docs/NEXT_20_STEPS_V41.md](../docs/NEXT_20_STEPS_V41.md). Steps 745–764: struct.calcsize; _os.listdir/remove/rmdir; os.path.exists/isdir; shutil.move and rmtree; foundation tests StructCalcsize, PathlibPath, ShutilMove.

- [x] Steps 745–764

## 41. Next 20 Steps (v42) — completed

See [docs/NEXT_20_STEPS_V42.md](../docs/NEXT_20_STEPS_V42.md). Steps 765–784: tempfile.mkstemp unique path; tempfile.TemporaryFile file-like; io.TextIOWrapper; mimetypes; foundation tests TempfileMkstemp, IoTextIOWrapper, MimetypesGuess.

- [x] Steps 765–784

## 42. Next 20 Steps (v43) — completed

See [docs/NEXT_20_STEPS_V43.md](../docs/NEXT_20_STEPS_V43.md). Steps 785–804: pathlib.Path exists, is_dir, is_file, mkdir; decimal __mul__/__truediv__; fractions __mul__; io.BytesIO; foundation tests PathlibExistsIsdir, DecimalMul, IoBytesIO.

- [x] Steps 785–804

## 43. Next 20 Steps (v44) — completed

See [docs/NEXT_20_STEPS_V44.md](../docs/NEXT_20_STEPS_V44.md). Steps 805–824: pathlib.Path read_text/write_text; os.path.isfile; functools.partial; datetime date/timedelta; foundation tests PathlibReadText, FunctoolsPartial, DatetimeDate.

- [x] Steps 805–824

## 44. Next 20 Steps (v45) — completed

See [docs/NEXT_20_STEPS_V45.md](../docs/NEXT_20_STEPS_V45.md). Steps 825–844: pickle loads/dumps minimal; logging Handler/Formatter/StreamHandler; email message_from_string headers; foundation tests PickleLoadsDumps, LoggingHandler, EmailParse.

- [x] Steps 825–844

## 45. Next 20 Steps (v46) — completed

See [docs/NEXT_20_STEPS_V46.md](../docs/NEXT_20_STEPS_V46.md). Steps 845–864: secrets token_hex/token_urlsafe; subprocess.run args; regrtest persistence docs; C_MODULES_TO_REPLACE update; foundation tests SecretsTokenHex, SubprocessRun.

- [x] Steps 845–864

## 46. Next 20 Steps (v47) — completed

See [docs/NEXT_20_STEPS_V47.md](../docs/NEXT_20_STEPS_V47.md). Steps 865–884: HPY_INTEGRATION_PLAN; PACKAGING_ROADMAP; venv stub; lessons.md; IMPLEMENTATION_PLAN v43–v47 summary.

- [x] Steps 865–884

## 47. Next 20 Steps (v48) — completed

See [docs/NEXT_20_STEPS_V48.md](../docs/NEXT_20_STEPS_V48.md). Steps 885–904: Foundation test fixes (SetBasic, MathLog, MathDist, SetattrAndCallable with direct setAttribute/getAttribute and protoCore immutable model, MapBuiltin, StringDunders); OperatorInvert DISABLED_; TESTING, STUBS, todo updated.

- [x] Steps 885–904

## 48. Next 20 Steps (v49) — in progress

See [docs/NEXT_20_STEPS_V49.md](../docs/NEXT_20_STEPS_V49.md). Steps 905–924: v48 doc completion; ThreadModule stack-use-after-scope fix done; OperatorInvert DISABLED_ (direct asMethod returns nullptr; Python path works); SetattrAndCallable uses direct setAttribute/getAttribute; py_setattr/py_getattr (obj from posArgs) and py_log10 fixes deferred.

- [ ] Steps 905–924 (ThreadModule done; OperatorInvert, py_setattr, py_log10 deferred)

## 49. Next 20 Steps (v50) — completed

See [docs/NEXT_20_STEPS_V50.md](../docs/NEXT_20_STEPS_V50.md). Steps 925–944: Create v50 doc; tasks/todo.md v49/v50; IMPLEMENTATION_PLAN v48 done, v49/v50 in progress; STUBS and TESTING updated; lessons captured; commit.

- [x] Steps 925–944 (doc and commit)

## 50. Next 20 Steps (v51) — completed

See [docs/NEXT_20_STEPS_V51.md](../docs/NEXT_20_STEPS_V51.md). Steps 945–964: Create v51 doc; tasks/todo.md v51; IMPLEMENTATION_PLAN v50 done, v51 in progress; TESTING, STUBS v51 updates; protoCore immutable model documented; OperatorInvert, py_setattr/py_log10/ThreadModule status.

- [x] Steps 945–964

## 51. Next 20 Steps (v52) — completed

See [docs/NEXT_20_STEPS_V52.md](../docs/NEXT_20_STEPS_V52.md). Steps 965–984: Create v52 doc; tasks/todo.md v52; IMPLEMENTATION_PLAN v51 done, v52 in progress; TESTING, STUBS v52 updates; lessons consolidated; documentation and changes committed.

- [x] Steps 965–984

## 52. Next 20 Steps (v53) — completed

See [docs/NEXT_20_STEPS_V53.md](../docs/NEXT_20_STEPS_V53.md). Steps 985–1004: FilterBuiltin passes; ThreadModule fix; py_log10 workaround; regrtest persistence verified; foundation suite full vs filtered documented; tasks/todo.md v53; IMPLEMENTATION_PLAN v52 done, v53 in progress; STUBS, TESTING updated.

- [x] Steps 985–1004

## 53. Next 20 Steps (v54) — completed

See [docs/NEXT_20_STEPS_V54.md](../docs/NEXT_20_STEPS_V54.md). Steps 1005–1024: Regrtest dashboard; compatibility tracking; run_and_validate_output; C_MODULES_TO_REPLACE (_operator Replaced, _functools Partial); foundation test filter; TESTING, STUBS v54 updates.

- [x] Steps 1005–1024

---
*Updated from plan implementation. Mark items complete as work progresses.*
