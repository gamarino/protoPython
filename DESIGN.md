# protoPython Technical Design

## Introduction

protoPython is a Python 3.14 runtime without a Global Interpreter Lock (GIL), built on
[protoCore](https://github.com/numaes/protoCore). This document describes its
components and the main design choices. protoPython 1.0.0 is not production ready; the
current conformance status is recorded in
[docs/CPYTHON_CONFORMANCE.md](docs/CPYTHON_CONFORMANCE.md).

## 1. Core principles

- **No GIL**: Python threads are native threads that execute in parallel; protoPython
  relies on protoCore's concurrency primitives instead of a global lock.
- **protoCore foundation**: every Python value is a protoCore `ProtoObject` or is built
  from protoCore objects and collections.
- **Python 3.14 compatibility**: the target is the Python 3.14 language and standard
  library; known divergences are listed in the conformance document.
- **Two execution paths**: `protopy` interprets protoPython bytecode; `protopyc`
  translates Python source into C++.

## 2. Components

### 2.1 protoPython library (`libprotoPython`)

- **Environment**: `PythonEnvironment` holds the runtime state: built-in types,
  `builtins`, `sys` and module resolution. All environments use the process-wide
  `ProtoSpace` returned by `PythonEnvironment::getProcessSpace()`.
- **Object model**: built on protoCore prototypes. Instances created by built-in
  constructors (`set()`, `list()` and so on) set their `__class__` attribute explicitly,
  so identity and attribute lookup follow Python semantics where plain prototype
  inheritance would differ.
- **Built-in types** map to protoCore structures:
  - `int`: protoCore integers (tagged small integers, with arbitrary precision beyond
    their range);
  - `str`: `ProtoString`, a rope structure;
  - `list`: a `list` instance whose `__data__` attribute holds a `ProtoList` (a balanced
    tree);
  - `dict`: a `dict` instance whose `__data__` attribute holds a `ProtoSparseList` keyed
    by hash and whose `__keys__` attribute holds a `ProtoList` that keeps insertion
    order;
  - `set`: `ProtoSet`.
- **Module resolution**: `PythonEnvironment` registers module providers with protoCore's
  provider registry (Unified Module Discovery): `NativeModuleProvider` for modules
  implemented in C++, `PythonModuleProvider` for `.py` sources and
  `CompiledModuleProvider` for shared libraries produced by `protopyc`.
- **Frontend**: a tokenizer, parser and compiler (`Tokenizer.h`, `Parser.h`,
  `Compiler.h`) compile Python source into protoPython bytecode.
- **Runtime objects**: frames, functions, generators, coroutines and exceptions are
  implemented in the library on top of protoCore objects.

### 2.2 protopy runtime

`protopy` (`src/runtime/main.cpp`) creates a `PythonEnvironment` and runs a script, a
module, a `-c` string or the REPL. It is a bytecode executor inside protoPython, not a
fork of CPython; see [docs/PROTOPY_SCOPE.md](docs/PROTOPY_SCOPE.md). Command-line usage
and exit statuses are described in [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

- **Execution engine** (`src/library/ExecutionEngine.cpp`): a `switch`-based interpreter
  over protoPython bytecode. Each thread executes with its own chain of `ProtoContext`
  objects, one per call.
- **Memory management and GC safety**:
  - The operand stack of the execution engine is a root for the protoCore garbage
    collector.
  - Rooting policy: operands and intermediate results stay on the stack until an
    operation completes; popping the operands is the last step before pushing the
    result.
  - In-place mutation: opcodes overwrite stack slots in place where possible to reduce
    allocation.
  - Native code that keeps `ProtoObject` pointers where the collector cannot see them
    must pin them; see [docs/GC_BRIDGING.md](docs/GC_BRIDGING.md).
- **Script entry point**: as in CPython, protopy executes a script's module body once as
  `__main__` and never calls a `main` function on its own; a script runs it explicitly,
  typically under `if __name__ == "__main__":`.
- **Threading**: `_thread.start_new_thread`, on which `threading` is built, creates
  native threads through protoCore's `ProtoSpace::newThread`.

### 2.3 protopyc compiler

`protopyc` (`src/compiler/`) translates Python source into C++ that calls
`PythonEnvironment` and protoCore directly.

- **Translation**: one `.cpp` file per `.py` file, with `#line` directives that refer
  back to the Python source.
- **Specialisation**: arithmetic and comparisons get an inline fast path when both
  operands are protoCore small integers, and fall back to
  `PythonEnvironment::binaryOp` otherwise.
- **Packaging**: `--emit-make` writes a Makefile and `--build-so` builds a shared
  library. A generated module exports `proto_module_init`; `CompiledModuleProvider`
  loads `<module>.so` files found on the module search path.

Details are in [docs/PROTOPYC_SPECIFICATION.md](docs/PROTOPYC_SPECIFICATION.md).

### 2.4 Bytecode

A compiled code object stores its instructions in `co_code`, a flat sequence of
integers. Every instruction occupies two slots: the opcode and its argument, which is 0
when the opcode takes none. Arguments are full integers, not bytes, and several opcodes
pack more than one value into the argument (for example `UNPACK_EX` uses
`(count_after << 8) | count_before`). The opcodes (codes 100-214) are defined in
`include/protoPython/ExecutionEngine.h` and listed in
[docs/EXECUTION_ENGINE_OPCODES.md](docs/EXECUTION_ENGINE_OPCODES.md). The names follow
CPython, but the codes, argument encodings and semantics are protoPython's own.

After jump targets are resolved, a peephole pass (`Compiler::specialiseBytecode`)
replaces three common four-instruction sequences with fused opcodes. Each fused opcode
packs two operand indices, each at most 255, into its argument as `(a << 8) | b`:

| Fused opcode | Replaces |
|--------------|----------|
| `OP_ACC_FAST_FAST` (212) | `LOAD_FAST a; LOAD_FAST b; INPLACE_ADD; STORE_FAST a` |
| `OP_INC_FAST_K` (213) | `LOAD_FAST i; LOAD_CONST k; INPLACE_ADD; STORE_FAST i`, for a small-integer constant `k` |
| `OP_LT_FAST_FAST_JF` (214) | `LOAD_FAST a; LOAD_FAST b; COMPARE_OP <; POP_JUMP_IF_FALSE target` |

The other six slots of the replaced window become `NOP`, so the offsets of later
instructions, and therefore all jump targets, stay unchanged. `OP_LT_FAST_FAST_JF`
keeps its jump target in the argument slot of the last replaced instruction. Setting
`PROTOPY_NO_PEEPHOLE=1` disables the pass.

### 2.5 Standard library

- The pure-Python modules come from `lib/python3.14`, a copy of the CPython 3.14
  standard library kept in this repository.
- Selected modules that CPython implements in C are implemented in C++ in
  `src/library/` (for example `_io`, `_collections`, `_functools`, `_struct` and
  `_thread`); see [docs/C_MODULES_TO_REPLACE.md](docs/C_MODULES_TO_REPLACE.md).

### 2.6 Debugging support

- The `sys` module provides `settrace`, and `protopy --trace` prints module enter and
  leave events.
- `include/protoPython/DAPServer.h` is a placeholder for Debug Adapter Protocol support:
  `DAPServer::isSupported()` returns `false`, and no DAP server is implemented.

## 3. Testing

- `ctest` runs the C++ unit tests under `test/library` and `test/compiler` and the
  Python regression scripts under `test/regression`; the suite has 410 tests on
  2026-09-16.
- `test/regression/run_and_report.py` runs CPython regression tests against `protopy`
  and records pass rates; see
  [docs/COMPATIBILITY_DASHBOARD.md](docs/COMPATIBILITY_DASHBOARD.md).
- Behaviour differences found by running the same script under CPython and protopy are
  tracked in [docs/CPYTHON_CONFORMANCE.md](docs/CPYTHON_CONFORMANCE.md).

## 4. Object identity

All objects live in the process's single `ProtoSpace`. `id(obj)` returns the value of
the object's `ProtoObject` pointer.

## 5. Concurrency model

- There is no global interpreter lock. protoCore collections are immutable and share
  structure between versions, and updates to mutable objects rely on protoCore's atomic
  compare-and-swap operations.
- The current `ProtoContext` and the pending exception are thread-local.
- Locks that remain in protoPython belong to the Python API (for example `_thread`
  locks) rather than to the interpreter. See
  [docs/L_SHAPE_ARCHITECTURE.md](docs/L_SHAPE_ARCHITECTURE.md).

## 6. Status

The development history and its phases are recorded in
[docs/archive/IMPLEMENTATION_PLAN.md](docs/archive/IMPLEMENTATION_PLAN.md). Current
status and known divergences: [docs/CPYTHON_CONFORMANCE.md](docs/CPYTHON_CONFORMANCE.md).
