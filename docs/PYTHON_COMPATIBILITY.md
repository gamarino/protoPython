# Python Compatibility Guide

ProtoPython aims for high compatibility with **Python 3.14**. This document details the supported features, built-in types, and notable differences from the standard CPython implementation.

## Core Syntax

| Feature | Status | Notes |
|---------|--------|-------|
| Control Flow | Supported | `if`, `for`, `while`, `try`, `except`, `finally`, `with` |
| Functions | Supported | Global, nested, `lambda`, default arguments, `*args`, `**kwargs` |
| Classes | Supported | Multiple inheritance, dunder methods, properties |
| Generators | Supported | `yield`, `yield from`. |
| Async/Await | Supported | `async def`, `await`, `async for`, `async with`; `asyncio.run` works, with the asyncio limitations listed under known divergences. |
| Comprehensions | Supported | List, dict, and set comprehensions. |
| Slicing | Supported | Full support for `[start:stop:step]` including negative indices. |

## Built-in Types

- **Numeric**: `int` (arbitrary precision), `float` (64-bit double), `bool`.
- **Sequences**: `list`, `tuple`, `range`, `bytes`.
- **Mappings**: `dict`.
- **Sets**: `set`.
- **Text**: `str` (Unicode-aware, implemented as high-performance ropes).
- **Other**: `NoneType`, `ellipsis`, `slice`.

## The "GIL-Free" Difference

The most fundamental difference from CPython is the **absence of the Global Interpreter Lock (GIL)**.

1. **Parallel Threads**: `threading.Thread` creates native OS threads that execute Python code in parallel on separate cores.
2. **Mutations**: While collections like `list` and `dict` are mutable, the underlying `protoCore` engine manages concurrent access. However, for complex shared state, standard synchronization primitives (`threading.Lock`) are still recommended.
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
- **Tracebacks**: unhandled exceptions print a `Traceback (most recent call last)` report; frames often show `File "<unknown>"` without a line number.

## Notable Differences from CPython

1. **Memory Management**: Uses `protoCore`'s hybrid GC and context-based promotion rather than simple reference counting. `sys.getrefcount` is not supported.
2. **C API**: The CPython C API is not supported. protoPython contains an HPy-style C++ extension API, not binary compatible with the HPy universal ABI, whose modules `import` loads; see [HPY_DEVELOPER_GUIDE.md](HPY_DEVELOPER_GUIDE.md).
3. **Known semantic gaps**: frozen dataclasses, some introspection (`types.FunctionType` with a closure, `inspect.getclosurevars`), a few error messages and reprs, and `gc.collect()` being a no-op. The maintained list is in [CPYTHON_CONFORMANCE.md](CPYTHON_CONFORMANCE.md#known-divergences-pending).
4. **Frame objects** (`sys._getframe()`, `inspect.currentframe()`, `f_back`):
   - `f_locals` at module level is the module namespace, the same object as
     `f_globals` and `globals()`, as in CPython. In a function it is a **snapshot
     dict** of the local variables taken when the attribute is read; CPython 3.13+
     returns a write-through `FrameLocalsProxy`, so writing to it does not change
     the function's variables here. Cell variables of a function whose locals live
     in fast slots may be missing from the snapshot.
   - `exec(source)` without explicit namespaces runs, inside a function, in a frame
     of its own whose `f_back` is the calling function (so the setuptools
     `*-nspkg.pth` line `sys._getframe(1).f_locals['sitedir']` works). It reads the
     function's variables; names it binds stay in that frame and are not visible to
     the function afterwards, as with CPython 3.13+'s snapshot semantics.
   - `f_lineno` is the first line of the frame's code object (`co_firstlineno`; 1
     for a module): the runtime records the executing line only when an exception
     is raised.
   - Small functions that build no inner functions or classes run without a frame
     object, so they do not appear in the `f_back` chain, and `sys._getframe()`
     called from such a function's callee skips it. A function that names
     `_getframe` or `currentframe` always gets a frame, so the common
     `sys._getframe(n)` and `inspect.currentframe()` patterns see their callers.
5. **Import system**: the `import` statement resolves modules natively
   (native modules, protopyc-compiled modules, HPy extensions, then `.py`
   files on `sys.path`) and does not consult `sys.meta_path`,
   `sys.path_hooks` or `sys.path_importer_cache`.
   - `sys.meta_path` is empty: `PathFinder` is not on it, so
     `importlib.util.find_spec(name)` answers `None` for a module that has not
     been imported (`import_module` falls back to the native importer).
   - `sys.path_hooks` holds `zipimport.zipimporter` and
     `FileFinder.path_hook(...)` for source files (`.py`) only. No
     `SourcelessFileLoader`: protoPython writes no `.pyc` files and cannot run
     CPython's. No `ExtensionFileLoader`: extension modules are imported only
     by the `import` statement (`_imp` has no `create_dynamic`, as on a
     CPython built without dynamic loading). `sys.path_importer_cache`
     caches the finders. Both are created, and `importlib` imported to fill
     them, on their first use (a module `__getattr__` on `sys`); CPython fills
     them at startup.
   - Namespace packages (PEP 420) are imported natively: a directory without
     `__init__.py` found on `sys.path` becomes a namespace package when no
     module or regular package of that name is found anywhere on the path.
     Its `__path__` is a plain list of the portions found at import time
     (CPython's `_NamespacePath` also follows later `sys.path` changes), and
     its `__spec__` is a plain object with `origin` None and
     `submodule_search_locations` the same list. A submodule is also searched
     for in its parent package's `__path__`.
   - `compile()` accepts source as `bytes`, decoded as UTF-8; a coding
     declaration naming another encoding is not honoured.
   - No `.pyc` files are written (`sys.dont_write_bytecode` is True).
6. **Interpreter exit**: `atexit` handlers run once, when the program ends
   (normally, through `SystemExit` or after an unhandled exception is
   reported). A handler that raises is reported through `sys.unraisablehook`
   as `Exception ignored in atexit callback <function f at 0x...>:` followed
   by the traceback, and the remaining handlers run. An application that
   embeds `PythonEnvironment` calls `runExitHandlers()` at shutdown.
   `gc.disable()`/`gc.enable()` only change what `gc.isenabled()` reports;
   protoCore's collector keeps running.
