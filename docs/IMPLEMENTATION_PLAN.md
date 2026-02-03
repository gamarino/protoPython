# ProtoPython: Comprehensive Implementation Plan

This document outlines the roadmap for turning `protoPython` into a GIL-less, high-performance Python 3.14 replacement built on `protoCore`.

## 1. Core Runtime & Object Model (In Progress)
The foundation of the runtime must be fully compatible with Python 3.14's semantics while leveraging `protoCore`'s fine-grained locking and immutable-by-default sharing.

- [x] **Phase 1: Foundation**: Set up `PythonEnvironment`, `object`, `type`, `int`, `str`, `list`, `dict`.
- [ ] **Phase 2: Full Method Coverage** (Next 20 Steps v3: Steps 01–04 done): Implement all dunder methods for built-in types. First batch: `__getitem__`, `__setitem__`, `__len__` for list and dict [done]. Second batch: list `__iter__`/`__next__` [done], dict key iteration [done via `__keys__` list]. Third batch: list/dict `__contains__` [done]. Fourth batch: list/dict `__eq__` [done]. Fifth batch: list ordering dunders `__lt__`, `__le__`, `__gt__`, `__ge__` [done]; dict ordering returns `PROTO_NONE` (unsupported for now). Sixth batch: list/dict `__repr__` and `__str__` [done]. Seventh batch: list/dict `__bool__` [done]. Eighth batch: tuple `__len__` and builtins tuple registration [done]. Ninth batch: list slicing for `__getitem__` with slice spec `[start, stop, step]` (step=1 only) [done]. Tenth batch: tuple `__getitem__`, `__iter__`, `__contains__`, `__bool__` [done]. Eleventh batch: string `__iter__`, `__contains__`, `__bool__`, `upper()`, `lower()` [done]; see [STRING_SUPPORT.md](STRING_SUPPORT.md); raw string iteration has known issues.
- [ ] **Missing-key behavior**: dict `__getitem__` returns `PROTO_NONE` for missing keys until exceptions are implemented [documented].
- [ ] **Dict views**: `keys()`, `values()`, `items()` backed by `__keys__` and `__data__` [done].
- [ ] **List methods**: `pop()` and `extend()` [done].
- [ ] **Dict accessors**: `get()` and `setdefault()` [done].
- [ ] **Dict mutation helpers**: `update()`, `clear()`, `copy()` (shallow copy) [done].
- [ ] **protopy CLI**: distinct exit codes and flags (`--module`, `--script`, `--path`, `--stdlib`) with tests [done].
- [ ] **Phase 2b: Foundation test stability**: `test_foundation` hangs during list prototype creation (GC deadlock suspected). CTest timeout and troubleshooting docs added; see [TESTING.md](TESTING.md).
- [ ] **Phase 3: Execution Engine**: Finalize `ProtoContext` integration for Python bytecode execution (via `protopy`). protopy invokes module `main` when running a script (stub execution path).
- [ ] **Phase 4: GIL-less Concurrency**: Audit all mutable operations to ensure thread-safety using `protoCore` primitives. Audit documented in [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md); fixes TBD.

## 2. Standard Library Integration
The goal is to provide a complete standard library that behaves identically to CPython's.

- [x] **Phase 1: StdLib Shell**: Copy the Python 3.14 `.py` standard library into `lib/python3.14/`.
- [x] **Phase 2: C-to-C++ Replacement**:
    - [x] Initial `builtins` and `sys` native implementation.
    - [x] Basic `_io` module for file operations (Phase 4).
    - [x] Identify remaining modules traditionally implemented in C (e.g., `_collections`, `_functools`, `_ssl`, `_socket`); see [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md).
    - Ensure these modules are GIL-less from the start.
- [ ] **Phase 3: Native Optimization**: Progressively replace performance-critical Python modules with C++ implementations.

## 3. Compatibility & Testing
We aim for "No-Modification" compatibility with CPython tests.

- [x] **Integration of `test.regrtest`**: Set up a harness to run CPython's test suite directly.
- [ ] **Incremental Success**: Track compatibility percentage across the entire test suite (e.g. script or dashboard counting pass/fail; see `test/regression/`). Results can be persisted to JSON via `run_and_report.py --output <path>` or `REGRTEST_RESULTS=<path>`.
- [ ] **Bug-for-Bug Compatibility**: Where safe, emulate CPython edge cases to ensure existing code works without changes.

## 4. Debugging & IDE Support
To be a viable replacement, `protoPython` must support professional developer workflows.

- [x] **Tracing API**: Implement `sys.settrace` and related hooks within the execution engine.
- [ ] **Protocol Support**:
    - Implement the **Debug Adapter Protocol (DAP)** directly in the runtime (or as a module).
    - Enable line-by-line stepping, variable inspection, and breakpoint management.
- [ ] **IDE Integration**: Ensure compatibility with VS Code (via `debugpy`-like backend) and PyCharm.
- [ ] **Step-through C++ Boundaries**: Allow debugging to transition seamlessly from Python code into C++ module code.

## 5. Compiler & Deployment (`protopyc`)
Transitioning from interpreted to compiled execution.

- [ ] **AOT Compilation**: A head-of-time compiler that translates `.py` to `.so`/`.dll` via C++.
- [ ] **Static Analysis**: Leverage type hints for further optimization in the C++ layer.
- [ ] **Self-Hosting**: Reach a point where `protopyc` can compile itself.

## 6. Installation & Distribution
- [ ] **Packaging**: Support `pip` and `wheels`.
- [ ] **Virtual Environments**: Full `venv` support.
- [ ] **Drop-in replacement**: Ensure `protopy` can be aliased to `python` and work in complex setups.

---
*This plan is a living document and will be updated as we reach milestones.*
