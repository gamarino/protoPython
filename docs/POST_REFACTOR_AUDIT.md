# Technical Audit — protoPython

This document tracks correctness, concurrency safety, and implementation quality
across the protoPython lifetime. Sections are updated at each major release.

---

## Revision History

| Version | Date | Key Changes Audited |
|---------|------|---------------------|
| L-Shape (post-refactor) | 2025-Q4 | Mutex/atomic hot path, O(1) locals, slot pre-fill |
| V97–V100 | 2026 Q1 | Performance pass, super() fix, import-in-function fix |
| v1.0.0 | 2026-04-20 | Release audit: debug cleanup, stdlib cleanup, conformance |

---

## 1. Mutex / Atomic Status

### Hot Execution Path

- **ExecutionEngine bytecode loop**: No `std::mutex` or `std::atomic`.
  Context lookup uses `ctx` passed by caller; no global map or lock.
- **ContextScope**: RAII push/pop only; no synchronization in hot path.
- **TLS path**: When a `ProtoThread` is attached, `ProtoThread::getCurrentContext()`
  provides O(1) current context access.

### Non-Hot Path (Acceptable per L-Shape)

| Location | Primitive | Purpose | Classification |
|----------|-----------|---------|----------------|
| `PythonEnvironment.cpp` | `std::mutex g_internMutex` | String interning | Init-time only |
| `PythonEnvironment.cpp` | `std::atomic<int> s_pythonEnvInstanceCount` | Instance counting | Diagnostic |
| `PythonEnvironment.cpp` | `std::atomic<bool> s_sigintReceived` | SIGINT handler flag | Signal safety |
| `ThreadModule.cpp` | `std::mutex m` (Lock struct) | `_thread.allocate_lock()` | User-level API |
| `ThreadModule.cpp` | `std::atomic<bool> held`, `atomic<int> count` | Lock state | User-level API |
| `ThreadModule.cpp` | `std::atomic<int> s_bootstrapTidCount` | Bootstrap counter | Diagnostic |
| `CollectionsModule.cpp` | `std::mutex mutex` (DequeState) | Deque mutation | Per-instance, user-level |

**Conclusion**: Compliant with L-Shape architecture. No synchronization primitive in the
bytecode interpreter hot path. All mutex/atomic usage is either user-level API (Python
`_thread` semantics), diagnostic counters, or initialization-time string interning.

---

## 2. O(1) Local Variable Access

### AUTOMATIC Slots

- **LOAD_FAST / STORE_FAST**: `ctx->getAutomaticLocals()[arg]` — direct array index, O(1).
- **Compiler**: Assigns slot indices via `localSlotMap_`; `co_automatic_count` excludes
  MAPPED (closure-captured or dynamic-locals) variables.
- **Slot pre-fill**: `runUserFunctionCall` copies positional `args[i]` to `slots[i]` for
  `i < nparams`. Slot 0 = first parameter, slot 1 = second, etc. Matches `co_varnames` order.

### MAPPED Fallback

- **Escape analysis**: `hasDynamicLocalsAccess()` detects `locals()`, `exec()`, `eval()`.
- **Captured vars**: Closure-captured variables are excluded from automatic slots; use
  `OP_LOAD_NAME` / `OP_STORE_NAME` through the frame dict.
- **Fallback logging**: When `forceMapped` or captured, compiler emits (gated by
  `PROTO_ENV_DIAG`): `"DEBUG COMPILER: FunctionDef '<name>' forceMapped=… dynamicReason=…"`.

**Conclusion**: O(1) local access for AUTOMATIC vars. MAPPED fallback is logged and
not silent.

---

## 3. Debug / Diagnostic Output Audit — v1.0.0

### Gate Mechanism

All diagnostic output in protoPython uses one of three env-var gates:

| Gate | Env Var | Scope |
|------|---------|-------|
| `get_env_diag()` | `PROTO_ENV_DIAG` | General interpreter diagnostics |
| `std::getenv("PROTO_RESOLVE_DIAG")` | `PROTO_RESOLVE_DIAG` | Module resolution traces |
| `std::getenv("PROTO_ENV_DIAG")` | `PROTO_ENV_DIAG` | Direct env checks (main.cpp, etc.) |

### File-by-File Status

| File | Debug Prints | Status |
|------|-------------|--------|
| `src/library/BuiltinsModule.cpp` | 70+ | All gated by `get_env_diag()` ✓ |
| `src/library/ExecutionEngine.cpp` | 70+ | All gated by `get_env_diag()` ✓ |
| `src/library/Compiler.cpp` | 9 | All gated by `get_env_diag()` ✓ |
| `src/library/ExceptionsModule.cpp` | 3 | All gated by `get_env_diag()` ✓ |
| `src/library/PythonEnvironment.cpp` | ~30 | All gated ✓ |
| `src/library/CollectionsAbcModule.cpp` | 1 | Gated ✓ |
| `src/library/PythonModuleProvider.cpp` | 1 | Gated by `PROTO_RESOLVE_DIAG` ✓ |
| `src/runtime/main.cpp` | 1 | Gated by `PROTO_ENV_DIAG` ✓ |

