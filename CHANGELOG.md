# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-05-11

### CPython conformance sweep (test_descr.py)

Brought `test/cpython/test_descr.py` from **104 failing tests** (early in the
session) down to **73 issues** (54 failures + 19 errors out of 165 tests)
in three work sessions of intensive, root-cause-only fixes.  Every change
was paired with `ctest --test-dir build-release`; ctest stayed at
**199/199** throughout.

Across sessions the test_descr.py count improved from 104 → 73 via
~20 root-cause fixes spread across str unbound dispatch, dict()
strictness, bound-method ordering, complex hashing, type()
validation, py_object_new rules, OP_STORE_SUBSCR MRO walk, deque
trampolines, raiseIndexError, and more — every one paired with
ctest green.

### Fixed: str unbound dispatch sweep (session 3, May 2026)

A second wave of unbound built-in dunder fixes landed for `str` methods
that returned `None` / `False` when invoked as `str.method(receiver, …)`:

- `str.zfill / join / format / encode / partition / rpartition`
- `str.isalpha / isdigit / isdecimal / isnumeric / isspace / isalnum /
  isupper / islower / isprintable / isascii / isidentifier`
- `str.format` numeric indices `{0}`, `{1}`, … and auto-index `{}` are
  shifted by the receiver offset so the format-string and its
  arguments line up the same way in both calling conventions.

A second `str.format` bug was uncovered along the way: the default
conversion (empty spec, `'s'` type, `{!s}`) was rendering strings
through `reprObject`, producing `"'a'"` for `'{}'.format('a')`
instead of `"a"`.  Default conversion now mirrors CPython's
`str(obj)` for strings; `{!r}` keeps its quoting behaviour.

### Fixed: dict() construction strictness

`py_dict_call` no longer accepts arbitrary instances as if they were
empty mappings.  Three correctness fixes ride on the same commit:

- `dict(obj)` rejects with `TypeError` when `obj` lacks `keys()` and
  has neither `__iter__` nor `__getitem__` on its type's protoCore
  parent chain.  The previous fallback iterated any user instance's
  attribute SparseList and returned `{}` silently
  (`test_descr.test_dict_constructors`).
- `dict([entry1, entry2])` works when each entry's only contract is
  `__iter__` yielding two values (e.g. an `AddressBookEntry` class).
- `dict([('too', 'long', 'by 1')])` raises `ValueError` (wrong
  length) instead of dropping the extra element.

### Fixed: bound method ordering raises TypeError

`l.__add__ < l.__add__` returned `True` (raw pointer comparison)
instead of raising `TypeError`.  `compareObjects` now detects bound
built-in methods via `asMethod(ctx)` on both operands before the
user-class fallback and raises the same diagnostic the user-class
branch raises.  `==` / `!=` keep their existing identity-based
answer.

### Fixed: complex.__hash__ (value-based)

Two `complex` instances with identical components hashed
differently — the type inherited the default identity-based hash
from `object`.  Subclass tests (`class madcomplex(complex)`) failed
when comparing against the base class.  Installed a `__hash__` slot
on `complexPrototype` mirroring CPython's
`hash(c) = hash(c.real) + Py_HASH_IMAG * hash(c.imag)`; component
hashing routes through each component's `__hash__` so
`hash(complex(7, 0)) == hash(7)`.

### Fixed: type() rejects malformed __slots__

`class C(object): __slots__ = 1` and `__slots__ = [1]` silently
created classes.  CPython rejects both with `TypeError` at class
creation time.  Tightened the `py_type` slot-collision loop to
require every element of a tuple/list `__slots__` to be a string;
added a terminal else branch that rejects bare non-str/list/tuple
slot values with `__slots__ must be a str, iterable of strings,
or None`.

### Fixed: subclass operand wins in rich comparison

