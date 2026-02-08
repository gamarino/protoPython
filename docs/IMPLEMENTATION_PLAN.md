# ProtoPython: Comprehensive Implementation Plan

**Status (2026-02):** Phase 5 (HPy, REPL, error reporting) complete. Phase 6 (v73–v77: full stubs, threading, networking) in progress. See [tasks/todo.md](../tasks/todo.md) for step-level tracking.

This document outlines the roadmap for turning `protoPython` into a GIL-less, high-performance Python 3.14 replacement built on `protoCore`.

## 1. Core Runtime & Object Model (In Progress)
The foundation of the runtime must be fully compatible with Python 3.14's semantics while leveraging `protoCore`'s fine-grained locking and immutable-by-default sharing.

- [x] **Phase 1: Foundation**: Set up `PythonEnvironment`, `object`, `type`, `int`, `str`, `list`, `dict`.
- [x] **Phase 2: Full Method Coverage** (Next 20 Steps v4: Steps 01–24 done): Implement all dunder methods for built-in types. First batch: `__getitem__`, `__setitem__`, `__len__` for list and dict [done]. Second batch: list `__iter__`/`__next__` [done], dict key iteration [done via `__keys__` list]. Third batch: list/dict `__contains__` [done]. Fourth batch: list/dict `__eq__` [done]. Fifth batch: list ordering dunders `__lt__`, `__le__`, `__gt__`, `__ge__` [done]; dict ordering returns `PROTO_NONE` (unsupported for now). Sixth batch: list/dict `__repr__` and `__str__` [done]. Seventh batch: list/dict `__bool__` [done]. Eighth batch: tuple `__len__` and builtins tuple registration [done]. Ninth batch: list slicing for `__getitem__` with slice spec `[start, stop, step]` (step=1 only) [done]. Tenth batch: tuple `__getitem__`, `__iter__`, `__contains__`, `__bool__` [done]. Eleventh batch: string `__iter__`, `__contains__`, `__bool__`, `upper()`, `lower()`, `split()`, `join()` [done]; raw string iteration has known issues.
- [x] **Missing-key behavior**: dict `__getitem__` raises KeyError for missing keys; see [EXCEPTIONS.md](EXCEPTIONS.md).
- [x] **Dict views**: `keys()`, `values()`, `items()` backed by `__keys__` and `__data__`.
- [x] **List methods**: `pop()`, `extend()`, `insert()`, `remove()`, `clear()`. Note: `list.extend(iterable)` accepts only list-like objects (with `__data__`/list); arbitrary iterables (e.g. `range`, `map`) are not supported—use a loop with `append()`.
- [x] **Dict accessors**: `get()` and `setdefault()`.
- [x] **Dict mutation helpers**: `update()`, `clear()`, `copy()` (shallow copy).
- [x] **Set prototype**: `add`, `remove`, `__len__`, `__contains__`, `__bool__`, `__iter__`; see [SET_SUPPORT.md](SET_SUPPORT.md).
- [x] **Exception scaffolding**: `exceptions` module with `Exception`, `KeyError`, `ValueError`; see [EXCEPTIONS.md](EXCEPTIONS.md).
- [x] **protopy CLI**: distinct exit codes and flags (`--module`, `--script`, `--path`, `--stdlib`, `--bytecode-only`, `--trace`, `--repl`) with tests.
- [x] **Phase 2b: Foundation test stability**: Startup/GC hang resolved when using protoCore with the getFreeCells deadlock fix (caller must park and wait for GC when out of cells). Verify with `test_minimal` and `protopy --module abc`.
- [x] **Step 5 (optional): Parser/compiler full opcode set**: Tokenizer (Dot, LSquare, RSquare, LCurly, RCurly, Colon, Assign; keywords for, in, if, else, global). Parser: AST nodes AttributeNode, SubscriptNode, SliceNode, ListLiteralNode, DictLiteralNode, TupleLiteralNode, AssignNode, ForNode, IfNode, GlobalNode; grammar for statements and expressions. Compiler: LOAD_ATTR, STORE_ATTR, BINARY_SUBSCR, STORE_SUBSCR, BUILD_LIST, BUILD_MAP, BUILD_TUPLE, BUILD_SLICE, GET_ITER, FOR_ITER, JUMP_ABSOLUTE, POP_JUMP_IF_FALSE, LOAD_GLOBAL, STORE_GLOBAL; forward-jump patching. CTest: HandBuiltStoreNameInFrame, CompiledAssignment, CompiledListLiteral.
- [x] **Phase 3: Execution Engine**: `executeMinimalBytecode` supports LOAD_CONST, RETURN_VALUE, LOAD_NAME, STORE_NAME, BINARY_ADD/SUBTRACT/MULTIPLY/TRUE_DIVIDE/MODULO/POWER/FLOOR_DIVIDE, BINARY_LSHIFT, BINARY_RSHIFT, BINARY_AND, BINARY_OR, BINARY_XOR, COMPARE_OP, POP_JUMP_IF_FALSE, JUMP_ABSOLUTE, CALL_FUNCTION, LOAD_ATTR, STORE_ATTR, BUILD_LIST, BINARY_SUBSCR, BUILD_MAP, STORE_SUBSCR, BUILD_TUPLE, GET_ITER, FOR_ITER, UNPACK_SEQUENCE, LOAD_GLOBAL, STORE_GLOBAL, BUILD_SLICE, ROT_TWO, DUP_TOP, UNARY_NEGATIVE, UNARY_NOT, UNARY_INVERT, UNARY_POSITIVE, POP_TOP, NOP, INPLACE_ADD, INPLACE_SUBTRACT, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_MODULO, INPLACE_POWER, INPLACE_LSHIFT, INPLACE_RSHIFT, INPLACE_AND, INPLACE_OR, INPLACE_XOR, ROT_THREE, ROT_FOUR, DUP_TOP_TWO. protopy invokes module `main` when running a script. CTest `test_execution_engine` covers these opcodes.
- [x] **Phase 4: GIL-less Concurrency**: Audit all mutable operations to ensure thread-safety using `protoCore` primitives. Audit and fixes completed; see [POST_REFACTOR_AUDIT.md](POST_REFACTOR_AUDIT.md). Trace, pending exception, context registration, and resolve cache made thread-safe; sys.path/sys.modules and deque policy documented; concurrent execution test added (serialized allocation to respect ProtoSpace/ProtoContext/ProtoThread).