### Stdlib Debug Cleanup (v1.0.0)

Unconditional stderr output removed from Python standard library files:

| File | Removed | Notes |
|------|---------|-------|
| `lib/python3.14/os.py` | 8 `print()` calls | Import preamble, posix section, load confirmation |
| `lib/python3.14/types.py` | 5 `print()` calls | DEBUG_TRACE prints |
| `lib/python3.14/collections/__init__.py` | 4 `sys.stderr.write()` calls | ENTER, MutableSequence, etc. |

### Native Module Debug Cleanup (v1.0.0)

| File | Removed | Notes |
|------|---------|-------|
| `src/library/WeakrefModule.cpp` | 10 `fprintf(stderr)` | Module initialization traces |

**Conclusion**: No unconditional debug output remains in production paths (C++ or Python).
All diagnostic traces are opt-in via `PROTO_ENV_DIAG` or `PROTO_RESOLVE_DIAG` environment
variables. Safe for production use.

---

## 4. Refcounting Audit

- **Result**: No `Py_INCREF`, `Py_DECREF`, `incref`, `decref`, `refcount`, or `ref_count`
  in any protoPython source file.
- **Memory model**: protoCore uses arena/concurrent-GC; protoPython does not perform
  manual memory management.

**Conclusion**: No manual refcounting in protoPython.

---

## 5. False Sharing

- **protoCore alignment**: Cells allocated with 64-byte alignment in `ProtoSpace::getFreeCells`
  to reduce cache-line false sharing.
- **protoPython layout**: Object fields are protoCore attribute keys (`__data__`, `__keys__`,
  etc.). Layout changes would require protoCore struct modifications.
- **Status**: No trivial protoPython-only mitigation available; documented as future work.

---

## 6. ExecutionEngine Architecture

- **ContextScope**: All user function calls use `ContextScope`; no bare `ProtoContext`
  without scope.
- **TLS path**: When `ProtoThread` is attached, `ProtoThread::getCurrentContext()` provides
  O(1) current context. Main thread uses `space->mainContext` when `thread == nullptr`.
- **Bootstrap**: Root context has `thread == nullptr`; `ContextScope` dtor safely skips
  `thread_->setCurrentContext` when `thread_` is null.
- **FunctionMetaCache** (V96): Per-function metadata (slot count, param count, captured vars)
  cached on first call; eliminates repeated attribute lookups in hot path.
- **Max stack depth** (V98): Compiler computes actual maximum stack depth; eliminates 34 KB/call
  static worst-case allocation.

---

## 7. Compiler — Escape Analysis

- **`hasDynamicLocalsAccess(ASTNode*)`**: Implemented; delegates to `getDynamicLocalsReason()`.
- **MAPPED classification**: When `locals()`, `exec()`, or `eval()` appear in the function body,
  all locals use MAPPED (empty `automaticNames`). Closure-captured vars are excluded from slots.
- **Opcodes**: MAPPED vars → `OP_LOAD_NAME` / `OP_STORE_NAME`; AUTOMATIC vars → `OP_LOAD_FAST` / `OP_STORE_FAST`.

---

## 8. Correctness Fixes — V100 / v1.0.0

### Zero-Argument super()

- **Root cause**: Python 3.x `super()` with no arguments requires the compiler to implicitly
  inject `__class__` (the enclosing class) and `self` (the first parameter). protoPython's
  compiler was not performing this injection.
- **Fix**: In `compileFunctionDef`, the compiler now propagates `currentClassName_` and
  rewrites bare `super()` calls at compile time to `super(ClassName, self)`.
- **Commit**: `3b0cc2d9`
- **Verification**: `class A: def f(self): return super().f()` — works correctly.

### import Inside Function Bodies

- **Root cause**: The `__import__` builtin was being resolved via `OP_LOAD_NAME` (frame-local
  lookup). Inside nested function bodies without an explicit `import` in the outer scope,
  `__import__` was not found in the local frame dict, causing `NameError`.
- **Fix**: Changed compilation of `__import__` to emit `OP_LOAD_GLOBAL` (frame-independent
  global lookup), matching CPython's semantics.
- **Commit**: `3b0cc2d9`
- **Verification**: Functions containing `import os` / `import json` — work correctly.

---

## 9. Conformance Status — v1.0.0

### Test Results (2026-04-20)

| Suite | Tests | Passed | Status |
|-------|-------|--------|--------|
| CPython `test_grammar.py` | 75 | 75 | ✓ PASS (V100) |
| Custom `test_decorator.py` | — | — | ✓ PASS |
| Custom `test_metaclass.py` | — | — | ✓ PASS |
| Custom `test_contextlib.py` | — | — | ✓ PASS |
| Custom `test_abc.py` | — | — | ✓ PASS |
| Custom `test_dataclasses.py` | — | — | ✓ PASS |
| CPython `test_types.py` | — | — | BLOCKED (`test.support`) |
| CPython `test_descr.py` | — | — | BLOCKED (`test.support`) |
| CPython `test_generators.py` | — | — | BLOCKED (`test.support`) |
| CPython `test_asyncgen.py` | — | — | BLOCKED (`asyncio` gaps) |
| CPython `test_json.py` | — | — | BLOCKED (`test.support`) |
| CPython `test_base64.py` | — | — | BLOCKED (`test.support`) |