CPython's `PyObject_RichCompare` runs the right operand's dunder
*first* when its type is a strict subclass of the left's type AND
overrides the relevant slot.  Our dispatcher only ran the right
side as the reflected fallback, so `A() == B()` with a `B(A)` that
overrides `__eq__` silently skipped the override.  Walk the right
operand's `__mro__` for the left's type, check that the right type
*owns* the reflected dunder, run it first, fall through unchanged
on `NotImplemented`.  Doesn't yet help `str`-subclass instances
because their `type()` still reports `str` (separate bug).

### Fixed: py_object_new tighter rules for extra args

`object.__new__(cls, *args)` was accepting extras whenever
`__init__` was overridden, even when `__new__` was also overridden.
CPython rejects that combination: the override on `__new__` is the
legitimate landing site for extras, so calling `object.__new__`
directly with extras bypasses the override.  New rule: accept
extras only when `__init__` is overridden AND `__new__` is the
default `object.__new__`.  Metaclass / module subclasses get a
carve-out (their constructor protocol legitimately takes args).

### Fixed: OP_STORE_SUBSCR walks MRO for __setitem__

`L([1,2,3])[slice(1,3)] = [3,2]` raised
`RuntimeError: Object is not an integer type` for any list
subclass.  The dispatcher's raw `container->getAttribute` walked
the protoCore parent chain and returned a tagged-sentinel value
instead of the inherited native method, sending the dispatch into
the integer-key fallback that ran `key->asLong()` on the slice.
Use `env->getAttribute` for the lookup so the Python MRO is
honoured.

### Fixed: complex.__pos__ / __neg__ / __add__-shape

Installed `__pos__` and `__neg__` slots on `complexPrototype` —
both cast the result to plain complex, dropping any subclass
(`(+madcomplex(...)).__class__ is complex`).  Before this `-x`
returned `None` for any complex value because no slot existed at
all.

### Fixed: staticmethod / classmethod __bases__/__mro__ as tuples

Both prototypes had list-shaped `__bases__` and `__mro__`.  CPython
stores them as tuples on every type, and `test_builtin_bases`
asserts the shape.  Converted via `ctx->newTupleFromList`.

### Fixed: type() rejects duplicate base classes

`type('X', (A, A), {})` and `class X(A, A): pass` silently
succeeded.  Now raises `TypeError: duplicate base class A` at
class-creation time.  Does not yet catch general MRO conflicts
(`type('X', (A, B), {})` where B subclasses A — needs full C3
linearisation, separate work).

### Fixed: raiseIndexError actually marks the exception pending

The helper built the exception object via `invokePythonCallable`
but never called `setPendingException`, so callers returning
nullptr never had their IndexError surface — `deque().pop()`
silently returned None.

### Fixed: deque trampolines reject non-deque receiver

`deque.append/appendleft/pop/popleft/extend/extendleft/clear/remove`
all returned silently when `self` lacked the internal
`__deque_ptr__` external pointer.  All now raise the standard
"descriptor 'X' for 'collections.deque' objects doesn't apply to
a non-deque object" TypeError.  `pop`/`popleft` also raise
IndexError on empty deque.

### Fixed: object.__setattr__ Carlo Verre check for user metaclasses

Extended the existing built-in-type guard to also reject
`object.__setattr__(cls, ...)` when `cls` is a user class whose
metaclass overrides `__setattr__` — bypassing the metaclass's
intended validation is the exact security hole the guard is
named after.

### Fixed: int.__repr__ / __str__ re-register on the authoritative prototype

The initial intPrototype init block set `py_int_repr` as
`__repr__`, but a later step replaced `intPrototype` with
`space_->smallIntegerPrototype`, dropping the registration.  Result:
`repr(C(5))` for `class C(int): pass` walked the MRO, found no own
`__repr__` on int, and fell through to `object.__repr__` → rendered
`<C object at 0x…>`.  Re-register `__repr__` and `__str__` (both
pointing at `py_int_repr`) on the authoritative prototype.

### Fixed: complex arithmetic + abs + pow

Installed `complex.__add__ / __sub__ / __mul__ / __truediv__`,
`__abs__`, and `__pow__` on `complexPrototype`.  Without them
`complex + int`, `abs(complex)`, and `complex ** int` either
returned None or raised TypeError.  Results always cast to plain
complex (dropping any subclass), matching CPython numeric-
subclass semantics.

