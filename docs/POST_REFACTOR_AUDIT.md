# Post-Refactor Audit: L-Shape Execution Engine

This document confirms the L-Shape refactoring outcomes and audit findings.

## 1. protoPython Mutex/Atomic Status

### Hot Path

- **ExecutionEngine**: No `std::mutex` or `std::atomic` in the bytecode interpreter loop. Context lookup uses `ctx` passed by caller; no global map or mutex.
- **ContextScope**: RAII push/pop only; no synchronization in hot path.

### Non-Hot Path (Acceptable per L-Shape)

- **CollectionsModule** (`DequeState`): Per-instance `std::mutex` protects deque operations. Documented as user-level locking in `L_SHAPE_ARCHITECTURE.md`. Not a runtime GIL.
- **ThreadModule**: `_thread.allocate_lock` uses `std::mutex` (Python API). Bootstrap diagnostic uses `std::atomic<int>` for lock-free counter (acceptable).
- **protoCore**: GC and allocation use internal locks; L-Shape mandate applies to protoPython, not protoCore internals.

**Conclusion**: No protoPython mutex/atomic in the hot execution path.

---

## 2. O(1) Local Access

### AUTOMATIC Slots

- **LOAD_FAST / STORE_FAST**: Use `ctx->getAutomaticLocals()[arg]`—direct array index, O(1).
- **Compiler**: Assigns slot indices via `localSlotMap_`; `co_automatic_count` excludes MAPPED vars.

### MAPPED Fallback

- **Escape analysis**: `hasDynamicLocalsAccess()` detects `locals()`, `exec()`, `eval()`.
- **Captured vars**: Excluded from slots; use `OP_LOAD_NAME` / `OP_STORE_NAME` (frame dict).
- **Slot fallback**: When `forceMapped` (dynamic locals) or captured, compiler emits structured log:
  `"[protoPython] Slot fallback: function=<name> reason=<locals|exec|eval|capture>"`.

**Conclusion**: O(1) local access for AUTOMATIC vars; MAPPED vars use frame dict with structured logging.

### 2.3 Slot Pre-fill (Arg Binding Order)

- **Compiler**: `automaticNames` = params first, then non-captured locals; `co_varnames` mirrors this order.
- **ExecutionEngine** (`runUserFunctionCall`): Copies positional args `args[i]` to `slots[i]` for `i < nparams`, so slot 0 = first param, slot 1 = second param, etc.
- **ProtoContext**: Binds keyword args to `closureLocals`; positional pre-fill into slots matches `co_varnames` order.
- **Verified**: Arg binding order matches `co_varnames`; params first, then local slots. No change required.

---

## 3. Slot Fallback Logging

- **Trigger**: `getDynamicLocalsReason()` returns non-empty when body contains `locals()`, `exec()`, or `eval()`.
- **Log format**: `"[protoPython] Slot fallback: function=<name> reason=<reason>"`.
- **Reasons**: `locals`, `exec`, `eval`, or `capture` (closure-captured vars).
- **Location**: `Compiler::compileFunctionDef` in `Compiler.cpp`.

**Conclusion**: Slot fallback is logged; not silent revert.

---

## 4. Refcounting Audit

- **Search**: No `Py_INCREF`, `Py_DECREF`, `incref`, `decref`, `refcount`, or `ref_count` in protoPython source.
- **protoCore**: Uses arena/GC; no refcounting in protoPython.

**Conclusion**: No manual refcounting in protoPython.

---

## 5. False Sharing Audit

- **Python object layout**: `__data__`, `__keys__` are attribute names on ProtoObject. Layout is defined in protoCore (ProtoObject, ProtoSparseList, etc.).
- **Recommendation**: For future multi-threaded hot-path optimization, consider splitting hot fields so frequently accessed members do not share a 64-byte cache line. Implementation would require protoCore struct changes—scope as follow-up.
- **Current**: protoCore allocates cells with 64-byte alignment in `ProtoSpace::getFreeCells` to reduce false sharing.

**Conclusion**: No trivial protoPython-only change for false sharing; documented as follow-up.

---

## 6. ExecutionEngine Refactor Summary

- **ContextScope**: All user function calls use `ContextScope`; no bare `ProtoContext` without scope.
- **TLS path**: When `ProtoThread` is attached, `ProtoThread::getCurrentContext()` provides O(1) current context. Main thread uses `space->mainContext` when thread is null.
- **Bootstrap**: Root context has `thread == nullptr`; `ContextScope` dtor safely skips `thread_->setCurrentContext` when `thread_` is null.

---

## 7. Compiler Escape Analysis Summary

- **`hasDynamicLocalsAccess(ASTNode*)`**: Implemented; delegates to `getDynamicLocalsReason()`.
- **MAPPED classification**: When `locals()`, `exec()`, or `eval()` in body, all locals use MAPPED (empty automaticNames). When closure-captured, those vars excluded from slots.
- **Opcodes**: MAPPED vars use `OP_LOAD_NAME` / `OP_STORE_NAME`; AUTOMATIC vars use `OP_LOAD_FAST` / `OP_STORE_FAST`.

---

## 8. Pending / Future Work

### Main-Thread ProtoThread (1.2–1.3 Follow-up)

- **Current**: Root context has `thread == nullptr`; `space->mainContext` fallback used; `ContextScope` dtor safely skips `thread_->setCurrentContext` when `thread_` is null.
- **Target**: Full TLS O(1) for main thread so `ProtoThread::getCurrentContext()` works on bootstrap.
- **Requirement**: protoCore must provide an API to attach the current OS thread to a `ProtoThread` without spawning (e.g. `ProtoSpace::attachMainThread(ProtoContext* root)` or `newMainThread(ProtoContext* root)`). `ProtoSpace::newThread` spawns a new OS thread and cannot be used for the main thread.
- **Scope**: protoCore change; protoPython would call the new API at `PythonEnvironment` construction.

### Other Follow-ups

- **Deque mutex**: Option B (per-instance lock) adopted; Option A (lock-free copy-on-write) documented as follow-up.
- **False sharing**: protoCore struct layout changes scoped as follow-up.