### Blocker Analysis

The primary blocker for all Essential/Important CPython tests is `test.support`, a CPython
internal test infrastructure module that is not part of the standard library and requires
deep CPython internals (`sys.getsizeof`, `gc.collect`, `gc.get_objects`, resource limits,
signal handling, etc.). Implementing `test.support` is a separate project and is not
required for production use of protoPython.

The `test_asyncgen.py` blocker is separate: it requires a complete `asyncio` event loop
implementation including `asyncio.run()`, task scheduling, and async generator protocol.

**Honest assessment**: All custom conformance tests that exercise protoPython's own Python
feature set (decorators, metaclasses, ABC, dataclasses, context managers, full grammar)
pass. CPython's internal test infrastructure is a separate concern.

---

## 10. Standard Library

### Coverage (v1.0.0)

- **194 total entries** in `lib/python3.14/` (156 `.py` files + 38 directories)
- **Core modules available**: `abc`, `argparse`, `atexit`, `base64`, `bisect`, `calendar`,
  `codecs`, `collections`, `contextlib`, `dataclasses`, `datetime`, `enum`, `functools`,
  `gc`, `io`, `itertools`, `json`, `logging`, `os`, `pathlib`, `re`, `sys`, `threading`,
  `types`, `unittest`, `weakref`, and more
- **Async stack**: `asyncio/` subpackage present (partial)
- **Import machinery**: `importlib/` with `_bootstrap.py` (CPython original, adapted)

### Classification

- **Pure Python (CPython originals, adapted)**: majority of stdlib — `os.py`, `re.py`,
  `json/`, `logging/`, `pathlib.py`, `threading.py`, `types.py`, `collections/`, `functools.py`
- **Native C modules (protoPython implementations)**: `_ast`, `_collections_abc`,
  `_functools`, `_thread`, `errno`, `gc`, `posix`, `stat`, `_weakref`
- **Verified working**: `base64`, `contextlib`, `abc`, `dataclasses`, `json`, `re`
  (runtime-confirmed via custom test suite)

---

## 11. Performance History

| Version | Milestone | Impact |
|---------|-----------|--------|
| V94 | Benchmark baseline established | — |
| V95 | Removed `proto_internal.h` dependency | Code hygiene |
| V96 | FunctionMetaCache; removed redundant TLS writes | Reduced per-call overhead |
| V97 | Cached `get_env_diag()` (namespace-scope bool) | Eliminated per-call `getenv` |
| V98 | Actual max stack depth; eliminated 34 KB/call static allocation | Memory reduction |
| V99 | Eliminated per-call guard check in `diagEnabled()` | Hot path cleanup |
| V100 | Zero-arg super() fix; import-in-function fix | Correctness |
| v1.0.0 | Stdlib/native debug cleanup; version bump | Release quality |

---

## 12. Pending / Future Work

### 12.1 Main-Thread ProtoThread (Target: v1.2–1.3)

**Current**: Root context has `thread == nullptr`. Main thread uses `space->mainContext`
fallback. `ContextScope` dtor safely skips `thread_->setCurrentContext` when `thread_` is null.

**Target**: Full TLS O(1) for the main thread so `ProtoThread::getCurrentContext()` works
during bootstrap.

**Blocker**: protoCore must provide an API to attach the current OS thread to a `ProtoThread`
without spawning a new thread. `ProtoSpace::newThread` always spawns and cannot be used for
the main thread. Required API: `ProtoSpace::attachMainThread(ProtoContext* root)` or
`newMainThread(ProtoContext* root)`.

**Action**: protoCore upstream change required; protoPython would call the new API at
`PythonEnvironment` construction.

### 12.2 test.support Stub (Target: v1.1)

Implementing a minimal `test.support` module would unlock all 5 currently-blocked Essential
CPython conformance tests. The module needs: `run_unittest`, `check_warnings`,
`captured_stdout`, `captured_stderr`, `import_module`, `requires`. A stub returning no-ops
for resource and signal APIs would be sufficient to run the grammar/type/descriptor tests.

### 12.3 Deque Lock-Free Implementation (Target: v1.3+)

Current: per-instance `std::mutex` (Option B). Option A (lock-free copy-on-write backed by
protoCore immutable lists) would eliminate the remaining user-level mutex. Scoped as
follow-up after main-thread ProtoThread is resolved.

### 12.4 False Sharing (Target: protoCore v1.2+)

Hot field splitting in protoCore's ProtoObject/ProtoSparseList layout to prevent CPU
cache-line false sharing in multi-threaded execution. Requires protoCore struct changes.

---

*Audit maintained by: protoPython engineering team*
*Last updated: 2026-04-20 (v1.0.0 release)*
