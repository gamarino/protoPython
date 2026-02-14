# ProtoPython Internals Deep Dive

This document explores the advanced architectural details of the **ProtoPython** runtime.

## 1. Hardware-Aligned Memory Model

ProtoPython is built on [**protoCore**](../protoCore/), which uses a "hardware-aware" memory topology.

- **64-Byte Alignment**: Every object (Cell) is aligned to 64-byte boundaries. This matches modern CPU cache line sizes, preventing false sharing and maximizing cache efficiency.
- **Pointer Tagging**: We use the lower bits of 64-bit pointers to store tag information (type, embedded values like small integers). This enables O(1) type checking without dereferencing the pointer.
- **L-Shape Topology**: Memory is organized into a hierarchy of `ProtoSpace` (global) and `ProtoContext` (thread-local). Objects are "promoted" from local contexts to the global space as needed.

## 2. GIL-Free Execution Engine

The Execution Engine (`ExecutionEngine.cpp`) is a specialized bytecode interpreter designed for high-performance, parallel execution.

### The Interpreter Loop
The engine uses a tight `switch` statement over opcodes. Since there is no Global Interpreter Lock (GIL), multiple threads can execute the interpreter loop simultaneously on different `ProtoContext` stacks.

### `GCStack` and Frames
Execution state is managed via `GCStack`. 
- **Frames**: Each function call creates a frame that stores local variables and the evaluation stack.
- **Optimization**: We use hardware-aligned memory for frames to ensure that local variable access is extremely fast.

## 3. Immutable Core and Structural Sharing

Many core types in ProtoPython leverage protoCore's immutable-by-default primitives.

- **Strings (Ropes)**: Large strings are implemented as ropes, allowing for O(1) concatenation without copying data.
- **Tuples (Balanced Trees)**: Large tuples utilize structural sharing, making them thread-safe and memory-efficient.
- **Lists and Dicts**: While providing a mutable API to Python, the underlying implementation uses balanced trees (AVL) and copy-on-write strategies to ensure thread safety without heavy global locks.

## 4. The Zero-Copy Principle

ProtoPython minimizes data movement between layers.

- **Function Returns**: When a function returns, the result is "promoted" to the caller's context by simply updating a pointer and updating the context ownership. No `memcpy` is performed.
- **Native Bridge**: Passing data from C++ to Python (e.g., a massive image buffer) uses the **Unified Memory Bridge (UMD)**. This passes the raw pointer with appropriate ownership metadata, allowing Python to treat native C++ memory as a native Python `bytes` or `buffer` object.

## 5. Metadata and Prototyping

ProtoPython implements a sophisticated prototype-based object model.

- **Prototype Linkage**: Every object can have one or more parents. Attribute lookup traverses this parent chain.
- **`co_name` and Introspection**: Code objects store metadata like `co_name` for improved diagnostics and representation.
- **Standardized Dunder Lookups**: Lookups for `__repr__`, `__str__`, etc., are optimized and standardized in `PythonEnvironment` to handle both built-in and user-defined types efficiently.