### Fixed: tuple/list subclass + drops subclass

`binaryAdd` for tuple+tuple and list+list checked the receiver's
`__class__` and built the result with the same class — so
`madtuple((1,)) + ()` returned `madtuple`, not plain tuple.  Walk
the receiver's `__mro__` for tuplePrototype/listPrototype and
substitute the primitive prototype before wrapping the result,
matching CPython's unoverridden-dunder semantics.

### Fixed: str.rsplit three bugs in one

`'hello world'.rsplit('o')` returned `[]`.  Three independent
issues: the result was a bare ProtoList (no Python list wrapper,
so repr / iteration saw it as empty); the trailing-prefix push
duplicated the first element when the no-match branch had
already pushed it; and `str_from_self` blocked the unbound
calling shape.  Fixed all three.

### Fixed: str.partition / rpartition empty separator → ValueError

Both methods returned None instead of raising
`ValueError: empty separator` for the empty-sep case.

### Added: str.maketrans + str.translate full implementations

`str.maketrans` was a stub returning an empty SparseList that
printed as the entire dict prototype.  `str.translate` did not
exist.  Implemented both per CPython:

  - `maketrans(x)`           — dict[int|str → str|int|None]
  - `maketrans(x, y)`         — chars + replacements
  - `maketrans(x, y, z)`      — + delete-set
  - `translate(table)`         — UTF-8-aware mapping, None drops,
                                int→char, str inline-replace.

### Earlier in the v0.3.0 cycle

### Added

- **float.__hash__**: native value-based hash matching CPython's
  `hash(1.0) == hash(1)` contract; subclasses unwrap via `__data__`.
- **float.__lt__/__le__/__gt__/__ge__/__eq__/__ne__**: comparison dunders
  on `floatPrototype` (reuse the int comparator that already accepts
  mixed numeric operands).
- **list.__imul__**: in-place multiplication on list; previously absent
  from `listPrototype`.
- **PYFLAG_HAS_CUSTOM_GETATTR**: new class-flag bit so the
  `OP_LOAD_ATTR` and `tryFastGetAttribute` fast paths bypass when a
  user `__getattribute__` override exists in the type's MRO.

### Fixed: dispatch / method resolution

- **`__str__` / `__repr__` lookup walks `__mro__`** (not the protoCore
  parent chain) so `str(instance)` for a plain user class returns
  `<C object at 0x…>` rather than the class's `<class 'C'>` spelling.
- **`runUserClassCall` honours the protoCore parent link** when the
  instance was built via `self.newChild()`, so primitive-subclass
  wrappers (e.g. `class cistr(str)`) actually invoke their `__init__`.
- **`runUserClassCall` detects subclass returns via `__mro__`** rather
  than `isInstanceOf`, fixing the `C(arg) → D` flow where `__new__`
  returns a strict subclass.
- **`compareObjects` tries the reflected dunder** when `a.__op__(b)`
  returns NotImplemented (`1 == Proxy(1)`, etc.).
- **`bool()` / `isTruthy` invoke Python user `__bool__` / `__len__`**
  via `invokePythonCallable` instead of skipping anything whose
  `asMethod` slot is null.

### Fixed: object / type construction

- **`object.__new__` rejects built-in container `cls`** (list, dict,
  tuple, set, frozenset, bytes) and any class that inherits from one
  with a layout-incompatible `__new__` substitution.
- **`object.__init__` detects `__new__ = object.__new__` re-exports**
  as non-overridden, so `class B(A): __new__ = object.__new__` still
  routes through the legacy "pass extras to `__init__`" path.
- **`object.__new__/__init__` accept ModuleType subclasses**: the C3
  MRO sometimes drops `modulePrototype`, so we walk `__bases__` to
  detect module ancestry.
- **`list.__new__` / `tuple.__new__` / `dict.__new__` reject non-subclass
  receivers** with `TypeError("X.__new__(Y): Y is not a subtype of X")`.
