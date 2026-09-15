# C Modules to Replace for the protoPython Standard Library

CPython implements several standard library modules, or accelerators for them, in C.
protoPython cannot load CPython C extensions, so it either re-implements these modules
in C++ (in `src/library/`, registered as native modules in
`src/library/PythonEnvironment.cpp`) or relies on pure-Python code in `lib/python3.14`.
See also [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md).

## Priority

- **High**: required for basic imports and many regression tests.
- **Medium**: needed by a substantial part of the tests or of common standard library use.
- **Low**: can be deferred; pure-Python fallbacks or stubs may be enough.

## Module list

The status column was checked on 2026-09-15 by importing each module with `protopy`.

| Module | Priority | Status |
|--------|----------|--------|
| `_collections` | High | Native C++ module (`CollectionsModule.cpp`). |
| `_functools` | High | Native C++ module (`FunctoolsModule.cpp`). |
| `_operator` | High | Native C++ module (`OperatorModule.cpp`). |
| `_io` | High | Native C++ module (`IOModule.cpp`). |
| `_socket` | Medium | Pure-Python module `lib/python3.14/_socket.py`. |
| `_ssl` | Medium | Pure-Python module `lib/python3.14/_ssl.py`. |
| `_json` | Medium | Not available; no native implementation. `json` uses its pure-Python implementation. |
| `_pickle` | Medium | Not available; `pickle` uses its pure-Python implementation. |
| `_struct` | Medium | Native C++ module (`StructModule.cpp`). |
| `array` | Medium | Pure-Python module `lib/python3.14/array.py`. |
| `_heapq` | Medium | Not available; no native implementation. `heapq` uses its pure-Python implementation. |
| `_random` | Low | Pure-Python module `lib/python3.14/_random.py`. |
| `_datetime` | Low | Native C++ module (`DatetimeModule.cpp`). |
| `_hashlib` | Low | Not available. |

Other native modules registered by `PythonEnvironment` include `sys`, `builtins`,
`_thread`, `_signal`, `_weakref`, `_codecs`, `_ast`, `_imp`, `_warnings`, `_string`,
`_stat`, `_opcode`, `_posixsubprocess`, `math`, `time`, `itertools`, `re`, `errno`,
`marshal`, `binascii`, `fcntl`, `select`, `faulthandler`, `atexit` and
`exceptions`. `pathlib` is the standard library package in `lib/python3.14/pathlib`;
the native stub that shadowed it was removed.

## References

- [archive/IMPLEMENTATION_PLAN.md](archive/IMPLEMENTATION_PLAN.md): the original plan
  (section 2, standard library integration).
- [STUBS.md](STUBS.md): catalogue of stub implementations.