## 2. HPy Support for Imported Modules

[HPy](https://hpyproject.org/) is a portable API standard for writing Python C extension modules, designed to work across CPython, PyPy, GraalPy, and other implementations. It avoids exposing CPython implementation details (e.g. reference counting, GIL) and is better suited for GIL-less or alternative memory management.

**Implementation plan:** 100 steps (1185–1284) in 5 blocks of 20 (v63–v67). v63: Phase 1 foundation (context, handle table, core ABI). v64–v67: Phase 1 completion, Phase 2–4. See [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md) and [tasks/todo.md](../tasks/todo.md).

- [x] **Phase 1: HPy runtime shim** (v63): HPy context and handle table implemented; core ABI done.
- [x] **Phase 2: HPy universal ABI** (v64): Support loading HPy modules built against the universal ABI. `HPyModuleProvider` implemented. Done (v64).
- [x] **Phase 3: HPy API coverage** (v65–v66): Implement additional HPy ABI (Object creation, numeric/string, protocols, collections, type creation). Done (v66).
- [x] **Phase 4: Ecosystem compatibility** (v67): Documented build/distribute; final tests. Done (v67).
- [x] **Phase 5: Interactive REPL & UX** (v68–v72): Implement CPython-style REPL, high-quality error messages, and final project polish. Phase 5.4 done (v71: advanced REPL features, help/dir, auto-indent, %debug). Phase 5.5 pending (v72).
- [ ] **Phase 6: Implementation Completion & Full Stubs** (v73–v77): Complete core builtins, full native threading, networking foundation, and comprehensive stub implementation. 100 steps (1385–1484). See [tasks/todo.md](../tasks/todo.md).

*References: [HPy Documentation](https://docs.hpyproject.org/), [HPy API](https://docs.hpyproject.org/en/latest/api.html), [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md).*

## 3. Standard Library Integration
The goal is to provide a complete standard library that behaves identically to CPython's.

- [x] **Phase 1: StdLib Shell**: Copy the Python 3.14 `.py` standard library into `lib/python3.14/`.
- [x] **Phase 2: C-to-C++ Replacement**:
    - [x] Initial `builtins` and `sys` native implementation.
    - [x] Basic `_io` module for file operations (Phase 4).
    - [x] Identify remaining modules traditionally implemented in C (e.g., `_collections`, `_functools`, `_ssl`, `_socket`); see [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md).
    - [x] Native stubs: `os.path` (join, exists, isdir), `pathlib` (Path basics), `collections.abc` (Iterable, Sequence).
    - Ensure these modules are GIL-less from the start.
- [ ] **Phase 3: Native Optimization**: Progressively replace performance-critical Python modules with C++ implementations.
- **Standard library stub implementation**: See [STUBS.md](STUBS.md) and [FULL_STUB_IMPLEMENTATION.md](FULL_STUB_IMPLEMENTATION.md). Phase 0 (tokenizer, parser, compiler, code object, compile/eval/exec) done; Phase 1 (binascii, uu, quopri, pprint, dis, random, statistics) implemented; Phase 2 (tokenize, runpy; ast stub); Phase 3 (native time module; os/subprocess/threading etc. stubs); Phase 4 (hashlib, ssl, sqlite3, ctypes stubs until external libs linked).

## 4. Compatibility & Testing
We aim for "No-Modification" compatibility with CPython tests.

- [x] **Integration of `test.regrtest`**: Set up a harness to run CPython's test suite directly.
- [x] **Incremental Success** (v54): Track compatibility percentage across the test suite via `test/regression/run_and_report.py` and `dashboard.py`. Results persisted to JSON via `--output <path>` or `REGRTEST_RESULTS=<path>`. JSON format: `passed`, `failed`, `total`, `compatibility_pct`, `timestamp`, `failed_tests`. History (last 100 runs) via `REGRTEST_HISTORY` or `results/history.json`. Dashboard: `python test/regression/dashboard.py` or `REGRTEST_HISTORY=<path>`. CTest `regrtest_persistence` (via `run_and_validate_output.py`) exercises persistence. See [COMPATIBILITY_DASHBOARD.md](COMPATIBILITY_DASHBOARD.md).
- [ ] **Bug-for-Bug Compatibility**: Where safe, emulate CPython edge cases to ensure existing code works without changes.

**Completed: Next 20 Steps v8 (85–104).** Delivered: builtins `pow`, `round`, `zip`, `sorted`, `vars`; str methods (strip, replace, startswith, endswith); execution engine LOAD_ATTR, STORE_ATTR, BUILD_LIST; benchmarks (list_append_loop, str_concat_loop, range_iterate) and harness; native stubs for `os.path`, `pathlib`, `collections.abc`; CTest coverage for LOAD_ATTR/STORE_ATTR/BUILD_LIST; regrtest persistence (`--output`, `REGRTEST_RESULTS`, CTest `regrtest_persistence`).

**Completed: Next 20 Steps v9 (105–124).** Delivered: builtins `filter`, `map`, `next(iter, default)`; execution engine BINARY_SUBSCR, BUILD_MAP; list.extend limitation documented; stubs: `typing` (Any, List, Dict), `dataclasses`, `unittest`, `functools.wraps`, `contextlib`; `os.path.realpath`/`normpath`; str `find`/`index`; foundation test for `sorted`; v9 scope and completion documented.

**Completed: Next 20 Steps v10 (125–144).** Delivered: builtins `divmod`, `ascii`; execution engine STORE_SUBSCR; str `count`, `rsplit`; list `reverse`; dict `pop`; int `bit_length`; stubs: `typing` (Optional, Union), `hashlib`, `copy`, `itertools.chain`; foundation test for `filter`; STORE_SUBSCR coverage documented; Phase 3 opcode list updated (BINARY_SUBSCR, BUILD_MAP, STORE_SUBSCR); v10 completion documented.

**Completed: Next 20 Steps v11 (145–164).** Delivered: builtins `ord`, `chr`, `bin`, `oct`, `hex`; list `sort`; execution engine BUILD_TUPLE, GET_ITER, FOR_ITER; stubs: `argparse`, `getopt`, `tempfile`, `subprocess`; str `encode`, bytes `decode`; float `is_integer`; foundation test for `map`; execution engine tests BUILD_TUPLE and GET_ITER; v11 completion documented.

**Completed: Next 20 Steps v12 (165–184).** Step details in [tasks/todo.md](../tasks/todo.md). Builtins `breakpoint`, `globals`, `locals`; str `center`, `ljust`, `rjust`, `zfill`, `partition`, `rpartition`; list `copy`; execution engine UNPACK_SEQUENCE; itertools `repeat` (islice existing); bytes `hex`, `fromhex`; stdlib stubs `struct`, `base64`, `random`; tests and docs.

**Completed: Next 20 Steps v13 (185–204).** Step details in [tasks/todo.md](../tasks/todo.md). Builtin `input` stub; execution engine LOAD_GLOBAL, STORE_GLOBAL, BUILD_SLICE; dict `popitem`; str `expandtabs`; int `from_bytes`, `to_bytes`; float `as_integer_ratio`; itertools `cycle`, `takewhile`; stdlib stubs `time`, `datetime`, `warnings`; tests and docs.

**Completed: Next 20 Steps v14 (205–224).** Step details in [tasks/todo.md](../tasks/todo.md). Builtin `object()` callable; str `capitalize`, `title`, `swapcase`; list `index`, `count`; tuple `index`, `count`; execution engine ROT_TWO, DUP_TOP; itertools `dropwhile`, `tee`; stdlib stubs `traceback`, `io.StringIO`; tests and docs.

**Completed: Next 20 Steps v15 (225–244).** Step details in [tasks/todo.md](../tasks/todo.md). Builtins `eval`, `exec` stubs; str `casefold`, `isalpha`, `isdigit`, `isspace`, `isalnum`; bytes `find`, `count`; set `pop`; itertools `accumulate`, `groupby` stubs; math `floor`/`ceil` (float support); os.environ stub; foundation test isdigit.

**Completed: Next 20 Steps v16 (245–264).** Step details in [tasks/todo.md](../tasks/todo.md). str `isupper`, `islower`; bytes `index`, `rfind`; itertools `product`, `combinations` stubs; math `fabs`, `trunc`; foundation test set.pop.

**Completed: Next 20 Steps v17 (265–284).** Step details in [tasks/todo.md](../tasks/todo.md). str `lstrip(chars)`, `rstrip(chars)`; dict `fromkeys`; str `isdecimal`, `isnumeric`; bytes `startswith`, `endswith`; set `discard`; itertools `combinations_with_replacement`, `permutations` stubs; math `copysign`, `isclose`; collections.Counter, uuid, secrets stubs; foundation tests str.isupper, bytes.index; exec engine BinarySubtract test.

**Completed: Next 20 Steps v18 (285–304).** Step details in [tasks/todo.md](../tasks/todo.md). str `splitlines`, `rfind`, `rindex`; bytes `rindex`, `strip`, `lstrip`, `rstrip`; set `copy`, `clear`, `union`/`intersection`/`difference`; builtins `help`, `memoryview` stubs; math `isinf`, `isfinite`, `isnan`, `isclose`; os `getcwd`, `chdir`; threading, enum stubs; functools `reduce`; foundation tests splitlines, set.discard; exec engine BinaryTrueDivide test.

**Stub completion (post-v18).** See [STUBS.md](STUBS.md). set union/intersection/difference implemented; itertools stubs return empty iterator; math.isclose implemented; tempfile.mkstemp, subprocess.run/Popen/CompletedProcess enhanced; os.path, pathsep, sep added.

**Completed: Next 20 Steps v19 (305–324).** Step details in [tasks/todo.md](../tasks/todo.md). itertools.accumulate full implementation (default add + optional func); foundation tests set.union/copy, math.isclose, itertools.accumulate; docs and todo updated.

**Completed: Next 20 Steps v20 (325–344).** Step details in [tasks/todo.md](../tasks/todo.md). Execution engine BINARY_POWER, BINARY_FLOOR_DIVIDE; math log/log10/exp; operator pow/floordiv/mod; str.isidentifier, bytes.count, list.__mul__/__rmul__; builtins hasattr/delattr; stdlib stubs atexit, heapq, functools.lru_cache; json.dumps/loads (JsonModule); foundation tests MathLog, ListRepeat, HasattrDelattr; STUBS and todo updated.

**Completed: Next 20 Steps v21 (345–364).** Step details in [tasks/todo.md](../tasks/todo.md). UNARY_NEGATIVE, UNARY_NOT; math sqrt/sin/cos; operator neg/not_; str.isprintable; bytes.isdigit/isalpha; list.__reversed__/reversed(); dict.__or__/__ror__; getattr/slice repr; csv, xml.etree stubs; foundation tests MathSqrt, DictUnion, OperatorNegOrGetattr; STUBS and todo updated.

**Completed: Next 20 Steps v22 (365–384).** Step details in [tasks/todo.md](../tasks/todo.md). UNARY_INVERT, POP_TOP; math.tan; operator.invert; str/bytes.isascii; setattr, callable; list __iadd__, dict __ior__/__iror__; html, difflib stubs; foundation and engine tests; STUBS and todo updated.

**Completed: Next 20 Steps v23 (385–404).** Step details in [tasks/todo.md](../tasks/todo.md). UNARY_POSITIVE, NOP, INPLACE_ADD; math asin/acos/atan; operator lshift/rshift; bytes removeprefix/removesuffix; repr, id; int.bit_count; shutil, mimetypes stubs; foundation and engine tests; STUBS and todo updated.

**Completed: Next 20 Steps v24 (405–424).** Step details in [tasks/todo.md](../tasks/todo.md). BINARY_LSHIFT, BINARY_RSHIFT, INPLACE_SUBTRACT; math atan2/degrees/radians; operator and_/or_/xor; str.split maxsplit, len/bool, float.hex (existing); email, runpy stubs; foundation and engine tests; STUBS and todo updated.

**Completed: Next 20 Steps v25 (425–444).** Step details in [tasks/todo.md](../tasks/todo.md). BINARY_AND, BINARY_OR, BINARY_XOR, INPLACE_MULTIPLY; math hypot/fmod/log2/log1p; operator.index; urllib.parse, zoneinfo stubs; foundation and engine tests; STUBS and todo updated.

**Completed: Next 20 Steps v26 (445–464).** Step details in [tasks/todo.md](../tasks/todo.md). INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_MODULO, INPLACE_POWER; math.remainder, math.erf; operator.truediv/floordiv/mod (existing); plistlib, quopri stubs; foundation tests MathRemainder, MathErf; execution-engine tests InplaceTrueDivide, InplaceModulo; STUBS and todo updated.

**Completed: Next 20 Steps v27 (465–484).** Step details in [tasks/todo.md](../tasks/todo.md). INPLACE_LSHIFT, INPLACE_RSHIFT, INPLACE_AND, INPLACE_OR, INPLACE_XOR; math.erfc, math.gamma, math.lgamma; operator (existing); binascii, uu stubs; foundation tests MathErfc, MathGamma; execution-engine tests InplaceLshift, InplaceAnd; STUBS and todo updated.

**Completed: Next 20 Steps v28 (485–504).** Step details in [tasks/todo.md](../tasks/todo.md). ROT_THREE, ROT_FOUR; math.dist, math.perm, math.comb; operator (existing); configparser, getpass stubs; foundation tests MathDist, MathPerm, MathComb; execution-engine tests RotThree, RotFour; STUBS and todo updated.

**Completed: Next 20 Steps v29 (505–524).** Step details in [tasks/todo.md](../tasks/todo.md). DUP_TOP_TWO; math.factorial, math.prod; operator (existing); pickle, logging stubs; foundation tests MathFactorial, MathProd; execution-engine test DupTopTwo; STUBS and todo updated.

**Completed: Next 20 Steps v30 (525–544).** Step details in [tasks/todo.md](../tasks/todo.md). math.isqrt, math.acosh, math.asinh, math.atanh; multiprocessing, ssl stubs; foundation tests MathIsqrt, MathAcosh; STUBS and todo updated.

**Completed: Next 20 Steps v31 (545–564).** Step details in [tasks/todo.md](../tasks/todo.md). math.cosh, math.sinh, math.tanh; math.nan, math.inf; shelve, locale stubs; foundation tests MathCosh, MathSinh; STUBS and todo updated.

**Completed: Next 20 Steps v32 (565–584).** Step details in [tasks/todo.md](../tasks/todo.md). math.ulp, math.nextafter; ctypes, sqlite3 stubs; foundation tests MathUlp, MathNextafter; STUBS and todo updated.

**Completed: Next 20 Steps v33 (585–604).** Step details in [tasks/todo.md](../tasks/todo.md). math.ldexp, math.frexp, math.modf, constant math.e; pprint, dis stubs; foundation test MathLdexpFrexpModfE; STUBS and todo updated.

**Completed: Next 20 Steps v34 (605–624).** Step details in [tasks/todo.md](../tasks/todo.md). math.cbrt, math.exp2, math.expm1, math.fma; ast, tokenize stubs; foundation test MathCbrtExp2Expm1Fma; STUBS and todo updated.

**Phase 3 Execution Engine completed.** executeMinimalBytecode supports all listed opcodes (100–155); test_execution_engine covers them (see [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md)); protopy runs script `.py` files via builtins.exec into the module namespace and invokes module `main` when present.

**Step 5 (optional): Parser/compiler extended to emit full opcode set.** Tokenizer: Dot, LSquare, RSquare, LCurly, RCurly, Colon, Assign, keywords For, In, If, Else, Global. Parser: AST nodes for Attribute, Subscript, Slice, ListLiteral, DictLiteral, TupleLiteral, Assign, For, If, Global; grammar for statements and module. Compiler: LOAD_ATTR, STORE_ATTR, BINARY_SUBSCR, STORE_SUBSCR, BUILD_LIST, BUILD_MAP, BUILD_TUPLE, BUILD_SLICE, GET_ITER, FOR_ITER, JUMP_ABSOLUTE, POP_JUMP_IF_FALSE, LOAD_GLOBAL, STORE_GLOBAL; jump patching for control flow. FOR_ITER jump target in the engine uses bytecode list index. Foundation tests: CompiledSnippetListLiteralAndSubscr, CompiledSnippetExecAssign.

**Completed: Next 20 Steps v35 (625–644).** Step details in [tasks/todo.md](../tasks/todo.md). math.sumprod; inspect, types stubs; foundation test MathSumprod; STUBS, todo, TESTING updated.

**Completed: Next 20 Steps v36 (645–664).** Step details in [tasks/todo.md](../tasks/todo.md). Concurrent execution model (GIL_FREE_AUDIT); protoCore compatibility (INSTALLATION); STUBS v36 section; IMPLEMENTATION_PLAN, todo, TESTING updated.

**Completed: Next 20 Steps v37 (665–684).** Step details in [tasks/todo.md](../tasks/todo.md). atexit (register/unregister/_run_exitfuncs) wired to runtime shutdown; shutil.copyfile and copy; traceback extract_tb, format_list, format_exception; difflib.unified_diff; foundation tests AtexitRegister, ShutilCopyfile; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v38 (685–704).** Step details in [tasks/todo.md](../tasks/todo.md). _thread native module resolution fix; statistics.mean, median, stdev, variance, pvariance; urllib.parse.quote, unquote, urljoin; dataclasses default values; unittest assertRaises, assertIn, main; foundation tests ThreadModuleLoads, StatisticsModuleLoads, UrllibParseModuleLoads; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v39 (705–724).** Step details in [tasks/todo.md](../tasks/todo.md). _os.getenv; os.environ populated from environment; argparse ArgumentParser add_argument/parse_args; configparser read/sections/get; getpass.getpass/getuser; foundation tests OsEnviron, ArgparseBasic, ConfigParserRead; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v40 (725–744).** Step details in [tasks/todo.md](../tasks/todo.md). difflib.SequenceMatcher; ast.literal_eval; inspect isfunction/ismodule/getsourcefile; decimal.Decimal and fractions.Fraction basics; types.ModuleType; foundation tests DifflibSequenceMatcher, AstLiteralEval, DecimalBasic; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v41 (745–764).** Step details in [tasks/todo.md](../tasks/todo.md). struct.calcsize; _os.listdir/remove/rmdir; os.path.exists/isdir; shutil.move and rmtree; foundation tests StructCalcsize, PathlibPath, ShutilMove; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v42 (765–784).** Step details in [tasks/todo.md](../tasks/todo.md). tempfile.mkstemp unique path; tempfile.TemporaryFile file-like; io.TextIOWrapper; mimetypes; foundation tests TempfileMkstemp, IoTextIOWrapper, MimetypesGuess; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v43 (785–804).** Step details in [tasks/todo.md](../tasks/todo.md). pathlib.Path exists, is_dir, is_file, mkdir; decimal __mul__/__truediv__; fractions __mul__; io.BytesIO; foundation tests PathlibExistsIsdir, DecimalMul, IoBytesIO; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v44 (805–824).** Step details in [tasks/todo.md](../tasks/todo.md). pathlib.Path read_text/write_text; os.path.isfile; functools.partial; datetime date/timedelta; foundation tests PathlibReadText, FunctoolsPartial, DatetimeDate; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v45 (825–844).** Step details in [tasks/todo.md](../tasks/todo.md). pickle loads/dumps minimal; logging Handler/Formatter/StreamHandler; email message_from_string headers; foundation tests PickleLoadsDumps, LoggingHandler, EmailParse; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v46 (845–864).** Step details in [tasks/todo.md](../tasks/todo.md). secrets token_hex/token_urlsafe; subprocess.run args; regrtest persistence docs; C_MODULES_TO_REPLACE update; foundation tests SecretsTokenHex, SubprocessRun; STUBS, todo, IMPLEMENTATION_PLAN updated.

**Completed: Next 20 Steps v47 (865–884).** Step details in [tasks/todo.md](../tasks/todo.md). HPY_INTEGRATION_PLAN; PACKAGING_ROADMAP; venv stub; lessons.md (v43–v47); STUBS, todo, IMPLEMENTATION_PLAN updated. Summary of v43–v47: pathlib (exists, is_dir, is_file, mkdir, read_text, write_text), decimal/fractions/io.BytesIO, pickle/logging/email, secrets/subprocess, regrtest docs, HPy and packaging roadmaps, venv stub.

**Completed: Next 20 Steps v48 (885–904).** Step details in [tasks/todo.md](../tasks/todo.md). Foundation test fixes: SetBasic, MathLog (log(100,10) workaround), MathDist, SetattrAndCallable (direct setAttribute/getAttribute on mutable obj; protoCore immutable model documented), MapBuiltin, StringDunders. OperatorInvert DISABLED_; TESTING, STUBS, todo updated.

**Completed: Next 20 Steps v49 (905–924).** Step details in [tasks/todo.md](../tasks/todo.md). v48 doc completion; ThreadModule stack-use-after-scope fix; OperatorInvert DISABLED_; SetattrAndCallable with direct setAttribute/getAttribute; py_setattr, py_log10 deferred.

**Completed: Next 20 Steps v50 (925–944).** Step details in [tasks/todo.md](../tasks/todo.md). tasks/todo.md v49/v50; IMPLEMENTATION_PLAN, STUBS, TESTING updated; lessons captured; commit.

**Completed: Next 20 Steps v51 (945–964).** Step details in [tasks/todo.md](../tasks/todo.md). tasks/todo.md v51; IMPLEMENTATION_PLAN v50 done; TESTING, STUBS v51; protoCore immutable model; OperatorInvert, py_setattr/py_log10/ThreadModule status.

**Completed: Next 20 Steps v52 (965–984).** Step details in [tasks/todo.md](../tasks/todo.md). tasks/todo.md v52; IMPLEMENTATION_PLAN v51 done; TESTING, STUBS v52; lessons consolidated; commit.

**Completed: Next 20 Steps v53 (985–1004).** Step details in [tasks/todo.md](../tasks/todo.md). FilterBuiltin passes; ThreadModule fix documented; py_log10 workaround; regrtest persistence verified; foundation suite full vs filtered documented; STUBS, TESTING updated.

**Completed: Next 20 Steps v54 (1005–1024).** Step details in [tasks/todo.md](../tasks/todo.md). Regrtest dashboard; compatibility tracking; C_MODULES_TO_REPLACE (_operator Replaced, _functools Partial); TESTING, STUBS v54.

**Completed: Next 20 Steps v55 (1025–1044).** Step details in [tasks/todo.md](../tasks/todo.md). HPy Phase 1 design (handle table, HPyContext); packaging install and wheel layout; OperatorInvert root-cause hypothesis; TESTING, STUBS updated.

**Completed: Next 20 Steps v56 (1045–1064).** Step details in [tasks/todo.md](../tasks/todo.md). functools.lru_cache; operator audit; json/re stdlib coverage; py_setattr posArgs; STUBS v56.

**Completed: Next 20 Steps v57 (1065–1084).** Step details in [tasks/todo.md](../tasks/todo.md). DAP Phase 1 doc; venv and drop-in replacement (PACKAGING_ROADMAP); lessons v53–v57 consolidation.

**Completed: Next 20 Steps v58 (1085–1104).** Step details in [tasks/todo.md](../tasks/todo.md). Implementation Block 1100-1200 V2: strings as ProtoTuple only; inline string (up to 7 UTF-32 chars in tagged pointer); O(1) concat via tupleConcat; design doc [protoCore/docs/ROPES_AS_PROTOTUPLE.md](../../protoCore/docs/ROPES_AS_PROTOTUPLE.md).

**Completed: Next 20 Steps v59 (1105–1124).** Step details in [tasks/todo.md](../tasks/todo.md). Clustering in 64-byte tuple cells; getAt by tuple traverse (O(depth) then O(1) at leaf); getSize/iterator; String support, STUBS, and testing docs updated.

**Completed: Next 20 Steps v60 (1125–1144).** Step details in [tasks/todo.md](../tasks/todo.md). ProtoExternalBuffer (64-byte header, aligned_alloc segment); Shadow GC (finalize frees segment); protoCore docs, todo, IMPLEMENTATION_PLAN, STUBS, TESTING updated.

**Completed: Next 20 Steps v61 (1145–1164).** Step details in [tasks/todo.md](../tasks/todo.md). GetRawPointer API; stable-address contract; SwarmTests (ExternalBufferGC, GetRawPointerIfExternalBuffer pass).

**Completed: Next 20 Steps v62 (1165–1184).** Step details in [tasks/todo.md](../tasks/todo.md). Swarm hardening (disabled tests documented); lessons v58–v62; lock-free/C++20 notes; final doc updates (todo, STUBS, TESTING, STRING_SUPPORT). Block 1100-1200 V2 complete.

**Completed: Next 20 Steps v63 (1185–1204).** Step details in [tasks/todo.md](../tasks/todo.md). HPy Phase 1 foundation: HPyContext, handle table (ref-counted slots), core ABI (HPy_FromPyObject, HPy_AsPyObject, HPy_Dup, HPy_Close, HPy_GetAttr, HPy_SetAttr, HPy_Call, HPy_Type). Header `include/protoPython/HPyContext.h`, impl `src/library/HPyContext.cpp`, test `test_hpy_context`.

**Next 20 Steps v64 (1205–1224) — next.** Module load (.so/.hpy.so), HPyModuleDef, init; universal ABI start.

## 5. Debugging & IDE Support
To be a viable replacement, `protoPython` must support professional developer workflows.

- [x] **Tracing API**: Implement `sys.settrace` and related hooks within the execution engine.
- [ ] **Protocol Support**:
    - DAP skeleton added (`DAPServer.h`); `isSupported()` returns false (v57). Phase 1 scope: connect, disconnect, capabilities handshake; line stepping, variable inspection, breakpoints TBD.
- [ ] **IDE Integration**: Ensure compatibility with VS Code (via `debugpy`-like backend) and PyCharm.
- [ ] **Step-through C++ Boundaries**: Allow debugging to transition seamlessly from Python code into C++ module code.

## 6. Compiler & Deployment (`protopyc`)
Transitioning from interpreted to compiled execution.

- [ ] **AOT Compilation**: A head-of-time compiler that translates `.py` to `.so`/`.dll` via C++.
- [ ] **Static Analysis**: Leverage type hints for further optimization in the C++ layer.
- [ ] **Self-Hosting**: Reach a point where `protopyc` can compile itself.

## 7. Installation & Distribution
- [ ] **Packaging**: Support `pip` and `wheels`.
- [ ] **Virtual Environments**: Full `venv` support.
- [ ] **Drop-in replacement**: Ensure `protopy` can be aliased to `python` and work in complex setups.

## 7. Performance Benchmarks

### 7.1 Performance Test Suite

A dedicated performance test suite to measure protoPython execution speed and compare it against CPython 3.14.

- [x] **Benchmark harness**: [benchmarks/run_benchmarks.py](benchmarks/run_benchmarks.py) runs workloads on protopy and CPython. Use `PROTOPY_BIN`, `CPYTHON_BIN`, `--output`.
- [x] **Benchmark categories**: Startup (`startup_empty`), Arithmetic (`int_sum_loop`), `list_append_loop`, `str_concat_loop`, `range_iterate` (iteration over `range(N)`), **multithreaded CPU** (`multithread_cpu`: same total work, CPython with 4 threads vs protoPython single-thread; see [benchmarks/MULTITHREAD_CPU_BENCHMARK.md](../benchmarks/MULTITHREAD_CPU_BENCHMARK.md)).
- [x] **Reproducibility**: Warm-up runs before timing; median of 5 runs.
- [x] **Output**: Human-readable report (see §7.2) via `--output`; writes to `benchmarks/reports/`.

### 7.2 CPython Comparison Format

Results should be presented in a human-readable format, e.g.:

```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │        12.3  │         8.1  │ 1.52x slower │
│ list_append_loop       │        45.2  │        12.0  │ 3.77x slower │
│ int_sum_loop           │        28.1  │         6.2  │ 4.53x slower │
│ str_concat_loop        │        62.0  │        15.3  │ 4.05x slower │
│ range_iterate          │        18.4  │         4.1  │ 4.49x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~3.5x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```

- **Table format**: Benchmark name, protopy time, CPython time, ratio (protopy/cpython) with a short label (“X.XXx slower” or “X.XXx faster”).
- **Summary**: Geometric mean of ratios across benchmarks.
- **Notes**: Include environment (OS, CPU, build flags) and date in the report header.
- **Persistence**: Store reports under `benchmarks/reports/` (e.g. `YYYY-MM-DD_protopy_vs_cpython.md`).

---
*This plan is a living document and will be updated as we reach milestones.*
