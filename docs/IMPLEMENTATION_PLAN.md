# ProtoPython: Comprehensive Implementation Plan

This document outlines the roadmap for turning `protoPython` into a GIL-less, high-performance Python 3.14 replacement built on `protoCore`.

## 1. Core Runtime & Object Model (In Progress)
The foundation of the runtime must be fully compatible with Python 3.14's semantics while leveraging `protoCore`'s fine-grained locking and immutable-by-default sharing.

- [x] **Phase 1: Foundation**: Set up `PythonEnvironment`, `object`, `type`, `int`, `str`, `list`, `dict`.
- [x] **Phase 2: Full Method Coverage** (Next 20 Steps v4: Steps 01–24 done): Implement all dunder methods for built-in types. First batch: `__getitem__`, `__setitem__`, `__len__` for list and dict [done]. Second batch: list `__iter__`/`__next__` [done], dict key iteration [done via `__keys__` list]. Third batch: list/dict `__contains__` [done]. Fourth batch: list/dict `__eq__` [done]. Fifth batch: list ordering dunders `__lt__`, `__le__`, `__gt__`, `__ge__` [done]; dict ordering returns `PROTO_NONE` (unsupported for now). Sixth batch: list/dict `__repr__` and `__str__` [done]. Seventh batch: list/dict `__bool__` [done]. Eighth batch: tuple `__len__` and builtins tuple registration [done]. Ninth batch: list slicing for `__getitem__` with slice spec `[start, stop, step]` (step=1 only) [done]. Tenth batch: tuple `__getitem__`, `__iter__`, `__contains__`, `__bool__` [done]. Eleventh batch: string `__iter__`, `__contains__`, `__bool__`, `upper()`, `lower()`, `split()`, `join()` [done]; see [STRING_SUPPORT.md](STRING_SUPPORT.md); raw string iteration has known issues.
- [x] **Missing-key behavior**: dict `__getitem__` raises KeyError for missing keys; see [EXCEPTIONS.md](EXCEPTIONS.md).
- [x] **Dict views**: `keys()`, `values()`, `items()` backed by `__keys__` and `__data__`.
- [x] **List methods**: `pop()`, `extend()`, `insert()`, `remove()`, `clear()`. Note: `list.extend(iterable)` accepts only list-like objects (with `__data__`/list); arbitrary iterables (e.g. `range`, `map`) are not supported—use a loop with `append()`.
- [x] **Dict accessors**: `get()` and `setdefault()`.
- [x] **Dict mutation helpers**: `update()`, `clear()`, `copy()` (shallow copy).
- [x] **Set prototype**: `add`, `remove`, `__len__`, `__contains__`, `__bool__`, `__iter__`; see [SET_SUPPORT.md](SET_SUPPORT.md).
- [x] **Exception scaffolding**: `exceptions` module with `Exception`, `KeyError`, `ValueError`; see [EXCEPTIONS.md](EXCEPTIONS.md).
- [x] **protopy CLI**: distinct exit codes and flags (`--module`, `--script`, `--path`, `--stdlib`, `--bytecode-only`, `--trace`, `--repl`) with tests.
- [ ] **Phase 2b: Foundation test stability**: `test_foundation` hangs during list prototype creation (GC deadlock suspected). CTest timeout and troubleshooting docs added; see [TESTING.md](TESTING.md). v5 Step 25 documented GC reproducer; `test_foundation_filtered` runs stable subset.
- [ ] **Phase 3: Execution Engine**: `executeMinimalBytecode` supports LOAD_CONST, RETURN_VALUE, LOAD_NAME, STORE_NAME, BINARY_ADD/SUBTRACT/MULTIPLY/TRUE_DIVIDE, COMPARE_OP, POP_JUMP_IF_FALSE, JUMP_ABSOLUTE, CALL_FUNCTION, LOAD_ATTR, STORE_ATTR, BUILD_LIST, BINARY_SUBSCR, BUILD_MAP, STORE_SUBSCR, BUILD_TUPLE, GET_ITER, FOR_ITER, UNPACK_SEQUENCE, LOAD_GLOBAL, STORE_GLOBAL, BUILD_SLICE, ROT_TWO, DUP_TOP. protopy invokes module `main` when running a script. CTest `test_execution_engine` covers these opcodes.
- [ ] **Phase 4: GIL-less Concurrency**: Audit all mutable operations to ensure thread-safety using `protoCore` primitives. Audit documented in [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md); fixes TBD.

## 2. HPy Support for Imported Modules

