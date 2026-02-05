# Re-architecture of protoPython on protoCore

**Goal**: Refactor protoPython execution to be 100% native on protoCore, eliminate traditional synchronization (locks, mutexes, semaphores), and maximize multithreaded throughput.

**Audience**: Low-level / HPC systems engineer. This document is the architectural specification and gap analysis.

---

## 1. Mandatory Architecture Guidelines

### 1.1 Shared-Nothing Memory Model

- **No global non-atomic reference counts.** All Python objects must map to protoCore data structures. Mutable shared state must be managed via protoCore's ownership/borrowing (or equivalent lock-free discipline), not via locks.
- **Data alignment**: Use 64-byte padding on execution-node structures to avoid false sharing (e.g. in performance tests).
- **Mapping**: Python `int` → protoCore embedded integer or `Integer` cell; `str` → `ProtoString`; `list` → `ProtoList`; `dict` → `ProtoSparseList` (dict representation). Zero-copy: no CPython API wrappers; data flows as proto objects only.

### 1.2 Task Batching (Granularity)

- **Do not dispatch single bytecode instructions** to a scheduler.
- The **compiler** must analyse the AST and group code into **basic blocks** (maximal sequences with no external side effects). Each such block is compiled into a **single protoCore::Task** (or equivalent unit of work).
- **Target**: Task execution time ≥ 10× the cost of scheduler dispatch, so that dispatch overhead is amortized.

### 1.3 Immutability by Default

- **Global interpreter state** (type dicts, builtins, module registry) is treated as **Constant** in protoCore.
- Any modification (e.g. new module, new attribute on a type) uses **Copy-on-Write (CoW)** provided by protoCore's memory model, so other threads can keep reading the previous version without blocking.

### 1.4 Eliminate Heavy Abstraction

- Remove any wrapper that emulates the CPython API. protoPython talks to protoCore only.
- **Zero-copy**: No serialization or intermediate buffers between Python runtime and protoCore; all data is proto objects (cells, lists, strings, etc.).

---

## 2. Implementation Directions

### 2.1 Threading Strategy

- **Current state**: protoPython and protoCore use `std::mutex` (e.g. `PythonEnvironment::traceAndExceptionMutex_`, `resolveCacheMutex_`, `s_contextMapMutex`; protoCore `ProtoSpace::globalMutex`, `moduleRootsMutex`; `Thread.cpp` locks; `CollectionsModule` per-deque mutex). These cause contention under load.
- **Target**: Replace the current task queue (if any) and ad-hoc locking with **protoCore's native Work-Stealing Scheduler**. Each execution thread has its own **LocalHeap** (or equivalent) to avoid contention on the global allocator.
- **Note**: As of this writing, protoCore does not yet expose a Work-Stealing Scheduler, `Task` type, or per-thread LocalHeap. These must be added in protoCore first; protoPython will then be refactored to use them. See **Gap analysis** below.

### 2.2 Execution Engine

- **Current state**: `ExecutionEngine::executeMinimalBytecode` runs a single loop, dispatching one instruction at a time. No batching.
- **Target**:
  - **Bulk dispatch**: Execute a contiguous range of bytecode (one basic block) in one go, without yielding to a scheduler between instructions. This is implemented as `executeBytecodeRange` (or similar) and used when the compiler has grouped instructions into blocks.
  - **Type mapping**: Ints, strings, lists already use protoCore primitives; ensure no extra indirection or refcount layer. Document the direct mapping in code and in this doc.

### 2.3 Compiler

- **Current state**: Compiler emits a flat bytecode stream; no basic-block metadata.
- **Target**: Add an analysis pass that identifies basic blocks (entry points: start of code and every jump target; block end: jump or return). Emit block boundaries (e.g. `[start_0, end_0], [start_1, end_1], ...`) so the runtime can schedule one block per task.

---

## 3. Gap Analysis (protoCore vs Requirements)

| Requirement              | protoCore today                    | Gap / action |
|--------------------------|-------------------------------------|--------------|
| Work-Stealing Scheduler  | Not present                         | Add in protoCore: task queue, steal policy, worker threads (or integration with external executor). |
| Task type                | No `Task` or runnable unit          | Define `Task` (e.g. function + context + args) and integrate with scheduler. |
| Per-thread LocalHeap     | Single heap, `globalMutex` for GC   | Design per-thread allocation arenas and GC cooperation (e.g. thread-local free lists, global GC only for cross-thread refs). |
| CoW for global state     | Mutations use locks                 | Introduce immutable snapshots and CoW updates for module/type tables so readers never block. |
| No mutex in hot path     | `globalMutex`, `moduleRootsMutex`   | Replace with lock-free or per-thread structures; protoPython must avoid its own mutexes in resolve/trace paths. |
| 64-byte alignment        | Not enforced on execution state     | **Done in protoPython**: `ExecutionTask` and execution stack use `alignas(64)`; see ThreadingStrategy.h and ExecutionEngine.cpp. |

