# Python Compatibility Guide

ProtoPython aims for high compatibility with **Python 3.14**. This document details the supported features, built-in types, and notable differences from the standard CPython implementation.

## Core Syntax

| Feature | Status | Notes |
|---------|--------|-------|
| Control Flow | Supported | `if`, `for`, `while`, `try`, `except`, `finally`, `with` |
| Functions | Supported | Global, nested, `lambda`, default arguments, `*args`, `**kwargs` |
| Classes | Supported | Multiple inheritance, dunder methods, properties |
| Generators | Supported | `yield`, `yield from`. High-performance state persistence. |
| Async/Await | Supported | Native coroutines and task orchestration. |
| Comprehensions | Supported | List, dict, and set comprehensions. |
| Slicing | Supported | Full support for `[start:stop:step]` including negative indices. |

## Built-in Types

- **Numeric**: `int` (arbitrary precision), `float` (64-bit double), `bool`.
- **Sequences**: `list`, `tuple`, `range`, `bytes`.
- **Mappings**: `dict`.
- **Sets**: `set`.
- **Text**: `str` (Unicode-aware, implemented as high-performance ropes).
- **Other**: `NoneType`, `ellipses`, `slice`.

## The "GIL-Free" Difference

The most fundamental difference from CPython is the **absence of the Global Interpreter Lock (GIL)**.

1. **Parallel Threads**: `threading.Thread` creates native OS threads that execute Python code in parallel on separate cores.
2. **Mutations**: While collections like `list` and `dict` are mutable, the underlying `protoCore` engine managed concurrent access. However, for complex shared state, standard synchronization primitives (`threading.Lock`) are still recommended.
3. **Immutability**: Strings and tuples use structural sharing. Mutating a "mutable" collection often leverages copy-on-write (CoW) or balanced tree updates for efficiency and safety.

## Built-in Functions and Modules

### Supported Built-ins
`print`, `len`, `type`, `getattr`, `setattr`, `hasattr`, `range`, `enumerate`, `zip`, `any`, `all`, `sum`, `min`, `max`, `abs`, `round`, `int`, `str`, `float`, `list`, `dict`, `tuple`, `set`, `bool`.

### Supported Standard Library Modules (Subset)
- `sys`: Core system parameters and functions.
- `threading`: Native GIL-free threading support.
- `time`: Time access and conversions.
- `builtins`: Built-in namespace.
- `_collections`: High-performance collections (e.g., `deque`).
- `_functools`: Higher-order functions.

## Implementation Details

- **Object Model**: Objects are 64-byte aligned cells in a `ProtoSpace`.
- **Recursion**: Managed limit via `sys.setrecursionlimit`.
- **Error Formatting**: Rich, metadata-aware tracebacks with snippet highlighting.

## Notable Differences from CPython

1. **Memory Management**: Uses `protoCore`'s hybrid GC and context-based promotion rather than simple reference counting. `sys.getrefcount` is not supported.
2. **Performance Profile**: Startup is significantly faster. CPU-bound threaded code scales linearly with cores.
3. **C API**: Does not support the CPython C API directly. Use **HPy** for writing or porting C extensions.
