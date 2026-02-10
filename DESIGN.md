# DESIGN: protoPython Technical Architecture

## Introduction

This document outlines the technical design for **protoPython**, a Python 3.14 compatible environment designed for high performance and massive parallelism by eliminating the Global Interpreter Lock (GIL) and leveraging the **protoCore** foundation.

## 1. Core Principles

- **No GIL**: Purely multi-threaded execution using `protoCore`'s concurrency primitives.
- **protoCore Foundation**: All Python objects are represented as `ProtoObject`s or specialized versions thereof.
- **Python 3.14 Compatibility**: Adherence to the Python 3.14 language specification and standard library expectations.
- **Native Efficiency**: Compiling Python logic to C++ via `protopyc` for maximum performance.

## 2. Component Architecture

### 2.1. protoPython Shared Library (`libprotoPython.so`)

The heart of the project, providing the runtime infrastructure.

- **Environment Management**: Defines `PythonEnvironment` which resides in a `ProtoSpace`.
- **Object Model**:
    - Implements the base `object` class.
    - Implements fundamental dunder methods (`__init__`, `__new__`, `__call__`, `__getattr__`, etc.).
    - Maps Python types (int, str, list, dict, set) to `protoCore` optimized structures:
        - **list**: `ProtoList` (balanced AVL binary tree, $O(\log N)$ access/modification).
        - **str**: `ProtoString` (rope structure based on `ProtoTuple`, $O(1)$ concatenation).
        - **dict**: `ProtoSparseList`.
        - **set**: `ProtoSet`.
- **Import Logic**: A `protoCore`-native import system capable of loading both Python modules (compiled) and native C++ modules.
- **Runtime Objects**: Implementation of frames, generators, coroutines, and exceptions using `protoCore` threading and fiber models.

### 2.2. protopy Runtime

The execution engine for Python scripts.

- **Fork of CPython 3.14**: Maintains the frontend (parser, AST) but replaces the bytecode evaluation loop and memory management.
- **Execution Engine**: Uses the `protoPython` library to execute logic without a GIL.
- **Module main**: When running a script or module, protopy resolves it and, if the module has a callable `main` attribute, invokes it (stub execution path until full bytecode is in place).
- **Threading**: Native mapping of Python `threading` to `protoCore` threads.

### 2.3. protopyc Compiler

A static compiler for Python code.

- **Translation**: Converts Python code into C++ source code.
- **Optimization**: Performs type inference (where possible) and optimizes calls to `protoCore` and `protoPython` APIs.
- **Packaging**: Compiles a directory of Python modules into a single shared library (`.so`) that can be loaded by `protopy` or linked directly by C++ applications.

### 2.4. Standard Library Integration
- **Overlay Architecture**: Pure-Python modules are used directly from CPython 3.14.
- **C-to-C++ Transition**: Modules traditionally implemented in C (e.g., `_io`, `_socket`) are re-implemented in C++ for `protoPython` to ensure GIL-less performance and a clean API.

### 2.5. Debugging & IDE Support
- **Tracing Support**: Native implementation of tracing hooks (`sys.settrace`).
- **DAP Integration**: Support for the Debug Adapter Protocol to enable a premium debugging experience in IDEs like VS Code and PyCharm.

## 3. Compatibility & Testing
- **CPython Regression Tests**: Direct execution of CPython's regression test suite (`test.regrtest`) to ensure 1:1 behavioral parity.

## 4. Data Representation

Python objects in `protoPython` will be stored in `ProtoSpace`.
- **Small Objects**: Inlined or optimized within `ProtoSpace`.
- **Large Collections**: Using `protoCore`'s thread-safe collection types.
- **Identity**: `id(obj)` will correspond to the `ProtoObject` address/identifier.

## 6. Concurrency Model

Instead of a GIL, `protoPython` will use:
- **Fine-grained locking**: For shared mutable structures where necessary (provided by `protoCore`).
- **Atomic operations**: For reference counting (if required) or state transitions.
- **Isolated Spaces**: Capability to run independent Python environments in separate `ProtoSpace` instances for maximum isolation and scaling.

**Phase 4 concurrency policy:** A single `PythonEnvironment` and `ProtoContext` may be used from multiple threads. Attribute mutations rely on protoCore's lock-free CAS; protoPython's shared state (trace, resolve cache, context map) is made thread-safe. See [docs/GIL_FREE_AUDIT.md](docs/GIL_FREE_AUDIT.md).

## 7. Development Phases

1.  **Phase 1**: Foundation - Basic `protoPython` library with `object` and core dunder methods. Initial `ProtoSpace` integration [COMPLETED].
2.  **Phase 2**: Objects & Types - Implementation of fundamental Python types (int, str, list, dict) [COMPLETED].
3.  **Phase 3**: StdLib Infrastructure - Import system and pure-Python library layout [COMPLETED].
4.  **Phase 4**: Core Module Replacement - Re-implementing `builtins`, `sys`, and `_io` in C++ [COMPLETED].
5.  **Phase 5**: Debugging & Tooling - Tracing hooks (`sys.settrace`) [COMPLETED]. DAP support and IDE integration [IN PROGRESS].
6.  **Phase 6**: protopy & protopyc - protopy first: minimal runtime and bytecode executor using `PythonEnvironment` and protoCore; then protopyc AOT compiler integration.
7.  **Phase 7**: Full Compatibility - Passing the CPython regression test suite.