---

## 4. protoPython-Specific Contention Points to Remove

The following mutexes remain as technical debt until protoCore provides lock-free or per-thread primitives (Work-Stealing Scheduler, LocalHeap, CoW). The **ThreadingStrategy** and **ExecutionEngine** hot paths do not use mutexes.

1. **PythonEnvironment**
   - `traceAndExceptionMutex_`: protect trace hooks and pending exception with lock-free or thread-local state.
   - `resolveCacheMutex_`: replace resolve cache with a lock-free structure or per-thread cache.

2. **PythonEnvironment.cpp**
   - `s_contextMapMutex`: context registration map; use lock-free map or thread-local registration.

3. **CollectionsModule (deque)**
   - Per-deque `std::mutex`: replace with protoCore-backed concurrent structure or ownership rules so that only one thread mutates a given deque.

4. **ThreadModule**
   - Exposes `std::mutex` to Python; keep for compatibility but document that the target is protoCore-native synchronization.

5. **TestExecutionEngine**
   - `runMutex` used to serialize concurrent execution; once execution is task-based and each task runs on a dedicated context/heap, this can be removed.

No mutexes were added in the ThreadingStrategy or ExecutionEngine paths; existing mutexes above remain until protoCore provides replacements.

---

## 5. Output Deliverables (Code)

- **ThreadingStrategy** (`ThreadingStrategy.h` / `ThreadingStrategy.cpp`): Placeholder for work-stealing integration; no `std::mutex` or `std::atomic_flag` in the new strategy layer; document where protoCore scheduler will plug in.
- **ExecutionEngine**: New entry point that runs a **bytecode range** (basic block) in one shot; execution state (e.g. stack, pc) in a **64-byte-aligned** struct to avoid false sharing.
- **Compiler**: Basic-block boundaries are computed by `getBasicBlockBoundaries()` (see BasicBlockAnalysis). Optional future: emit block metadata in code objects for the runtime.

---

## 6. Implementation Status (protoPython)

- **ThreadingStrategy** (done): `include/protoPython/ThreadingStrategy.h`, `src/library/ThreadingStrategy.cpp`. No mutexes; `ExecutionTask` is 64-byte aligned; `runTaskInline` / `submitTask` call `executeBytecodeRange`. Ready to plug in protoCore Work-Stealing Scheduler when available.
- **ExecutionEngine** (done): `executeBytecodeRange(ctx, constants, bytecode, names, frame, pcStart, pcEnd)` executes one basic block without per-instruction dispatch; stack is `alignas(64) std::vector<...>`; `executeMinimalBytecode` delegates to full range. Type mapping remains direct protoCore (ints, strings, lists) with zero-copy.
- **Basic-block analysis** (done): `getBasicBlockBoundaries(ctx, bytecode)` in `BasicBlockAnalysis.h` / `BasicBlockAnalysis.cpp` computes block boundaries from the flat bytecode list (block starts: index 0 and every jump target; block ends: RETURN_VALUE, JUMP_ABSOLUTE, POP_JUMP_IF_FALSE, FOR_ITER). The compiler does not yet embed block metadata in code objects; the runtime can call `getBasicBlockBoundaries` when scheduling.
- **protoCore gaps**: Work-Stealing Scheduler, Task type, LocalHeap, CoW for global state—all remain in protoCore; protoPython is prepared to use them once exposed.

---

## 7. Testing

| Component            | Test executable                  | Coverage |
|---------------------|----------------------------------|----------|
| ExecutionEngine     | `test_execution_engine`          | `executeBytecodeRange` partial/full range, equivalence with `executeMinimalBytecode`; all opcodes. |
| ThreadingStrategy   | `test_threading_strategy`        | `ExecutionTask` 64-byte alignment; `runTaskInline` result; `submitTask` and null-safety. |
| BasicBlockAnalysis  | `test_basic_block_analysis`      | Empty/null inputs; single block; JUMP_ABSOLUTE / POP_JUMP_IF_FALSE block starts and ends. |

Run re-architecture tests: `ctest -R "test_execution_engine|test_threading_strategy|test_basic_block"` from the build directory.

Full suite (protoCore + protoPython): `ctest` from the build directory (71 tests; all must pass before commit).

---

## 8. References

- [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md) — Current concurrency and locking in protoPython.
- [MODULE_DISCOVERY.md](../../protoCore/docs/MODULE_DISCOVERY.md) — Module cache and thread safety.
- protoCore `ProtoSpace`, `ProtoThread`, `ProtoContext` — Current allocation and threading model.
