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
    - Maps Python types (int, str, list, dict, set) to `protoCore` optimized structures (e.g., `ProtoSparseList`, `ProtoSet`).
- **Import Logic**: A `protoCore`-native import system capable of loading both Python modules (compiled) and native C++ modules.
- **Runtime Objects**: Implementation of frames, generators, coroutines, and exceptions using `protoCore` threading and fiber models.

### 2.2. protopy Runtime

The execution engine for Python scripts.

- **Fork of CPython 3.14**: Maintains the frontend (parser, AST) but replaces the bytecode evaluation loop and memory management.
- **Execution Engine**: Uses the `protoPython` library to execute logic without a GIL.
- **Threading**: Native mapping of Python `threading` to `protoCore` threads.

### 2.3. protopyc Compiler

A static compiler for Python code.

- **Translation**: Converts Python code into C++ source code.
- **Optimization**: Performs type inference (where possible) and optimizes calls to `protoCore` and `protoPython` APIs.
- **Packaging**: Compiles a directory of Python modules into a single shared library (`.so`) that can be loaded by `protopy` or linked directly by C++ applications.

## 3. Data Representation

Python objects in `protoPython` will be stored in `ProtoSpace`.
- **Small Objects**: Inlined or optimized within `ProtoSpace`.
- **Large Collections**: Using `protoCore`'s thread-safe collection types.
- **Identity**: `id(obj)` will correspond to the `ProtoObject` address/identifier.

## 4. Concurrency Model

Instead of a GIL, `protoPython` will use:
- **Fine-grained locking**: For shared mutable structures where necessary (provided by `protoCore`).
- **Atomic operations**: For reference counting (if required) or state transitions.
- **Isolated Spaces**: Capability to run independent Python environments in separate `ProtoSpace` instances for maximum isolation and scaling.

## 5. Development Phases

1.  **Phase 1**: Foundation - Basic `protoPython` library with `object` and core dunder methods. Initial `ProtoSpace` integration.
2.  **Phase 2**: Objects & Types - Implementation of fundamental Python types (int, str, list, dict).
3.  **Phase 3**: Import System - Logic for loading modules.
4.  **Phase 4**: protopyc - Basic AOT compilation of Python scripts.
5.  **Phase 5**: protopy - Bytecode executor/C-extension integration.
6.  **Phase 6**: Standard Library - Porting/Implementing core stdlib modules.
