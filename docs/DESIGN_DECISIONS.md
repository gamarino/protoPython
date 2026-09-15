# protoPython Design Decisions

This document consolidates the main architectural decisions for onboarding new collaborators. Each section is brief; see the linked documents for details.

---

## Minimal Bytecode Executor

**Decision:** protoPython is a minimal bytecode executor built inside protoPython, not a fork of CPython with a swapped eval loop.

**Rationale:** Keeps all runtime code in protoPython; no CPython build dependency. The executor is GIL-less by design from the start. Enables incremental delivery: run scripts via `PythonEnvironment`, resolve modules, execute bytecode through `ProtoContext`.

**Reference:** [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md)

---

## L-Shape Architecture

**Decision:** Single `ProtoSpace` per process; M:N thread mapping (each OS thread backed by a `ProtoThread`); explicit context stack with RAII; lock-free hot path.

**Rationale:** One memory topology simplifies ownership. O(1) context lookup via thread-local storage. Deterministic cleanup when contexts are destroyed. Work-stealing readiness: `ProtoSpace::threads` registry allows future scheduler integration without protoPython changes.

**Reference:** [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md)

---

## GIL-Free by Design

**Decision:** No `std::mutex` or `std::lock_guard` in the bytecode interpreter hot path. Context lookup uses thread-local storage. protoCore handles GC and allocation; its internal locks are outside the L-Shape mandate for protoPython.

**Rationale:** True parallel execution. Each thread runs independently. User-level locks (`_thread.allocate_lock`) and per-deque mutexes are Python API features, not a runtime GIL.

**Reference:** [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md), [POST_REFACTOR_AUDIT.md](POST_REFACTOR_AUDIT.md)

---

## Zero-Copy Object Promotion

**Decision:** Return values are promoted by pointer transfer. `ctx->returnValue` is set before context destruction; the `ProtoContext` destructor moves the object to the parent via `addCell2Context`. No memcpy.

**Rationale:** Eliminates copy overhead on function return. Objects allocated in a callee context are either promoted to the caller or reclaimed by GC when the context is destroyed.

**Reference:** [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md) §4

---

## Immutable-by-Default

**Decision:** Shared state uses Copy-on-Write. Python objects map directly to protoCore cells (e.g. `int` → `Integer`, `str` → `ProtoString`, `list` → `ProtoList`). No CPython API wrappers; zero-copy data flow as proto objects only.

**Rationale:** Thread safety by design. Structural sharing and CoW reduce contention. Aligns with protoCore's memory model.

**Reference:** [REARCHITECTURE_PROTOCORE.md](REARCHITECTURE_PROTOCORE.md)

---

## HPy for C Extensions

**Decision:** Use an [HPy](https://hpyproject.org/)-style handle API for native extension modules instead of the CPython C API. protoPython implements a C++ subset of that API (`include/protoPython/HPyContext.h`) and a loader, `HPyModuleProvider`, registered in module resolution (see [HPY_USER_GUIDE.md](HPY_USER_GUIDE.md)).

**Rationale:** CPython's C API exposes reference counting and assumes a GIL. HPy provides handles, type/attribute/call operations, and number/sequence protocols without those assumptions, so extensions can fit the GIL-less runtime.

**Reference:** [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md)

---

## Type Representations and Metadata

**Decision:** Standardize `__repr__` and `__str__` lookups in `PythonEnvironment`; include metadata like `co_name` in code objects for improved introspection. Ensure `type(None)` correctly returns `NoneType`.

**Rationale:** Improves REPL UX and diagnostic clarity. `co_name` allows generators and functions to report their actual names in tracebacks and `repr()`. Standardizing dunder lookups ensures consistency across builtin and user-defined types.

**Reference:** [INTERNALS_DEEP_DIVE.md](INTERNALS_DEEP_DIVE.md#5-metadata-and-prototyping)