[HPy](https://hpyproject.org/) is a portable API standard for writing Python C extension modules, designed to work across CPython, PyPy, GraalPy, and other implementations. It avoids exposing CPython implementation details (e.g. reference counting, GIL) and is better suited for GIL-less or alternative memory management.

- [ ] **Phase 1: HPy runtime shim**: Implement a protoPython-specific HPy context that maps HPy handles to `ProtoObject` instances. Provide the core HPy ABI (handles, `HPy_Close`, `HPy_Dup`, type/attr/call operations) so HPy-compiled `.so`/`.hpy.so` modules can be loaded.
- [ ] **Phase 2: HPy universal ABI**: Support loading HPy modules built against the universal ABI (single binary, no Python-version coupling). Resolve HPy entry points (`HPyModuleDef`, init function) and wire them into the protoPython import system.
- [ ] **Phase 3: HPy API coverage**: Implement the HPy API subset required by common HPy extensions (object creation, attribute access, calling, number/sequence protocols, exceptions). Prioritize modules relevant to protoPython's target workload.
- [ ] **Phase 4: Ecosystem compatibility**: Document how to build and distribute HPy extensions for protoPython. Ensure compatibility with `hpy` package and existing HPy tooling where feasible.

*References: [HPy Documentation](https://docs.hpyproject.org/), [HPy API](https://docs.hpyproject.org/en/latest/api.html).*

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

## 4. Compatibility & Testing
We aim for "No-Modification" compatibility with CPython tests.

- [x] **Integration of `test.regrtest`**: Set up a harness to run CPython's test suite directly.
- [ ] **Incremental Success**: Track compatibility percentage across the entire test suite (e.g. script or dashboard counting pass/fail; see `test/regression/`). Results can be persisted to JSON via `run_and_report.py --output <path>` or `REGRTEST_RESULTS=<path>`. See [TESTING.md](TESTING.md) for the JSON format and the `regrtest_persistence` CTest that exercises persistence.
- [ ] **Bug-for-Bug Compatibility**: Where safe, emulate CPython edge cases to ensure existing code works without changes.

**Completed: Next 20 Steps v8 (85–104).** Delivered: builtins `pow`, `round`, `zip`, `sorted`, `vars`; str methods (strip, replace, startswith, endswith); execution engine LOAD_ATTR, STORE_ATTR, BUILD_LIST; benchmarks (list_append_loop, str_concat_loop, range_iterate) and harness; native stubs for `os.path`, `pathlib`, `collections.abc`; CTest coverage for LOAD_ATTR/STORE_ATTR/BUILD_LIST; regrtest persistence (`--output`, `REGRTEST_RESULTS`, CTest `regrtest_persistence`).

**Completed: Next 20 Steps v9 (105–124).** Delivered: builtins `filter`, `map`, `next(iter, default)`; execution engine BINARY_SUBSCR, BUILD_MAP; list.extend limitation documented; stubs: `typing` (Any, List, Dict), `dataclasses`, `unittest`, `functools.wraps`, `contextlib`; `os.path.realpath`/`normpath`; str `find`/`index`; foundation test for `sorted`; v9 scope and completion documented.

**Completed: Next 20 Steps v10 (125–144).** Delivered: builtins `divmod`, `ascii`; execution engine STORE_SUBSCR; str `count`, `rsplit`; list `reverse`; dict `pop`; int `bit_length`; stubs: `typing` (Optional, Union), `hashlib`, `copy`, `itertools.chain`; foundation test for `filter`; STORE_SUBSCR coverage note in TESTING.md; Phase 3 opcode list updated (BINARY_SUBSCR, BUILD_MAP, STORE_SUBSCR); v10 completion documented.

**Completed: Next 20 Steps v11 (145–164).** Delivered: builtins `ord`, `chr`, `bin`, `oct`, `hex`; list `sort`; execution engine BUILD_TUPLE, GET_ITER, FOR_ITER; stubs: `argparse`, `getopt`, `tempfile`, `subprocess`; str `encode`, bytes `decode`; float `is_integer`; foundation test for `map`; execution engine tests BUILD_TUPLE and GET_ITER; v11 completion documented.

**Completed: Next 20 Steps v12 (165–184).** See [NEXT_20_STEPS_V12.md](NEXT_20_STEPS_V12.md). Builtins `breakpoint`, `globals`, `locals`; str `center`, `ljust`, `rjust`, `zfill`, `partition`, `rpartition`; list `copy`; execution engine UNPACK_SEQUENCE; itertools `repeat` (islice existing); bytes `hex`, `fromhex`; stdlib stubs `struct`, `base64`, `random`; tests and docs.

**Completed: Next 20 Steps v13 (185–204).** See [NEXT_20_STEPS_V13.md](NEXT_20_STEPS_V13.md). Builtin `input` stub; execution engine LOAD_GLOBAL, STORE_GLOBAL, BUILD_SLICE; dict `popitem`; str `expandtabs`; int `from_bytes`, `to_bytes`; float `as_integer_ratio`; itertools `cycle`, `takewhile`; stdlib stubs `time`, `datetime`, `warnings`; tests and docs.

**Completed: Next 20 Steps v14 (205–224).** See [NEXT_20_STEPS_V14.md](NEXT_20_STEPS_V14.md). Builtin `object()` callable; str `capitalize`, `title`, `swapcase`; list `index`, `count`; tuple `index`, `count`; execution engine ROT_TWO, DUP_TOP; itertools `dropwhile`, `tee`; stdlib stubs `traceback`, `io.StringIO`; tests and docs.

**Completed: Next 20 Steps v15 (225–244).** See [NEXT_20_STEPS_V15.md](NEXT_20_STEPS_V15.md). Builtins `eval`, `exec` stubs; str `casefold`, `isalpha`, `isdigit`, `isspace`, `isalnum`; bytes `find`, `count`; set `pop`; itertools `accumulate`, `groupby` stubs; math `floor`/`ceil` (float support); os.environ stub; foundation test isdigit.

**Completed: Next 20 Steps v16 (245–264).** See [NEXT_20_STEPS_V16.md](NEXT_20_STEPS_V16.md). str `isupper`, `islower`; bytes `index`, `rfind`; itertools `product`, `combinations` stubs; math `fabs`, `trunc`; foundation test set.pop.

## 5. Debugging & IDE Support
To be a viable replacement, `protoPython` must support professional developer workflows.

- [x] **Tracing API**: Implement `sys.settrace` and related hooks within the execution engine.
- [ ] **Protocol Support**:
    - DAP skeleton added (`DAPServer.h`); full DAP (line stepping, variable inspection, breakpoints) TBD.
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
- [x] **Benchmark categories**: Startup (`startup_empty`), Arithmetic (`int_sum_loop`), `list_append_loop`, `str_concat_loop`, `range_iterate` (iteration over `range(N)`).
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
