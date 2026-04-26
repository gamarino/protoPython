# protoPython: GIL-Free Python 3.14 Runtime

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://isocpp.org/)
[![Build System](https://img.shields.io/badge/Build-CMake-green.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/Status-Phase%207%20Complete-green.svg)]()
[![Conformance](https://img.shields.io/badge/CPython%20Conformance-17%2F17%20(100%25)-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

> **"The GIL is no longer a limit. Immutability is no longer a constraint. Welcome to the era of the Swarm of One."**

**protoPython** is a Python 3.14 compatible environment built from the ground up on top of [**protoCore**](https://github.com/numaes/protoCore). It delivers a parallel Python runtime that eliminates the Global Interpreter Lock (GIL) and leverages immutable data structures for thread safety. The current focus is correctness: all 17 CPython conformance test categories pass. Interpreter throughput optimization is the active next phase.

> [!IMPORTANT]
> **protoPython**, **protopy**, and **protopyc** are now **Ready for community review**. We invite the community to audit the architecture, test edge cases, and provide performance feedback. The compiler now supports full C++ translation with incremental collection building and runtime support.

---

## 🎯 Key Features

- **True Parallel Concurrency (GIL-Free)**: Each Python thread is a native OS thread. Based on protoCore's parallel architecture, protoPython executes Python code across multiple cores without the bottlenecks of a Global Interpreter Lock.
- **Immutable Core Integrity**: Leverages protoCore's immutable-by-default memory model. Objects like tuples, strings, and collections utilize structural sharing, making them thread-safe by design and eliminating a whole class of concurrency bugs.
- **Zero-Copy Interop (UMD & HPy)**: The **Unified Memory Bridge (UMD)** and full **HPy** support enable passing massive data buffers between Python and C++ libraries without a single pointer move or data copy.
- **Hardware-Aware Design**: Optimized for modern CPU architectures with 64-byte cell alignment, eliminating false sharing and maximizing cache locality.
- **Python 3.14 Compatibility**: Targets the latest Python feature set, including advanced syntax and built-in type behaviors.

---

## 📋 Project Status: Phase 7 Support ✅

**Current Status:** Ready for community review (protoPython, protopy). Work in Progress (protopyc).

| Metric | Status |
|--------|--------|
| **Core Runtime** | **Complete** - GIL-free execution engine ✅ |
| **Type System** | **Advanced** - Lists, Tuples, Sets, Dicts with native wrapping ✅ |
| **C++ Interop** | **Full** - HPy and UMD support integrated ✅ |
| **Compiler** | **Advanced** - Full C++ translation with collection support ✅ |
| **Performance** | **Optimization in Progress** - V154 active: 256-shard mutable cache with negative caching, frame-skip restored for CO_OPTIMIZED leaf calls, attribute-name lookup avoids `toUTF8String` allocations, real `_thread.start_joinable_thread` + cooperative GC safepoint in the bytecode dispatcher (Tier 3 round 1). Geomean **5.53×** vs CPython 3.14 (Release build, minimum of 10) — best protoPython has hit. ⚙️ |
| **CPython Conformance** | **100%** - 17/17 test categories passing (Essential, Important, Necessary) ✅ |

- ✅ **Generator Delegation**: Full support for `yield` and `yield from` with efficient state persistence.
- ✅ **Smart Collection Unwrapping**: Seamless bridge between Python objects and native C++ collection methods.
- ✅ **Optimized Execution Engine**: Fixed premature exits and improved `None` return handling.
- ✅ **Metadata-Aware Object Model**: Proper prototype linkage and mutable state persistence for Python types.
- ✅ **Enhanced Debugging**: Integrated diagnostic systems for deep runtime analysis.
- ✅ **Balanced Collection Core**: `ProtoList` uses AVL balancing for $O(\log N)$ stability.
- ✅ **Optimized String Ropes**: High-performance string concatenation using native `protoCore` ropes.
- ✅ **Core Language Completeness**: Full support for Slicing, `del` statement, and `assert` functionality integrated.
- ✅ **Variable Arguments Support**: Full implementation of `*args` and `**kwargs` with support for both function definitions and calls, including correct dictionary key string representation.

---

## 📊 Performance Benchmarks (V154)

End-to-end script timings on the same machine, same kernel, governor,
Release build (`-O3 -DNDEBUG`).  Methodology: 2 warmup runs +
**minimum of 10 timed runs** per side per benchmark, which resists
scheduler / contention spikes.  CPython reference is `python3` (CPython 3.14).

```
┌────────────────────────┬──────────────┬──────────────┬─────────────────┬─────────────────────┐
│ Benchmark              │ protoPy (ms) │ CPython (ms) │ Ratio           │ Note                │
├────────────────────────┼──────────────┼──────────────┼─────────────────┼─────────────────────┤
│ startup_empty          │      32.0    │      32.0    │ 1.00× same      │ floor               │
│ int_sum_loop           │      32.1    │      32.3    │ 1.00× same      │ pure SmallInt       │
│ list_append_loop       │    1067.0    │      64.2    │ 16.62× slower   │                     │
│ str_concat_loop        │     715.8    │      64.2    │ 11.14× slower   │                     │
│ range_iterate          │     615.4    │      64.3    │  9.57× slower   │                     │
│ multithreaded_cpu      │     164.5    │      64.4    │  2.55× slower   │ real 4-thread bench │
│ attr_lookup            │     916.7    │      64.4    │ 14.23× slower   │                     │
│ call_recursion         │     866.2    │      64.4    │ 13.46× slower   │ fib(25) 242k        │
├────────────────────────┼──────────────┼──────────────┼─────────────────┼─────────────────────┤
│ Geomean Time Ratio     │              │              │  5.53×          │                     │
└────────────────────────┴──────────────┴──────────────┴─────────────────┴─────────────────────┘
```

Detail and methodology:
[`benchmarks/reports/2026-04-25-mutable-cache.md`](benchmarks/reports/2026-04-25-mutable-cache.md).

### What landed in V154

- **256-shard `mutableRoot` with cache-line padding** in protoCore.
  Selection by `mutable_ref % 256`, each slot padded to 64 bytes inside
  an `alignas(64)` array so concurrent CASes on different shards don't
  thrash a shared line.
- **Per-thread mutable-value cache (`MutableValueCacheEntry[1024]`)**
  in `ProtoThreadExtension`.  Validation by shard-root pointer
  equality, so any successful CAS by any thread invalidates stale
  entries on the next lookup with no broadcast.  Stores **negative
  results** too: an unmutated mutable now pays AVL only on the first
  read; subsequent reads on the same thread return immediately.
- **`skipFrame` restored for CO_OPTIMIZED leaf calls** in both
  `runUserFunctionCall` and the `runUserFunctionCallRaw` fast path.
  `b35bf811` had unconditionally built a frame on every call, which
  drove `call_recursion` to 35 s.  After the fix it is **0.97 s**.
- **`PythonEnvironment::getAttribute` no longer allocates `std::string`
  in the keys-fallback path.**  Replaced
  `keyS->toUTF8String(...) == name->toUTF8String(...)` with the
  zero-allocation `keyS->cmp_to_string(ctx, name)`.  Five hot sites
  fixed; ~5-15 % wall-time reduction across attribute-bound
  benchmarks.
- **Tier 3 — multi-threading actually works now.**  `_thread.start_joinable_thread`
  was a stub that returned 0 without spawning anything, making
  `threading.Thread.start()` hang indefinitely; the legacy
  `multithread_cpu` benchmark masked this by silently falling back to
  sequential execution.  Replaced with a real implementation backed by
  protoCore's `ProtoSpace::newThread`, plus a `_ThreadHandle` Python-visible
  object exposing `is_done` / `join` / `_set_done`.  Two GC-handshake
  bugs that surfaced once threads ran for real:
  - Thread-startup race in `ProtoThreadImplementation`: `runningThreads`
    was incremented *before* the OS thread started, leaving GC waiting
    for a phantom thread to park.  Now incremented inside `thread_main`
    when the OS thread actually executes.
  - Pure-bytecode loops never participated in stop-the-world.  Added a
    public `ProtoContext::safepoint()` to protoCore (additive API, no
    inlining, no library merge) and call it every 64 opcodes from
    protoPython's bytecode dispatcher.  CPU-bound threads now park
    cooperatively when GC requests STW.

  The `multithread_cpu` benchmark was rewritten to use `_thread`
  directly with a strict assertion that all 4 workers actually publish
  their result (any future regression to a fake-threading fallback
  fails the benchmark instead of silently passing).  Result:
  `multithread_cpu` 1217 ms → **165 ms** (ratio 18.90× → 2.55×) and
  geomean 7.30× → **5.53×** (best ever).

### Where the remaining gap is

Latest DWARF profile of `list_append_loop` (Release, post-V154):

```
 5.4 %  proto::ProtoString::toUTF8String           ← name path elsewhere
 3.6 %  proto::RopeCharacterIterator::next
 3.3 %  proto::StringLeafNode::fromObject
 2.4 %  proto::ProtoSpace::getFreeCells             (was 46 % pre-fix)
 2.4 %  proto::ProtoObject::getAttribute
 2.3 %  proto::ProtoObject::isString
 2.0 %  __memmove_avx_unaligned_erms
 1.9 %  proto::ProtoSparseListImplementation::implGetAt
```

Allocator pressure dropped from 46 % to 2.4 %.  Remaining
`toUTF8String` calls are in diagnostic paths and exception-name
checks, not the hot per-instruction loop.

### Roadmap to approach and surpass CPython

In priority order:

| Tier | Item | Status | Expected impact |
|---|---|---|---|
| **1** | Eliminate `toUTF8String` in `getAttribute` keys fallback | ✅ landed | -5 to -15 % |
| **1** | Lazy frame materialisation for `sys._getframe()` consumers | ⚙ deferred (design) | low single-digit |
| **2** | Inline SmallInteger arithmetic on the bytecode hot path | planned | 20-40 % on integer loops |
| **2** | Lock-free per-context cell pool replenish | planned | 1-2 % single-thread, larger as cores grow |
| **3** | Real `_thread.start_joinable_thread` + `_ThreadHandle` + STW safepoint poll | ✅ landed | multithread_cpu 18.9× → 2.55× |
| **3** | Beat CPython on `multithread_cpu` (parallel scaling) | planned | currently 2.55× slower; goal <1.0× |
| **3** | Verify GC scaling on N=2,4,8 threads | planned | confirms #3 |
| **4** | JIT compile hot bytecode regions via the `co_bytecode_native` path | research | crosses 1.0× threshold |

`multithread_cpu` came back from being silently fake (sequential
fallback that masqueraded as 39 ms parallel) to being a real
4-thread benchmark.  The first round of Tier-3 work made it run
correctly and reliably (165 ms vs CPython 64 ms).

**Where the 2.55× gap actually is.**  An earlier iteration of this
README speculated that `globalMutex` contention in `getFreeCells`
plus per-context `new DirtySegment` mallocs were the dominant cost.
A DWARF profile of the working benchmark refutes that.  Per-thread
allocation batches in `getFreeCells` are sized
`min(blocksPerAllocation * runningThreads * 4, 65536)` when more than
one thread is running (i.e. **65 536 cells per refill** with 4 workers
+ GC), and the benchmark allocates well under that, so each worker
hits `getFreeCells` exactly once.  Profile shares confirm:

```
13.47 %  executeBytecodeRange   ← interpreter dispatch loop
 6.48 %  ProtoObject::getAttribute
 3.14 %  ProtoSpace::getFreeCells   (≈ 4 calls × ~1.3 ms; well-amortised)
 2.81 %  __tls_get_addr             ← thread-local storage lookups
 2.71 %  proto::isObject
 1.76 %  diagEnabled                ← env check still in the hot path
```

The 2.55× gap is dominated by per-bytecode interpreter cost
(executeBytecodeRange, getAttribute, TLS lookups, and the residual
`diagEnabled` PLT stubs).  Tier 2 (inline SmallInteger arithmetic on
the dispatch loop) and a follow-up to fully fold `diagEnabled` into a
constant in release builds are the next wins; allocator contention is
already amortised.  When parallelism actually beats CPython on this
benchmark it will be because the per-bytecode cost in protoPython
dropped, not because the allocator changed.

---

## �🚀 Quick Start

### Build and Install
For detailed, platform-specific instructions (including install prefix and library path), see the [**Installation Guide**](docs/INSTALLATION.md).

```bash
# Clone protoPython and protoCore (protoCore can be a sibling directory or installed separately)
git clone https://github.com/numaes/protoPython.git
cd protoPython
git clone https://github.com/numaes/protoCore.git ../protoCore

# Configure and build (install is optional; see docs for install location and prefix)
cmake -B build -S .
cmake --build build
# Optional: install to a prefix (default /usr/local; use -DCMAKE_INSTALL_PREFIX=... for a local install)
# cmake --build build --target install
```

**Running without installing:** You can run from the build tree; RPATH is set so the executables find the shared libraries without setting `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`. See the [Installation Guide](docs/INSTALLATION.md#running-from-the-build-tree).

### Run a Python Script
```python
# example.py
l = [1, 2]
l.append(3)
print("Updated list:", l)

s = "Hello World"
print("Starts with 'Hello':", s.startswith("Hello"))
```

Execute it with `protopy` (from the build tree or from your `PATH` after install):
```bash
# From build tree (see Installation Guide for library path on macOS/Linux)
./build/src/runtime/protopy example.py
# Or, after install: protopy example.py
```

---

## 🏗️ Components

- **protopy**: The primary GIL-less Python 3.14 execution environment. (**Ready for community review**)
- **libprotoPython**: The shared library providing the Python runtime environment for embedding in C++ applications. (**Ready for community review**)
- **protopyc**: A specialized compiler that translates Python modules into high-performance C++ shared libraries based on `protoCore`. (**Ready for community review**)

---

## 🏗️ Architecture

```mermaid
graph TD
    A[Python Source/Bytecode] --> B[Parser/Compiler]
    B --> C[Execution Engine]
    C --> D[protoPython Runtime Layer]
    D --> E[TypeBridge & HPy Layer]
    E --> F[protoCore Object System]
    F --> G[Memory/GC/Threads]
```

---

## The Swarm of One

**The Swarm of One** enables a paradigm shift in software development. In protoPython, we transitioned from core infrastructure to a fully functional, highly parallel runtime in record time. By orchestrating a swarm of specialized AI agents, a single architect has built a GIL-less environment where Python objects share the same 64-byte cell DNA as protoCore and protoJS. This is the democratization of high-level engineering: bridging language paradigms without the traditional overhead of massive R&D teams.

---

## The Methodology: AI-Augmented Engineering

This project was built using **extensive AI-augmentation tools** to empower human vision and strategic design. This is not "AI-generated code" in the traditional sense; it is **AI-amplified architecture**. The vision, the constraints, and the trade-offs are human; the execution is accelerated by AI as a force multiplier for complex system design. We embrace AI as the unavoidable present of software engineering.

---

## 📚 Documentation

- [**User Guide**](docs/USER_GUIDE.md) — Comprehensive guide for Python developers.
- [**Python Compatibility**](docs/PYTHON_COMPATIBILITY.md) — Supported features and differences from CPython.
- [**C++ API Reference**](docs/CPP_API_REFERENCE.md) — Documentation for embedding and native extensions.
- [**Internals Deep Dive**](docs/INTERNALS_DEEP_DIVE.md) — Technical exploration of the GIL-free runtime.
- [**Design Decisions**](docs/DESIGN_DECISIONS.md) — Architectural rationale and history.
- [**Installation Guide**](docs/INSTALLATION.md) — Platform-specific build instructions.
- [**HPy Guides**](docs/HPY_USER_GUIDE.md) — Loading and developing HPy extensions.
- [**Regression Audit**](docs/EXECUTION_ENGINE_REGRESSIONS.md) — Record of execution engine fixes.

---

## License & Author

Copyright (c) 2026 Gustavo Marino <gamarino@gmail.com>

Licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

## Lead the Shift

**Lead the Shift.** Don't just watch the Python ecosystem evolve—be the one who drives it. Join the review, test the GIL-less performance, and become part of the Swarm of One. **Think Different, As All We.**

