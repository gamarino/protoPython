# L-Shape Architecture

This document describes the L-Shape architecture adopted by protoPython for integration with protoCore: a single memory topology, M:N thread mapping, an explicit context stack with RAII, and lock-free hot paths.

## 1. Memory Topology (ProtoSpace Singleton)

- **Invariant**: There is exactly one `ProtoSpace` per process. It owns all physical memory and the object heap.
- **Access**: `PythonEnvironment::getProcessSpace()` returns the process singleton. All `ProtoObject` allocation goes through contexts that use this Space.
- **Alignment**: protoCore allocates cells with 64-byte alignment (`posix_memalign(..., 64, ...)` in `ProtoSpace::getFreeCells`) to avoid false sharing when multiple threads allocate.

## 2. M:N Execution Model (ProtoThread)

- **Mapping**: Each OS thread that runs Python bytecode is backed by a `ProtoThread`. Python threads created via `_thread.start_new_thread` run on a `ProtoThread` created by protoCore.
- **Current context**: Exactly one `ProtoContext` is active per `ProtoThread` at any time. It is stored in the thread (e.g. `ProtoThreadImplementation::currentContext`) and exposed as `ProtoThread::getCurrentContext()`. Access is **O(1)** (thread-local read).
- **Autonomy**: Each thread executes independently; shared mutable state is avoided in hot paths. Visibility is handled by protoCore (e.g. immutable objects, Copy-on-Write when shared across threads).

## 3. Context Stack (The Stack Architecture)

- **Push on call**: Every Python function or method call pushes a new `ProtoContext` with `previous` set to the caller’s context. The new context becomes the current context for the thread.
- **Pop on return**: When the call returns, the callee context is destroyed (RAII). The destructor submits the young generation to the GC and promotes the return value to the parent (see below). The thread’s current context is restored to the parent.
- **RAII**: protoPython uses `ContextScope` (in `MemoryManager.hpp`) to manage the scope: construction pushes the context, destruction pops and destroys it. No `new(ctx) ProtoContext` without a matching destroy; the context is stack-allocated inside the scope.

## 4. Object Ownership and Promotion (Zero-Copy)

- **Ownership**: Objects allocated during a call belong to the active `ProtoContext` for that call.
- **Cleanup**: When the context is destroyed, all cells in its chain are submitted to the GC (young generation). Objects that were not promoted are reclaimed.
- **Promotion**: The return value is “promoted” by setting `ctx->returnValue` before the context is destroyed. The destructor of `ProtoContext` wraps it in a `ReturnReference` and adds it to the parent via `previous->addCell2Context`. **No data copy**—only pointer transfer. The promoted object's cell pointer is moved to the parent; no memcpy or value copy occurs.
- **API**: `promote(ctx, obj)` in `MemoryManager.hpp` sets `ctx->returnValue = obj`. The bytecode interpreter sets `ctx->returnValue` on `OP_RETURN_VALUE` so that when the scope exits, the destructor promotes it.

## 5. Lock-Free Mandate

- **Hot path**: No `std::mutex` or `std::lock_guard` in the hot execution path. Context lookup uses thread-local storage (`s_threadEnv`); current context is read from `ProtoThread::getCurrentContext()`.
- **Remaining synchronization**:
  - **User locks** (`_thread.allocate_lock`, `lock.acquire`/`release`): Part of the Python API for user-level synchronization; they are not a runtime GIL.
  - **collections.deque**: Per-instance mutex in `DequeState` protects mutable shared state; documented as user-level locking. A lock-free deque can be a follow-up.
  - **protoCore**: The GC and allocation path in protoCore may use internal locks (e.g. `globalMutex` in `ProtoSpace::getFreeCells`); the L-Shape mandate applies to protoPython, not to changing those internals.

## 6. O(1) Current Context and Deterministic Cleanup

- **Current context**: Obtained via `ProtoThread::getCurrentContext()`—a single read of thread-local state. No global map or mutex.
- **Deterministic cleanup**: When a function returns, the corresponding `ContextScope` is left and its destructor runs. The callee `ProtoContext` is destroyed immediately, so cleanup is deterministic and does not depend on a separate GC pass to reclaim the context object.

## 7. Work-Stealing Readiness

- **ProtoThread registry**: `ProtoSpace::threads` holds a mapping of `ProtoThread` objects. Each Python thread created via `_thread.start_new_thread` uses `ProtoSpace::newThread()`, which allocates a `ProtoThreadImplementation` and registers it in `ProtoSpace::threads`.
- **protoPython role**: `ThreadModule::py_start_new_thread` invokes `ctx->space->newThread(ctx, name, thread_bootstrap, argsForThread, nullptr)`. protoCore handles registration; protoPython does not manage threads directly.
- **Future integration**: A work-stealing scheduler can enumerate `ProtoSpace::threads` to discover idle or runnable threads. No protoPython changes are required for that integration.

## 8. Key Files

- **MemoryManager.hpp**: `promote()`, `ContextScope` (RAII push/pop).
- **ExecutionEngine.cpp**: User function calls use `ContextScope` and `promote()`; `OP_RETURN_VALUE` sets `ctx->returnValue`.
- **PythonEnvironment**: Uses `getProcessSpace()` (singleton); context→env resolution via thread-local `s_threadEnv` (no mutex).
- **ThreadModule**: Bootstrap diagnostic uses a lock-free counter instead of a mutex-protected set; `start_new_thread` delegates to `ProtoSpace::newThread`.
