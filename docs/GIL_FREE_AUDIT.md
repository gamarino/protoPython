# GIL-less Concurrency Audit

**Phase 4 completion:** All mutable operations audited; trace function, pending exception, context map, and resolve cache made thread-safe; sys.path/sys.modules and deque concurrency policy documented; concurrent execution test added (see TESTING.md). Allocation and GC remain tied to ProtoSpace/ProtoContext/ProtoThread; the test serializes compile+run to avoid concurrent allocation from a single context.

This document lists mutable operations in the protoPython library and their thread-safety posture. The goal is to ensure all mutable operations are either implemented with protoCore’s concurrency primitives or documented for follow-up. See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Section 1 Phase 4 and protoCore’s [DOCUMENTATION.md](../../protoCore/DOCUMENTATION.md) (e.g. mutability model, module discovery).

## Concurrency policy (Phase 4)

- **Attribute mutations:** We rely on protoCore's lock-free `setAttribute` (CAS on `mutableRoot`). Multiple threads may mutate different objects or the same object; CAS ensures no lost updates. List/dict mutations that update object attributes (e.g. `setAttribute(__data__, newList)`) are therefore safe under this model.
- **Execution model:** A single `PythonEnvironment` and single `ProtoContext` may be used from multiple threads. All shared mutable state in protoPython (trace function, pending exception, context registration map, resolve cache) is made thread-safe in Phase 4 (see follow-up resolutions below).
- **Module resolution:** We rely on protoCore's `getImportModule` (SharedModuleCache with `std::shared_mutex`, moduleRoots under `moduleRootsMutex`) as the source of thread-safety for concurrent imports. **ProtoSpace and module cache:** Resolved per protoCore [MODULE_DISCOVERY.md](../../protoCore/docs/MODULE_DISCOVERY.md).

## Concurrent execution model

- **Shared env/context and thread-safe layers:** Using a single `PythonEnvironment` and single `ProtoContext` from multiple OS threads is safe for protoPython’s thread-safe layers: trace function, resolve cache, and context registration map. Attribute mutations go through protoCore CAS.
- **Allocation and GC:** Object allocation and garbage collection in protoCore are tied to ProtoSpace, ProtoContext, and ProtoThread (per-thread). Using one ProtoContext from multiple OS threads without per-thread ProtoThread registration would risk allocator/GC corruption. True concurrent execution from multiple threads therefore requires either per-thread ProtoThread/context registration (when supported by protoCore) or serialized execution (only one thread allocating at a time).
- **Current test:** The Phase 4 concurrent test (ConcurrentExecutionTwoThreads) launches two threads that share one env/context but **serializes** the compile+run sections (mutex around allocation and execution). This validates the thread-safe layers from multiple threads without violating the allocation model.

## Summary

- **protoCore foundation**: ProtoList and ProtoSparseList are immutable-by-default (e.g. `appendLast`, `setAt` return new structures). ProtoObject attribute storage uses lock-free CAS on `mutableRoot`; ProtoSpace and module resolution are thread-safe per protoCore (SharedModuleCache, moduleRoots).
- **protoPython usage**: We create and mutate objects during environment setup and when executing Python operations (list append, dict setitem, sys.path, sys.modules). Phase 4 makes shared mutable state in protoPython (trace, pending exception, context map, resolve cache) thread-safe; attribute mutations go through protoCore CAS.

## Mutable operations by component

### 1. PythonEnvironment (src/library/PythonEnvironment.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|-----------------|--------------------|-----------|
| Trace function / pending exception | — | **Lock-free:** thread-local storage; no mutex in get/set/take | Resolved (remaining work). |
| Resolve cache | — | **Lock-free:** per-thread cache + atomic generation for invalidation; type shortcuts use current env | Resolved (remaining work). |
| List `append` | Yes: `ProtoList::appendLast` (returns new list) | Safe: protoCore CAS on `setAttribute(__data__, ...)` | Resolved per concurrency policy. |
| List `__setitem__` | Yes: `ProtoList::setAt` (returns new list) | Safe: same | Resolved per concurrency policy. |
| Dict `__setitem__` | Yes: `ProtoSparseList::setAt` (returns new sparse list) | Safe: same | Resolved per concurrency policy. |
| Root object setup | `setAttribute` on prototypes (object, type, list, dict, sys, builtins) | One-time init, single-threaded | No change. |
| sys.path update | `pathList->appendLast`, then `sysModule->setAttribute(path, ...)` | Init only | No change. |
| sys.modules update | `modulesDictObj->setAttribute(...)` etc. | Safe: protoCore CAS; concurrent registration via getImportModule is thread-safe | Resolved per protoCore SharedModuleCache. |

### 2. SysModule (src/library/SysModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| settrace / gettrace | `PythonEnvironment::setTraceFunction` (thread-local) | No lock; per-thread | Resolved. |
| sys.platform, sys.version, sys.path, sys.modules | Stored via `setAttribute` on sys object | Safe for concurrent read after init | **Policy:** sys.path and sys.modules are safe for concurrent read after initialization. Writes go through protoCore `setAttribute` (CAS). C++ code should not mutate sys.path/sys.modules from multiple threads without coordination; Python-level mutations use the same CAS path. |

### 3. BuiltinsModule (src/library/BuiltinsModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| len, iter, next, range, etc. | ProtoList/ProtoSparseList/context APIs | Depends on object passed in | No extra mutable state; safety follows from object graph and ProtoContext. |
| range() | Builds list via `appendLast` | N/A | Immutable list construction. |

### 4. CollectionsModule (src/library/CollectionsModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| deque append/pop/appendleft/popleft | Internal `__deque_ptr__` (ProtoList or similar) | Not thread-safe for concurrent mutation | **Resolved:** Deque is not thread-safe for concurrent mutation of the same instance. Python code that shares a deque across threads must synchronize externally (e.g. threading.Lock) or use a single-threaded consumer. |

### 5. IOModule (src/library/IOModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| open() | Creates file object, `setAttribute` for name/mode | Per-call; no shared mutable state | File handles are external; document OS handle thread-safety separately. |

### 6. PythonModuleProvider (src/library/PythonModuleProvider.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| Module load / setAttribute(__name__, __file__, __path__) | Module object creation and attributes | Depends on ProtoSpace/getImportModule | Align with protoCore's SharedModuleCache and resolution chain; document whether concurrent imports are safe. |

### 7. NativeModuleProvider (src/library/NativeModuleProvider.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| Module registration (at startup) | ProviderRegistry | Init only | No change. |

## Recommendations

1. **Single PythonEnvironment, shared ProtoContext**: Phase 4 allows multiple threads to use the same `PythonEnvironment` and `ProtoContext`; shared mutable state in protoPython is made thread-safe (trace, pending exception, context map, resolve cache).
2. **ProtoSpace and module cache**: Resolved. protoCore's `getImportModule` and SharedModuleCache are thread-safe per [MODULE_DISCOVERY.md](../../protoCore/docs/MODULE_DISCOVERY.md).
3. **Attribute mutation**: Resolved. protoCore's `setAttribute` uses lock-free CAS on `mutableRoot`; multiple threads may mutate the same or different objects.
4. **Deque and shared collections**: Documented as not thread-safe for concurrent mutation of the same instance; see CollectionsModule section and Step 5 resolution.

## References

- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Phase 4 GIL-less Concurrency
- [DESIGN.md](../DESIGN.md) — Concurrency model (Section 6)
- protoCore [DOCUMENTATION.md](../../protoCore/DOCUMENTATION.md) — Index; see mutability and module discovery docs