- **`type.__new__` rejects multiple-inheritance layout conflicts**
  (`list + dict`, `module + str`, etc.) and obvious non-type bases
  (`None`, primitives).
- **`pow(I(2), I(3), I(5))`** now dispatches through `base.__pow__`
  when `base`'s type defines an override, even for int / float
  subclasses that look like primitives via `__data__`.

### Fixed: unbound built-in dunder dispatch

A consistent fix landed for many built-in container dunders so the
unbound form `Cls.__op__(receiver, ...)` works the same as
`receiver.__op__(...)`:

- `list.__add__ / __mul__ / __eq__ / __contains__ / __iadd__ /
  __imul__ / sort`
- `tuple.__contains__ / __len__`
- `dict.__eq__ / __len__`
- `set.__contains__ / __or__ / __and__ / __sub__ / __xor__`
- `str.__contains__ / split / strip / upper`

Native primitive descriptors (`str.upper`, `str.split`, `str.strip`,
`list.sort`) now raise CPython-style
`"descriptor 'X' for 'Y' objects doesn't apply to a non-Y object"`
when passed an instance of the wrong type.

### Fixed: argument validation

- **`dict()` validates argument shapes**: rejects non-iterables with
  TypeError, non-2-element iterable items with ValueError ("dictionary
  update sequence element has wrong length"), and more than one
  positional argument with TypeError.
- **`dict(mapping)` gates the native fast path on dictPrototype-derived
  type**: every Python instance owns `__data__` / `__keys__` for its
  attribute storage, so the unrestricted check accepted any random
  object as a dict.
- **`complex()` / `str()` reject unknown keyword arguments**.
- **`del d[0]`** on a plain instance raises TypeError ("'D' object
  doesn't support item deletion") instead of silently no-oping.

### Fixed: error semantics

- **`**=` TypeError mentions `**=`** (not `**`) in the operand-type
  message.
- **`str %` formatting** correctly unwraps int / float subclass values
  via `__data__` for `%d`, `%g`, `%f`, etc.
- **`%(key)s % None`** raises TypeError ("format requires a mapping")
  instead of silently inserting `'None'`.
- **Recursive `__str__` ↔ `__repr__`** raises RecursionError at a
  proper threshold instead of returning the placeholder `'...'`.
- **`'in' operator`** falls back to iteration via `__getitem__` for
  classic-sequence types that only expose item access; gates the
  native fast path on dict / module so random user instances don't
  short-circuit through their attribute SparseList.

### Architectural notes

- `getAttribute` / dunder lookup now consistently distinguishes the
  protoCore parent chain (used internally for type identification)
  from `__mro__`-driven Python attribute resolution.  The split was
  the root cause of the `<class 'C'>` mis-rendering and the
  `bool` / `__contains__` / `pow` Python-user dispatch misses.
- Every commit in the sweep was paired with `ctest --test-dir
  build-release`; no regressions were introduced.

## [0.2.3] - 2026-02-23

### Fixed

- **Bindings**: Renamed native functools module to `_functools` to allow standard library script loading.
- **Engine**: Fixed `PROTO_NONE` handling during dictionary lookups in `executeBytecodeRange`.
- **Compiler**: Verified `co_consts` and `co_code` as tuples (not lists) during code object execution.
- **Tests**: Introduced `invokeCallable` helper in C++ tests to properly invoke the `__call__` method when testing function objects.
- **Threading**: Exposed `RLock` alias mapping to `allocate_rlock`.
- **Build**: Added `-fno-delete-null-pointer-checks` compilation flag.

## [0.2.2] - 2026-02-18

### Fixed

- **Attribute Lookup**: Resolved `AttributeError: 'type' object has no attribute 'add'` by correcting internal method mapping for `set.add` (mapped to `add` instead of `__add__`).
- **Object Identity**: Standardized `__class__` attribute assignment across all core builtin constructors (`set`, `list`, `dict`, `tuple`, `bytes`, `object`). This ensures instances correctly identify as their respective types rather than inheriting `type` from the prototype.
- **Compiler Reliability**: Standardized the `makeCodeObject` internal API across `Compiler.cpp` and `PythonEnvironment.cpp` to ensure consistent metadata (line numbers, flags) for all generated code objects.
- **Diagnostics**: Cleaned up internal debug prints and enabled smoother `abc` module imports by resolving `GenericAlias` interaction bugs.
- **Exceptions**: Fixed `py_tuple_call` to correctly propagate non-StopIteration exceptions (e.g., `TypeError`) instead of suppressing them, which was masking critical errors.
- **Attribute Lookup**: Fixed a critical recursion bug in `PythonEnvironment::getAttribute` where `getAttrDepth` was not decremented on failed lookups, leading to false "recursion limit reached" errors and unbound method failures in `namedtuple` construction.

## [0.2.1] - 2026-02-17

### Fixed

- **Garbage Collection (GC) Safety**: Massive refactoring of the `ExecutionEngine` to ensure all `ProtoObject*` operands and intermediate results are rooted on the execution stack during bytecode execution. This prevents premature object reclamation during complex operations.
- **Opcode Stability**: Refactored major opcode families for GC safety:
  - Arithmetic and Bitwise (Binary and In-place).
  - Container mutations (`LIST_APPEND`, `SET_ADD`, `MAP_ADD`, `DICT_UPDATE`, `LIST_EXTEND`).
  - Attribute and Subscript access (`LOAD_ATTR`, `STORE_ATTR`, `BINARY_SUBSCR`, `STORE_SUBSCR`, `DELETE_SUBSCR`).
  - Iterator and Generator control flow (`GET_ITER`, `YIELD_FROM`, etc.).
  - Function calls (`CALL_FUNCTION`, `CALL_FUNCTION_KW`, `CALL_FUNCTION_EX`).
- **Critical Bug**: Fixed a stack indexing bug in `OP_MAP_ADD` that could lead to stack corruption or incorrect attribute mapping.
- **Stability**: Resolved `std::length_error` crashes caused by unsanitized stack manipulations and missing GC roots.
- **Python Parity**: Aligned `STORE_ATTR` and `STORE_SUBSCR` stack order with CPython 3.14 standards.
- **Test Alignment**: Updated unit tests in `TestExecutionEngine.cpp` to match Python-compliant stack rotation and attribute storage patterns.

### Changed

- **Performance Optimization**: Implemented internal dunder string caching in `PythonEnvironment` using `getInternalString` to accelerate attribute and method lookups.
- Improved stack management strategy to favor in-place modification over frequent pop/push cycles, enhancing performance and safety.

## [0.2.0] - 2026-02-14

### Added

- **Professional Documentation**: Comprehensive User Guide, C++ API Reference, Internals Deep Dive, and Python Compatibility Guide.
- **Example Suite**: New examples for generators, async/await, multithreading, and C++ embedding.
- **Improved Test Coverage**: Added dedicated tests for native generators, async coroutines, and parallel threading.
- **Generator Metadata**: Added `co_name` to code objects for better introspection and naming in tracebacks.
- **Enhanced Types**: Standardized `__repr__` and `__str__` lookups in `PythonEnvironment`.

### Fixed

- **Execution Engine Regressions**: Resolved critical instruction stepping issues that caused bytecode misalignment.
- **Stack Integrity**: Implemented `GCStack` fallback for unit tests to prevent stack overflows.
- **Opcode Logic**: Corrected stack order in `STORE_ATTR` and fixed `GET_ITER` return behavior to match expected norms.
- **Build and Syntax**: Fixed various compilation errors and missing closing braces in `ExecutionEngine.cpp`.

### Changed

- Improved `type(None)` to correctly return `NoneType`.
- Optimized dunder lookups for both built-in and user-defined types.

## [0.1.0] - 2026-02-10

### Added

- Initial release of protoPython.
- Basic bytecode interpreter for Python 3.14.
- GIL-free concurrency model based on protoCore.
- Support for core built-in types and functions.
- C++ interop bridge using HPy.
