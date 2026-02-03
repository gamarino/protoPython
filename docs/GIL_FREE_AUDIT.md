# GIL-less Concurrency Audit

This document lists mutable operations in the protoPython library and their thread-safety posture. The goal is to ensure all mutable operations are either implemented with protoCore’s concurrency primitives or documented for follow-up. See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Section 1 Phase 4 and protoCore’s [DOCUMENTATION.md](../../protoCore/DOCUMENTATION.md) (e.g. mutability model, module discovery).

## Summary

- **protoCore foundation**: ProtoList and ProtoSparseList are immutable-by-default (e.g. `appendLast`, `setAt` return new structures). ProtoObject attribute storage may use internal mutable state; ProtoSpace and module resolution are owned by protoCore.
- **protoPython usage**: We create and mutate objects during environment setup and when executing Python operations (list append, dict setitem, sys.path, sys.modules). Concurrent access to the same `PythonEnvironment` / `ProtoContext` from multiple threads is not yet guaranteed safe; follow-up work is required.

## Mutable operations by component

### 1. PythonEnvironment (src/library/PythonEnvironment.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|-----------------|--------------------|-----------|
| List `append` | Yes: `ProtoList::appendLast` (returns new list) | Single-threaded use only | Ensure `setAttribute(__data__)` on list instance is safe under protoCore’s model when multiple threads touch the same object. |
| List `__setitem__` | Yes: `ProtoList::setAt` (returns new list) | Single-threaded use only | Same as above. |
| Dict `__setitem__` | Yes: `ProtoSparseList::setAt` (returns new sparse list) | Single-threaded use only | Same as above. |
| Root object setup | `setAttribute` on prototypes (object, type, list, dict, sys, builtins) | One-time init, single-threaded | No change. |
| sys.path update | `pathList->appendLast`, then `sysModule->setAttribute(path, ...)` | Init only | No change. |
| sys.modules update | `modulesDictObj->setAttribute("sys", ...)` etc. | Init only; runtime module registration may be shared | Document: if multiple threads register modules concurrently, protoCore’s module cache / resolution must be audited. |

### 2. SysModule (src/library/SysModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| settrace / gettrace | `PythonEnvironment::setTraceFunction` (pointer assignment) | Single-threaded | If trace is set from one thread and execution runs on another, add synchronization or document as single-threaded. |
| sys.platform, sys.version, sys.path, sys.modules | Stored via `setAttribute` on sys object | Init only; path/modules may be read by many threads | Reads of immutable attributes are fine; mutations to sys.path/sys.modules from Python or C++ need a concurrency policy. |

### 3. BuiltinsModule (src/library/BuiltinsModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| len, iter, next, range, etc. | ProtoList/ProtoSparseList/context APIs | Depends on object passed in | No extra mutable state; safety follows from object graph and ProtoContext. |
| range() | Builds list via `appendLast` | N/A | Immutable list construction. |

### 4. CollectionsModule (src/library/CollectionsModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| deque append/pop/appendleft/popleft | Internal `__deque_ptr__` (ProtoList or similar) | Single-threaded use only | Deque state is per-instance; concurrent access to same deque needs locking or immutable-style updates. |

### 5. IOModule (src/library/IOModule.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| open() | Creates file object, `setAttribute` for name/mode | Per-call; no shared mutable state | File handles are external; document OS handle thread-safety separately. |

### 6. PythonModuleProvider (src/library/PythonModuleProvider.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| Module load / setAttribute(__name__, __file__, __path__) | Module object creation and attributes | Depends on ProtoSpace/getImportModule | Align with protoCore’s SharedModuleCache and resolution chain; document whether concurrent imports are safe. |

### 7. NativeModuleProvider (src/library/NativeModuleProvider.cpp)

| Operation | Uses protoCore | Thread-safe today | Follow-up |
|-----------|----------------|-------------------|-----------|
| Module registration (at startup) | ProviderRegistry | Init only | No change. |

## Recommendations

1. **Single ProtoContext per thread (or explicit locking)**: Until proven otherwise, treat one `PythonEnvironment` (and its `ProtoContext`) as used by a single thread at a time, or introduce a clear locking strategy for shared access.
2. **ProtoSpace and module cache**: Confirm with protoCore’s docs (e.g. [MODULE_DISCOVERY.md](../../protoCore/docs/MODULE_DISCOVERY.md), mutability model) whether `getImportModule` and the resolution chain are thread-safe for concurrent reads and/or registration.
3. **Attribute mutation**: ProtoObject’s `setAttribute` mutates the object’s attribute storage. If multiple threads can mutate the same object, protoCore’s locking or copy-on-write semantics must be applied; otherwise restrict to single-threaded execution per context.
4. **Deque and shared collections**: For shared deque/list/dict instances, either use protoCore’s thread-safe patterns (if any) or document that they are single-threaded and add locking in a later phase.

## References

- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Phase 4 GIL-less Concurrency
- [DESIGN.md](../DESIGN.md) — Concurrency model (Section 6)
- protoCore [DOCUMENTATION.md](../../protoCore/DOCUMENTATION.md) — Index; see mutability and module discovery docs
