# CPython Conformance Tracker

This document tracks protoPython's current conformance with CPython: the latest
status of the runtime-semantics work and the known divergences that are still
pending.

Earlier status reports (dated snapshots, the `test_descr.py` sweep rounds and the
V-numbered change entries) are kept verbatim in
[archive/CPYTHON_CONFORMANCE_HISTORY.md](archive/CPYTHON_CONFORMANCE_HISTORY.md).
They are historical; the note at the top of that file explains why the pass
counts in the V70-V154 entries cannot be reproduced.

---

## Current Status (2026-09-14) — runtime semantics sweep

A sequence of fix rounds driven by CPython probes (the same script run
under `python3` and `protopy`, outputs diffed).  Every commit fixes one
defect and adds a `test/regression/*.py` test registered in
`CMakeLists.txt`; every commit was built on its own and passes its test.

### Landed

- **85260941..aae0ba45** (13 commits): `del name`, `class C(*bases,
  **kw)`, PEP 695 generics, `exec`/`eval` of code objects,
  `list.__init__`, `__init_subclass__` from `type()`, `reversed()`,
  OrderedDict and deque fixes, `sorted()` over any iterable, TypeVar
  `__module__`.
- **aae0ba45..19846d65** (28 commits): zero-argument `__class__` in
  methods, fast locals start unbound, `x[*a]`, `compile(..., "single")`,
  StopIteration value, `str(exception)`, `isinstance(str, str)`,
  `Generic[T, *Ts]`, `globals()` / `vars(module)` as mappings, rich
  comparison results and NotImplemented → TypeError for orderings
  (containers, `sorted`, `list.sort`), `object.__init_subclass__`
  keywords, builtin function types, `map(strict=True)`, `import asyncio`,
  NotImplemented/Ellipsis repr, `str(KeyError)`, duplicate call
  keywords, `builtins.x = v`, module globals no longer exposing
  inherited attributes.
- **19846d65..1645d612** (6 commits): bytes vs str comparisons, name
  lookups no longer import modules by bare name (this hid a missing
  `import abc` in `os.py` and `import sys` in `enum.py`), list/tuple/dict
  `__eq__` NotImplemented, `delattr()` really deletes, `nonlocal`
  rebinding (which made `asyncio.run` with `gather` hang), submodule
  imported before its package.
- **1645d612..fb3d00d9** (3 commits): `@dataclass(slots=True)`
  (`itertools.filterfalse` was not callable), `function.__closure__` as a
  tuple of cells with `types.CellType`, `type(module)` is
  `<class 'module'>`.

### Verification

- ctest 304/304 at fb3d00d9.
- ASAN (RelWithDebInfo, `detect_leaks=0`): 44 regression tests × 3 runs
  clean at fb3d00d9.

### Known divergences (pending)

- **Dataclasses:** `frozen=True` fails in `__init__`:
  `object.__setattr__(obj, k, v)` dispatches to the subclass's
  `__setattr__`.
- **Closures:** a method of a class nested in a function gets
  `co_freevars == ('__class__', 'C')`; `f.__closure__[0] is
  f.__closure__[0]` is False (cells are built on each access);
  `del cell.cell_contents` is not supported; `nonlocal` inside a class
  body is untested.
- **Introspection:** `types.FunctionType(code, globals, name, defs,
  closure)` fails; `inspect.getclosurevars` fails because `dis` cannot
  decode protoPython bytecode; `types.FrameType` is `object`.
- **Attributes:** instances see class attributes such as `[].__name__`
  and `C().__mro__`; `dir(module)` lists `__qualname__`, `__module__` and
  `__class__`.
- **Comparisons:** `object.__ne__` runs a full comparison, so
  `[].__ne__(())` is True where CPython returns NotImplemented.
- **Error messages and reprs:** `sys.x` raises "'object' object has no
  attribute 'x'" (CPython: "module 'sys' has no attribute 'x'");
  `repr(cls)` has no module prefix; native types report bare names
  ('deque' instead of 'collections.deque') because they carry no
  `__module__`.
- **asyncio support stubs:** `socket.socketpair()` is built from two
  pipes; async-generator hooks are stored but never called;
  `sys.is_finalizing()` is always False.
- **Runtime:** `gc.collect()` is a no-op (protoCore collects under heap
  pressure only); sets and dicts treat two distinct keys with equal
  hashes as the same key (accepted limitation).
