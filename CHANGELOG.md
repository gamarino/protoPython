# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.0] - 2026-10-22

**Requires protoCore 3.0.0** (SOVERSION 4), the declared minimum for a build from source; the `.deb` depends on `protocore (>= 3.0.0)`. CI pins protoCore tag `v3.0.0` on every job.

### Changed — protoCore's unified exception model (protoCore 3.0.0, 2026-10-21)

- **A Python exception is protoCore's `proto::ProtoException`.**
  `protoPython::PyException` derives from it and adds nothing (its `TransientPin` is
  gone: protoCore pins the value in flight). Every handler (interpreter, code protopyc
  generates, builtins, HPy calls, threads, `protopy`, the REPL) catches
  `const proto::ProtoException&` and reads `value()`; embedders and native modules
  that caught `PyException` and called `exception()` must do the same.
- **`except` selects by prototype**: a foreign type (a C++ module's error prototype,
  another language's class) matches by the value's prototype chain; a foreign value is
  caught by `except Exception`, `except BaseException` and a bare `except:`; `str()`,
  `repr()`, `type()`, `sys.exc_info()`, chaining and the top-level report handle it.
  `except T` with `T` a Python object that is not an exception class now raises
  CPython's `TypeError` (it silently did not match before).
- **protoCore's errors arrive as Python exceptions**, built by protoPython's error
  factory: `DivisionByZero` -> `ZeroDivisionError`, `Overflow` -> `OverflowError`,
  `InvalidArgument` -> `ValueError`, `TypeMismatch` / `ArityMismatch` -> `TypeError`.
  The big-integer fallbacks that caught `std::overflow_error` test for protoCore's
  `Overflow` error instead (`tryAsLong`, `rethrowUnlessOverflow`). Without this the
  HEAD of this branch aborted in 20 tests against protoCore 3.0.0.
- A C++ standard exception from native code is `RuntimeError(what())` (the prefix
  "internal C++ exception: " is gone) at the interpreter's instruction boundary, in
  `invokePythonCallable` (compiled code) and in the HPy API; `protopy` reports one that
  escapes every frame instead of terminating.
- An exception leaving a `_thread.start_new_thread` function is reported through
  `sys.unraisablehook`, as CPython (it was dropped silently).
- **`import example.geometry` loads a UMD module** registered under the logical path
  `example/geometry` (any provider in the space's resolution chain): a Python module
  over its attributes, methods bound to the module.
- Conformance rule 14: `catchForeignError` and `escapeLanguageError` are implemented;
  the four `exception.*` cases pass (two were NotApplicable).
- protopyc: `import a.b as c` binds the submodule `a.b` (it bound the package `a`);
  float literals keep every digit (`12.566370614359172` compiled as `12.5664`).
- **Rule E3: an exception no handler selects leaves unchanged.** protopyc's unmatched
  except chains, finally clauses, `with` statements and bare `raise` rethrew a new
  `PyException` with the caught value, slicing off another runtime's
  `ProtoException` subclass; they now rethrow the frame-held `std::exception_ptr`.
  The interpreter's bare `raise`, the `mro()` rollback path and the HPy error
  round-trip do the same. `raise e` stays a new raise of the same value.
  New tests `exception_identity` and `protopyc_exception_identity`.
- Tests: `umd_geometry_errors`, `umd_geometry_unhandled` and `protopyc_umd_geometry`
  run `test/regression/umd_geometry_*.py` through `protopy_umd_host` (`test/umd/`),
  which links protoCore's example geometry module and registers its provider.

### Changed — exceptions, follow-up (2026-10-21)

- The pending-exception API is removed (`hasPendingException`, `peek`/`take`/
  `clearPendingException`, `setPendingException`, the per-thread slot and flag);
  `raiseException` throws, `executeString` lets an unhandled exception propagate.
- The exception being handled (`sys.exc_info()`, bare `raise`, `__context__`)
  lives in the context of the activation that handles it
  (`PythonEnvironment::kActivationSlots`, `HandledExceptionScope`), not in a
  per-thread stack.
- `collections.abc`: CPython's mixin methods (copied from Lib/_collections_abc.py,
  loaded one ABC family at a time on first use), the real ABC hierarchy and
  abstract instantiation; a Python metaclass receives the class namespace as a dict.
- `zip`, `enumerate`, `filter` drive Python `__next__` and propagate its errors;
  `Match.group` takes several groups (configparser works); `getattr` with a
  default no longer raises internally.
- A raise reuses one traceback frame per activation and stores only `args` on
  the instance: raise + catch 108,671 -> 83,539 cycles.
- Final measurement (benchmarks/reports/2026-10-21-cpp-exceptions-final/):
  geomean vs CPython 3.14t 2.82x -> 2.52x (protopy), 1.92x -> 1.91x (protopyc).
- Partitioning the dispatch function into per-opcode functions was tried and
  reverted: no change to the unwinding cost, 1-6 % more instructions on the
  normal path.

### Changed — Python exceptions are C++ exceptions (2026-10-21)

- **A Python exception is a C++ exception** (`protoPython::PyException`,
  PythonEnvironment.h, which keeps the exception object rooted while it
  travels) and **a Python `try` is a C++ `try`**: each protected range
  (SETUP_FINALLY, SETUP_WITH, SETUP_ASYNC_WITH) runs as a nested dispatch of
  the activation inside a C++ try, its handler inside the catch; RERAISE is
  `throw;`. The pending-exception flag is no longer polled on every opcode,
  and `raise*` / `setPendingException` throw. Natives, the module loader,
  the REPL, thread entry, atexit, the embedding API (`executeString`) and
  protopyc's generated code use try / catch; every site that inspected,
  swallowed or converted a pending exception catches instead.
- **Generators** record their try nesting (protected ranges, and handlers
  with the exception they handle) only to rebuild it on resume; `gen.throw`
  raises at the resume point. An interpreted generator that finishes answers
  iteration with nullptr instead of building and throwing a StopIteration
  (`py_generator_step`, used by `PythonEnvironment::next`).
- **HPy** keeps C frames free of C++ exceptions: every `HPy_*` call that can
  run Python code catches the exception, stores it in the context and
  returns the API's error value (`HPy_NULL`, -1, `(size_t)-1`);
  `HPyErr_SetString` / `HPyErr_Occurred` / `HPyErr_Clear` work on it, and the
  method wrapper and the module loader rethrow it when the extension returns.
- **Several environments in one process** no longer share state: native
  module initialisers resolve the importing thread's environment instead of
  capturing their own, a later environment's module providers carry a suffix
  so protoCore's process-wide module cache keeps its modules apart, its
  resolve-cache generations never overlap an earlier one's, and an
  environment's destructor resets the thread's mirrors of its state. A
  second environment received the first one's `_collections_abc`, whose
  classes were built on the first environment's `object` and `type`.
- **Bugs the swallowed errors hid**, now fixed: `map` stopped at a Python
  `__next__` (it required a native one); `min` / `max` drove their iterable
  with builtin `next`; `int.from_bytes` accepted only bytes (ipaddress, and
  so urllib.parse, failed to import); `math.fsum` was missing; `int` had no
  `numerator`, `denominator` or `as_integer_ratio`; `ABCMeta.__subclasscheck__`
  consulted the registries of the ABC's bases instead of its own and its
  subclasses' (`isinstance(1, numbers.Rational)` was False); a non-empty
  `__slots__` on a str subclass was rejected (configparser).
- The interpreter's activation is a struct whose try levels are member
  functions; closures capturing it by reference cost 270 instructions per
  call.
- New regression test `exception_nesting` (CPython oracle). The GoogleTest
  suite's checks of pending exceptions use exceptions; 13 tests that passed
  only because an error was silently ignored now test valid inputs.
- Measured (benchmarks/reports/2026-10-21-cpp-exceptions/): suite geomean
  against CPython 3.14t 2.82x -> 2.53x (protopy), 1.92x -> 1.90x (protopyc);
  `pyperf_binary_trees` 14.21x -> 6.81x; a raise and catch 192,657 -> 108,671
  cycles (CPython 1,052); the empty loop 139 -> 116 cycles an iteration
  (CPython 175); `pyperf_fib` +4.5 % instructions, the same CPython ratio.
- Known gaps, unchanged: a raise costs about 100 times CPython's (the C++
  floor is 5,300 cycles; unwinding the 118 KB dispatch function and building
  the exception object are the rest); the active-exception stack behind
  `sys.exc_info()` is still per thread; `zip`, `reversed` and a few
  collections.abc mixins still swallow every exception where CPython only
  converts StopIteration; `configparser.read_string` fails further in.

### Changed — instantiation and the exception object (2026-10-21)

- **Instantiation by protoCore**: `__new__` and `__init__` are annotations of
  the class, and one lookup of the class's chain returns the one that
  applies. When `__new__` is object's, the instance is the class's
  `newChild`; object.__new__'s checks are answered by the same lookups (a
  container base owns its own `__new__`; extra arguments with object's
  `__init__` take the full path, which raises). The full path ran an MRO
  walk for the extra-arguments check and another for the container check,
  and looked `__new__` and `__init__` up through the whole Python
  attribute protocol, on every instantiation.
- **The instance `__dict__` storage is created on demand** (by `__dict__` /
  `vars()`), not by `object.__new__` on every instance: two attribute writes
  and two collections per instantiation.
- **The exception object and each traceback link are built with one write
  group** (`setAttributes`) instead of five attribute writes each, with
  their names interned once.
- New regression test `instantiation` (CPython oracle).
- Measured, `perf stat -r 3` cycles: an instance of a plain class 22,345 ->
  12,818 per instantiation; `ValueError("x")` 29,178 -> 20,168; a raise and
  catch 192,657 -> 91,826; `pyperf_binary_trees` 2.04 G -> 0.83 G cycles
  (-59 %, same result). Known divergence, unchanged: a metaclass's
  `__call__` is not invoked when its class is called.

### Changed — attribute reads and writes by one protoCore lookup (2026-10-20)

- **`LOAD_ATTR` resolves the common read with one `getAttribute` of the
  whole chain** (protoCore's per-thread attribute cache answers repeats).
  A class keeps its annotations in that chain -- a property, a
  classmethod, a descriptor, a function that binds -- so the lookup
  returns the annotation in place of a value and the kind of the result
  decides: a tagged int or string, a bool, None, or an object whose type
  has no `__get__` is the value; a Python function binds to an instance of
  its class; anything else takes the previous paths unchanged. Whether
  the MRO overrides `__getattribute__` comes from the type's class flags.
- **`super` owns `__getattribute__`**, as in CPython (it was object's), so
  its proxies carry the custom-getattribute class flag;
  `tryFastGetAttribute` no longer probes `__py_getattr_handler__` on every
  call, and answers tagged values without a type probe.
- **`STORE_ATTR`**: the setter check is one lookup of the type's chain
  (absent, None or a tagged value is no descriptor: a class-level default
  no longer sends every instance write to the slow path); `__class__` /
  `__dict__` compare by pointer (names are interned) instead of hashing
  both strings on every write; the receiver test is a tag test.
- `fastClassFlags` decodes the SmallInt flags inline.
- New regression test `attribute_access` (CPython oracle).
- Interleaved A/B, three rounds, same protoCore, cycles: inherited class
  attribute read -69 %, None field read -55 %, store with a class default
  -28 %, own store -19 %, `attr_lookup` -19 %, own field read -15 % (and
  -14 % five levels deep), `richards` -6 %, method call -4 %, `fib` -4 %;
  `binary_trees`, `sieve`, `list_append` unchanged (+0.5 / +1.2 / +0.3 %,
  within A/A).
- Known divergences from CPython, unchanged by this work: a data
  descriptor does not win over a value written straight into `__dict__`;
  reassigning `__class__` does not change method resolution; methods of a
  `super` subclass are not found.

### Changed — protoCore 3.0.0: the descriptor calling convention (2026-10-20)

- **Built against protoCore 3.0.0** (SOVERSION 4), now the declared minimum:
  every native receives protoCore's `ProtoArguments` / `ProtoKeywordArguments`
  descriptors (protoCore `docs/CALLING_CONVENTION.md`), as do the argument
  helpers that read them.  CI pins protoCore `cf52f9f5` on every job, the
  floor job included; the DEB / RPM dependency is `>= 3.0.0, << 4.0.0`.
- The engine keeps lists internally: `invokePythonCallable`,
  `invokeCallable`, `bindArguments` and protopyc's generated bodies take a
  `ProtoList` / `ProtoSparseList`; overloads and the generated entry points
  convert a native's descriptors with `asList` / `asSparseList`, which
  answers the call's own list when it came with one.  `argsView(...)` gives a
  descriptor over a temporary for one full expression, for the ~230 direct
  calls of a native with a list.
- **`callObjectEx` builds no keyword structures for a call without
  keywords** (map's callback, for instance): it built an empty
  `ProtoSparseList` and an empty name list on every call.
- Interleaved A/B against main on protoCore 2.14.8, three rounds with A/A,
  cycles: `attr_lookup` -7.1 %, `nqueens` -6.1 %, `sieve` -5.4 %, `richards`
  -5.2 %, `binary_trees` -5.0 %, `list_append` -4.4 %, a map/lambda probe
  -3.9 % (before the `callObjectEx` change), `range_iterate` +3.0 %
  (instructions unchanged).

Changes on `main` since the 0.3.0 entry, from commit `038eb6ea` (2026-05-12)
onwards. Commit hashes are given for reference.

> **Version numbering.** `CMakeLists.txt` declares version 1.0.0. The version
> was raised from 0.2.0 to 1.0.0 in `fd29013c` (2026-04-20), the commit tagged
> `v1.0.0`; the only other tag is `v0.2.0` (2026-02-13). This file has no
> `[1.0.0]` entry. The `[0.3.0]` entry below was added on 2026-05-10
> (`9fa74ad1`), after the `v1.0.0` tag, and has no matching tag or CMake
> version.

### Changed — tuples are collectable (2026-10-12)

- **A Python tuple is an immutable object over a `ProtoList`, not a protoCore
  `ProtoTuple`** (maintainer decision: "a collectable tuple is a ProtoList").
  Every `ProtoTuple` goes through protoCore's tuple interner, whose table is
  keyed by the element pointers, is a GC root and never drops an entry, so a
  tuple of new objects (`*args`, a display `(a, b)`, `dict.items()` pairs,
  `zip`, `enumerate`, `divmod`, `str.partition`, slicing, `+`, `*`) kept them
  for the life of the process: about 10 cells per call. A loop of such
  operations now runs in a fixed 1 M-cell heap (100,000 iterations; 30,000 ran
  out of memory before).
  - `PythonEnvironment::makeTuple` builds every tuple (an immutable child of
    `tuple`, or a mutable instance of a tuple subclass, with the items in
    `__data__`); the empty tuple is one perennial object (`() is ()`, which was
    False). `tuple(t)` returns `t`; `tuple(some_list)` shares the list's
    `ProtoList` (immutable) instead of copying it.
  - `PythonEnvironment::tupleView` reads the items of a tuple in either form; the
    readers of the interpreter, the built-ins and the native modules use it
    (they called `asTuple()`, which a `ProtoList`-backed tuple does not answer).
    `getType`, `isinstance`, `==`, ordering and `+` tell a tuple from a list,
    whose storage now has the same shape: `[1, 2] + (3,)` and `(1,) < [1]`
    raise TypeError as in CPython (`list + tuple` concatenated before).
  - Unchanged: tuple hashes (`tuple_hash_values.py` checks the values of the
    `ProtoTuple`-based runtime), equality, ordering and repr. Tuples built once
    by the compiler or at start-up (code-object constants, `__mro__`,
    `__bases__`, keyword-name tuples of interned strings) remain `ProtoTuple`s.
  - protopyc emits `makeTuple` for tuple displays, star-call arguments and class
    bases.
  - *Measured* (Release, interleaved, median of 5-7 process runs, wall-clock;
    hardware counters are blocked on this machine): a tuple-heavy loop (star
    calls, displays, unpacking, `dict.items()`, tuple keys) 17.9 s -> 15.9 s
    (-11 %); binary_trees -9 %; nqueens -6 %; call_recursion -3 %; fib +1 %;
    attr_lookup +1.9 % and richards_lite +1.9 % (a first version was +6 %;
    `tupleView` now costs one call for a `ProtoTuple` and one own-attribute probe
    for a non-tuple). Rule 5 (`gc.transient_reclaimed`): the tuple workload grows
    the heap by 652,821 cells instead of 1,709,634.
  - *Tests*: `protopy_tuple_semantics` (CPython's output: construction, `() is
    ()`, hashing, equality, ordering, dict keys, sets, slicing, unpacking,
    `*args`, namedtuple, pickle, subclasses), `protopy_tuple_bounded_memory`,
    `protopy_tuple_hash_values`.

### Fixed — standard-library and runtime bugs from the Windows review (2026-10-11)

Each item has a regression test whose expected output is CPython 3.14's
(`test/regression/<name>.py` and `.expected`, registered through
`PROTOPY_STDLIB_ORACLE_TESTS` in `CMakeLists.txt`); every test fails on the
binary from before its fix.

- **`OrderedDict(generator)` and `dict.update(iterator)`** (`991d4d10`).
  dict.update probed `__keys__` / `__data__` on its argument through the
  parent chain; every class keeps its namespace there, so a generator, a zip
  or a `MappingProxyType` copied the attributes of `object`. The store is
  copied directly only for a dict; `update(None)` / `update(5)` raise
  TypeError. `dict.fromkeys` returned an immutable object (assignments were
  lost) and stopped at a `None` key (`38d3d26c`).
  Test: `protopy_ordered_dict_iterables`.
- **`collections.Counter` segfault** (`797ccd00`).
  `_collections._count_elements` was a placeholder bound to no receiver that
  called `self->newChild()`: `Counter("abc")` dereferenced a null `self`. It is
  gone (collections uses its Python `_count_elements`); the deque iterators'
  `__iter__` returns the iterator. Test: `protopy_counter_methods`.
- **`collections.abc` virtual subclasses** (`0294f7f2`). The native ABCs had a
  no-op `register()`, so `isinstance({}, Mapping)`, `isinstance([], Sequence)`
  and `isinstance([], Iterable)` were False and Counter counted the keys of
  mappings and `**kwargs`. Each ABC now has a registry with CPython's
  built-in registrations, the ABC hierarchy, and the one-method subclass
  hooks; issubclass / isinstance consult it after the MRO check fails.
  Test: `protopy_abc_virtual_subclasses`.
- **`itertools`** (`dfcf1d16`). The native module skipped generators and
  Python iterators, stopped chain / islice / cycle at the first `None`, built
  `chain.from_iterable` eagerly (endless on an infinite input), and returned
  empty iterators for groupby, permutations, combinations and
  combinations_with_replacement. `lib/python3.14/itertools.py` replaces it,
  after CPython's documented equivalents. **Slower** than the old native code
  where that code was correct (200,000 items: islice(count()) 178 -> 1,645 ms,
  chain 86 -> 2,183 ms, repeat 79 -> 636 ms); a native accelerator for
  count / repeat / islice / chain / cycle is the follow-up. `tuple("ab")` held
  raw code-point cells instead of str. Test: `protopy_itertools_functions`.
- **Recursive `yield from` cost** (`1a4c454b`). A generator resume rebuilt a
  local-names list and copied every slot of its context (operand-stack area
  included, ~135 slots for a small function) into a new list: ~70 us per
  resume, of which ~3 us ran bytecode. It now sizes the context like a call
  and saves only the variable slots. `for x in gen(4000)`: 69 -> 2.65 us per
  item; `sum(walk(200))` with recursive `yield from`: 2,947 -> 104 ms (CPython
  0.37 ms). `next()` finds `__next__` with the cached protoCore lookup first:
  4.19 s -> 1.43 s for 200,000 calls (`e8545204`).
  Test: `protopy_generator_resume_cost` (clock-dependent, informational in CI).
- **argparse `nargs="*"` / `"?"`** (`94c9ca06`). A regex search starts before
  the start-of-subject marker so `^` can take it; a pattern that can match
  empty (`(-*[A-]*)`, `(-*A?-*)`, which argparse builds) matched `''` on the
  marker and never saw the arguments. Also in `re`: `start` / `end` / `span`
  take a group, named groups work for `re.match(p, s)`, and `re.Match` /
  `re.Pattern` are classes with CPython's reprs. `lib/python3.14/argparse.py`
  is CPython's again (a local edit broke `prefix_chars` with several
  characters); `textwrap` falls back to whitespace splitting where `std::regex`
  lacks lookbehind, so `print_help()` no longer raises (`def587d0`).
  Tests: `protopy_regex_empty_matches`, `protopy_argparse_nargs`,
  `protopy_argparse_help`.
- **`hashlib`** (`4c0cef92`). There were no `_md5` / `_sha1` / `_sha2` /
  `_sha3` / `_blake2` modules: `import hashlib` raised. A native core
  (`_hashcore`) implements MD5, SHA-1, SHA-2, SHA-3, SHAKE and BLAKE2 (with its
  full parameter block), keeping a digest's state as an immutable bytes value;
  the hash objects are Python (`_hashobject.py`). `_operator._compare_digest`
  added for hmac. Bytes fixes it needed (`c475122a`): `bytes(generator)`
  raised StopIteration, `bytearray(iterable)` was empty, `bytes.translate`
  ignored a bytes table, and `ljust` / `rjust` / `center` / `isalnum` /
  `isspace` / `islower` / `isupper` / `istitle` / `capitalize` were missing.
  Tests: `protopy_hashlib_digests`, `protopy_bytes_construct_methods`.

Found and not fixed: `iter(object())` returns a dict key iterator;
`type(bytearray())` is bytes; `type(len)` has no `__call__`; class reprs omit
the module (`<class 'HelpFormatter'>`); `bytes.expandtabs` / `rsplit` are
missing; std::regex has no lookbehind; with libc++ (macOS), `re.findall(r"\b\w*", "ab cd")` misses the
final empty match; `_collections_abc.py` cannot replace the
native stand-in yet (`range(1 << 1000)` and class-namespace `get` fail).

### Changed — closures hold cells, not frames (2026-10-11)

- **A nested function closes over cells, never over the frame that defined it,
  in the interpreter and in protopyc** (`docs/INTERNALS_DEEP_DIVE.md`,
  "Closures"). A nested function held its defining frame (a mutable object) and
  that frame held the function as a local: one cycle between mutable objects per
  call of the defining function, which protoCore never collects (protoCore
  `docs/MemoryModel.md`, section 7). A loop calling a function that defines a
  recursive inner function reached 0.53 GB peak RSS after 20,000 calls and
  3.0 GB after 80,000 under the adaptive controller, and ran out of any fixed
  heap; it now stays at 0.15 GB. Free variables were found by walking frames
  by name at every read.
  - *Interpreter.* A variable a nested scope uses lives in a cell
    (`PythonEnvironment::newCell`, `cellGet`, `cellSet`, `cellClear`), kept in
    the variable's fast slot, so the function stays `CO_OPTIMIZED`; free
    variables take the slots after `co_varnames` (`co_freevars`,
    `co_cellvars`, `co_cellslots`, `co_nlocalsplus`). `LOAD_DEREF`,
    `STORE_DEREF` and the new `DELETE_DEREF` (217) take a slot; `LOAD_CLOSURE`
    (218) pushes a cell; `BUILD_FUNCTION` takes the closure's cells
    (`arg >> 8` of them) from below the code object; class bodies read free
    variables namespace-first with `LOAD_CLASSDEREF` (219). The function keeps
    its cells in a `ProtoList` (`__closure_cells__`): an interned `ProtoTuple`
    is perennial (protoCore embedder rule 5). The compiler recompiles a scope
    with exactly the cells its nested scopes ask for; the four comprehension
    compilers are one (`compileComprehensionScope`).
  - *Recursive nested functions.* A `def` that is the only binding of its name
    in its scope (no decorator, not in a loop) binds its own name to itself at
    entry instead of closing over the cell holding it, which would close
    `cell -> function -> closure -> cell`. `__closure__` still shows a cell for
    the name. Mutual recursion, a decorated recursive function and a class's
    `__class__` cell remain one cycle per definition, by what the program means.
  - *Classes.* The methods' `__class__` is the class body's own cell, left in
    the namespace as `__classcell__` and filled by `type.__new__` before
    `__init_subclass__` runs (`PythonEnvironment::applyClassCell`), as in
    CPython; zero-argument `super()` reads it (compiled and at run time). The
    class-name and `".__class__"` namespace bindings the frame walk needed are
    gone.
  - *Introspection.* `frame.f_locals`, `locals()` (now a snapshot dict in a
    function, as CPython 3.13 returns; it returned the frame object), `eval()`
    and `exec()` see cell and free variables; empty cells raise CPython's
    `NameError` / `UnboundLocalError` messages.
  - *protopyc.* A symbol table (`CppGenerator::buildScopes`) resolves every
    name of the module, defs, lambdas and class bodies; free variables are
    cells in the function storage (they were snapshots of the values at the
    definition: no late binding, `nonlocal` and `del` lost, a recursive nested
    function read its own name as `None`). A recursive nested def calls its body
    directly and captures its name only when it uses it as a value.
  - *Measured* (Release, interleaved, medians of 5; interpreter): a recursive
    inner `fib(25)` 1.007 s -> 0.070 s; a `nonlocal` counter, 300,000 calls,
    6.2 s -> 0.58 s; reading a free variable 3.5 s -> 0.84 s; a comprehension
    capturing a local 9.3 s -> 2.8 s; creating and calling a closure
    10.8 s -> 7.3 s; `fib(27)` unchanged (0.20 s). In instructions (perf stat),
    a loop calling a closure: 30,108 M -> 788 M; compiled, the same loop
    721 M -> 763 M (a cell read where a snapshot was read).
  - *Tests*: `protopy_closure_semantics` and `_gc` (CPython's output on 50
    closure cases, also under a 1.5 M-cell limit with a 200-cell context
    threshold; 86 lines differed before), `protopy_nested_def_bounded_memory`
    and `protopyc_nested_def_memory` (out of memory under a fixed 2.5 M-cell
    heap before), `MutableCycles.NamedNestedDefsLeaveNoCycle` (320 -> 1,120
    handles in cycles before), `protopyc_closures` and
    `protopyc_emitted_cpp_closures`.

- **Calls bind arguments as CPython does, in both engines**
  (`PythonEnvironment::bindArgumentsCore`): positional and keyword arguments,
  defaults, keyword-only and positional-only parameters (`co_posonlyargcount`,
  stamped with `co_argcount`), `*args`, `**kwargs` in call order, and CPython's
  `TypeError`s (missing, too many, unexpected keyword, multiple values,
  positional-only passed as keyword). The interpreter raised none of them (a
  missing argument read as `None`, an extra one was dropped); compiled code
  bound positional arguments only and evaluated defaults again at every call
  (they are now stored with the function at its definition). Two native callers
  passed a dunder the wrong arguments, hidden until now: `len()` passed the
  receiver again to a bound `__len__`, and the truth test called an unbound
  `__bool__` / `__len__` without it. Test: `protopyc_closures` (binding cases,
  interpreter and compiled).

### Changed — CI on protoCore 2.14.5 (2026-10-11)

- **CI builds protoCore 2.14.5** (tag `v2.14.5`, commit `063c6d55`) on Linux,
  macOS and the first Windows job instead of 2.14.3: the controller's
  out-of-memory rule uses the live set, the collector's registries outlive
  static destruction and protoCore's clock-dependent cases skip themselves in
  this add_subdirectory configuration (2.14.4); an embedder exits
  LeakSanitizer-clean (2.14.5). The Windows floor job stays on 2.11.0. No
  source change was needed: 857/857 locally in Release, and under
  AddressSanitizer with leak detection on, no report (855/857; the two
  memory-measuring tests need `quarantine_size_mb=1`).

### Added

- **Attribute write groups: a run of `self.a = ...; self.b = ...` is published
  as one version of the object.** Each attribute write on an ordinary instance
  publishes a new version into protoCore's mutable table (a new snapshot plus
  a path copy in the table). The compiler now turns a run of two to 16
  statements `p.name = value` on the same parameter `p` whose values after
  the first are constants, negated numeric constants, parameters, or list,
  tuple and dict displays of them (`Compiler::tryCompileAttrGroup`) into
  `OP_STORE_ATTR_GROUP` ... `OP_STORE_ATTR_GROUP_END` (215, 216); the engine
  publishes the run once with `ProtoObject::setAttributes` (protoCore 2.11.0).
  The program's meaning does not change; another thread sees the whole run or
  none of it. A call, an attribute read, a global, arithmetic or any other
  expression ends a run, and `__class__` / `__dict__` are never grouped.
  - *Guards* (once, at the run's first write): the receiver is an ordinary
    instance (not a class, a module or a tagged value), its type has no
    `__slots__` and owns no attribute of a written name, no class on the chain
    defines `__setattr__` or holds a data descriptor of a written name, and the
    parameters the run reads are bound (`del p` leaves them unbound).
    Otherwise every write is `OP_STORE_ATTR`'s own. While a run holds
    unpublished writes the loop delivers no Python signal handler (CPython
    runs them only at calls and backward jumps). If a thread is terminated in
    the middle of a run, which of its writes survive is indeterminate.
    `PROTOPY_ATTR_GROUPS=off` turns runs off. protopyc-compiled modules are
    not affected (they do not use this bytecode).
  - *Measured* (a five-attribute `__init__`, 50,000 constructions, no
    collection during the measurement; Release, protoCore 2.12.0): 119 to 142
    cells per construction per write, 83 to 89 grouped. A protoCore thread
    taking snapshots of an object while `self.a = k; self.b = k; self.c = k`
    runs 100,000 times saw 11.8 million torn snapshots of 19.9 million per
    write and none of 9.8 million grouped. In `lib/python3.14`, 4,893 of the
    20,773 attribute-assignment statements fall in 1,630 such runs (an AST
    count with the compiler's rules).
  - *Tests*: `protopy_attr_write_groups` and `protopy_attr_write_groups_off`
    run `test/regression/attr_write_groups.py` (every value is CPython's) with
    the groups on and off: own and inherited `__setattr__` (one observing the
    earlier writes), own and inherited properties and descriptors,
    `__slots__`, class and module receivers, a deleted parameter; with the
    guard removed it fails. `test_attr_write_groups` measures the cells and
    the snapshot reader.

- **Windows (MSVC):** protopy builds, passes its test suite (see
  `docs/INSTALLATION.md` for the current count) and
  runs scripts, `-c` programs and the REPL natively on Windows 11 with Visual
  Studio 2022, against an installed protoCore (branch `windows-port`:
  `73ea74b3`, `6816cd04`, `02dba2f5`, `36598a2d`). `os` is `nt` with `ntpath`, as in
  CPython; the POSIX calls of the native modules go through
  `src/library/PosixCompat.h` (UTF-16 paths, binary files, `os.stat` with file
  index and nanoseconds), `select` polls pipes, `re` matches UTF-32, asyncio uses
  the selector event loop, and the executables carry a UTF-8 manifest and a
  32 MiB stack. protoCore's 64-bit integers are spelled
  `proto::proto_long`/`proto_ulong` throughout (the same types as before on Linux
  and macOS). Not yet on Windows: subprocesses, `winreg`/`msvcrt`, and
  protopyc's module build. See `docs/INSTALLATION.md`, "Windows (MSVC)".
- **Language:** PEP 695 type parameters on classes and functions, and `type`
  aliases (`d2cc4ffb`, `3f8ee9f5`); PEP 560 `__mro_entries__` and
  `__orig_bases__` in class creation (`58a7c25f`); starred expressions in
  subscripts, PEP 646 (`06b841a6`); `class C(*bases, **kw)` (`861c4307`).
- **Standard library:** `import typing` completes, with `Generic[T]`
  subclasses, `Protocol` and aliases (`63945ff9`, `e160bc86`); `import asyncio`
  works (`461d3e6b`), and `asyncio.run()` with `gather` no longer hangs
  (`311e21fa`); `sys.setswitchinterval` and `sys.getswitchinterval`
  (`198fa281`); `itertools.batched` (`6f7b6549`); `collections.defaultdict`
  implemented on `dict.__missing__` (`fba4b8e4`).
- **Operating system interfaces:** a POSIX subset of `os` (`read`, `write`,
  `dup`, `dup2`, `fork`, the `exec*` family and more), a new `fcntl` module and
  `_posixsubprocess` fixes, so `subprocess.run(..., capture_output=True)` works
  (`c863ccb5`); further `os`, `time`, `signal`, `sys` and `fcntl` functions
  and constants (`6554b512`, `ea12e3db`, `775ec6b2`, `c514eb69`, `1c54f1f9`,
  `cc1c0efa`, `2eef3141`, `7f01b16a`, `52764c2f`, `237e2a6b`).
- **Command line:** CPython interpreter flags such as `-I`, `-E`, `-S`, `-O`,
  `-B`, `-u`, `-X opt` and `-W filter` are accepted as no-ops, and
  `sys.executable` is read from `/proc/self/exe` (`294b3316`).
- **HPy-style extension modules:** `HPyModuleProvider` is registered, so
  `import foo` loads `foo.hpy.so` (or `foo.so`) exporting `HPyInit_foo`.
  Modules keep their attributes and functions (module objects and function
  wrappers were immutable, so every attribute write was lost), functions
  receive the module as `self`, libraries are opened with `RTLD_LOCAL` and
  closed again when the entry point is missing, and the loaders' handle maps
  are locked. `examples/hpy/math_hpy.cpp` is built and imported by CTest. The
  API remains a C++ subset, not the HPy universal ABI; limits are listed in
  `docs/HPY_USER_GUIDE.md`.
- **Benchmarks:** the harness rejects runs that time out, exit with an error or
  do not print the expected result line (`e0d96c9b`); benchmarks print a
  `BENCH_RESULT` line with their inner timing, and
  `benchmarks/run_full_stack.py` compares protoPython with protoCpp and CPython
  at the same workload sizes (`3d425957`); a subset of pyperformance workloads
  (`5cf365ce`).

### Changed

- **protopy runs protoCore's adaptive heap controller by default.**
  `PythonEnvironment::getProcessSpace` calls `ProtoSpace::enableAdaptiveHeap`
  once, when the process space is created (guarded by
  `PROTOCORE_HAS_ADAPTIVE_HEAP`; every protoCore from the 2.11.0 floor has
  it). Before, protoPython set no heap limit, so protoCore never started a
  collection by itself and the heap grew with every allocation (the
  `adaptive_heap_garbage.py` loop of 20000 rounds: 5.6 GB peak RSS). Now the
  heap budget is 75 % of the smaller of physical memory and the process
  memory limit (cgroup on Linux, job object on Windows), a soft limit inside
  it grows only while the program waits for the collector, and **memory is
  not returned to the operating system**. `PROTOCORE_HEAP_LIMIT_CELLS` sets
  the budget; `PROTOCORE_ADAPTIVE_HEAP=0` turns the controller off (the
  budget becomes a fixed hard limit). README, "Memory". Embedders that create
  their own `proto::ProtoSpace` are not affected.
  - Tests: `adaptive_heap_on_by_default` (the controller's per-cycle trace
    appears with no environment set), `adaptive_heap_off_by_env` (only
    fixed-limit cycles with `PROTOCORE_ADAPTIVE_HEAP=0`) and
    `adaptive_heap_bounded_memory` (a 1 M-cell budget, about 85 times that in
    garbage, at least 10 cycles, peak RSS at most 160 MB; measured 73 MB),
    driven by `test/regression/adaptive_heap_check.py`, which measures the
    child's peak RSS on Linux, macOS and Windows. The first and third fail
    against the previous build (no controller line; with the variable set,
    fixed-limit cycles); the second passes there too, because
    `PROTOCORE_HEAP_LIMIT_CELLS` alone already applied a fixed limit, so it
    guards the switch, not the adoption.
  - `test_attr_write_groups` set a 200 M-cell ceiling so that no collection
    would run during its measurements; under the controller the soft limit
    starts at 2 M cells, a cycle started, and its snapshot reader (a native
    loop without a safepoint) held the stop-the-world forever (timeout). It
    now also sets `PROTOCORE_ADAPTIVE_HEAP=0`, which applies its ceiling as a
    fixed limit on every protoCore from the 2.11.0 floor
    (`PROTOCORE_ADAPTIVE_HEAP_START=budget` was tried first and timed out on
    the Windows floor job, cross-platform run 38101731627: 2.11.0 does not
    know it).
  - Held until the closure defect it exposed was fixed (see "Fixed": a
    running call's frame was not rooted across the interpreter's own
    safepoint). With the controller every program collects, so it was hit in
    the default configuration: `protopy_list_rewrite_threads` failed on macOS
    (cross-platform run 38102529735) and locally about 1 run in 5.
  - protopyc-compiled modules collect too now, and code that protopyc
    generates is not safe against collection: a module with an allocating
    loop can crash in its caller (issue #2, open). Before the controller this
    happened only under `PROTOCORE_HEAP_LIMIT_CELLS`.
  - The rising live set seen under the controller's trace is explained:
    - Comprehensions: one permanent mutable cycle per execution, fixed (see
      "Fixed"). A comprehension in a 20,000-iteration loop now stays at
      158-190 K live cells; it reached 1.27 M before the fix.
    - Chains of class instances: bounded, not a leak. Acyclic chains of
      mutables drain one level per cycle, so 50-deep chains rise to about
      1.9 M cells within 30 cycles and stay there for the next 400.
  - Measured on 2026-10-11 (DEV12 notebook, synthetic). Builds: main with the
    GC fixes ("before", no controller) and this branch ("after"), both against
    protoCore 2.14.3. Medians of 3, interleaved, every run's output verified;
    wall s / peak RSS MB:
    - `list_append_loop` 0.53 / 394 -> 0.40 / 147
    - `binary_trees` (9) 3.65 / 1646 -> 3.55 / 553
    - `sieve` (50000) 1.26 / 951 -> 1.03 / 188
    - `fannkuch` 6.04 / 3272 -> 3.95 / 137
    - `mandelbrot` 6.02 / 2072 -> 4.76 / 136
    - `adaptive_heap_garbage.py`, 20000 rounds: 6.70 / 5433 -> 9.93 / 208
      (**+48 % wall**; the before runs themselves spread over 6.66-9.15 s).
      A loop that keeps nothing, which the old default never collected, now
      pays for its cycles.
    - A list comprehension in a 20,000-iteration loop: before, 11 GB peak RSS,
      and earlyoom killed 2 of 3 runs (the one that finished took 15.32 s);
      after, 22.17 s / 139 MB (**+45 % wall** against that run).
    - `int_sum_loop`, `str_concat_loop`, `range_iterate`, `attr_lookup`,
      `call_recursion`, `fib`, `nqueens` and `richards_lite` run no cycle and
      are within -1 to +8 % (run-to-run noise).
    The GC fixes themselves cost nothing measurable without the controller:
    the previous main against this main, both with no limit, is within -7 to
    +4 % on the same workloads. Tuples are interned and never collected, as
    documented.

- **CI builds protoCore 2.14.3** (tag `v2.14.3`, commit `0923b3e4`) in the
  Linux job and the macOS and Windows jobs, instead of 2.14.2.  2.14.3 changes
  no library code: `check_static.py` no longer tells a runtime that calls
  `enableAdaptiveHeap` that it never sets a heap limit, and
  `ProtoContext::safepoint()` states the rooting obligation whose violation was
  the closure defect above. The Windows floor job stays on 2.11.0.

- **CI builds protoCore 2.14.2** (tag `v2.14.2`, commit `17bc118e`) in the
  Linux job and the macOS and Windows jobs, instead of 2.14.1: the collector
  adjustments (the multi-cursor sweep engages only when it pays; the adaptive
  controller's live-set floor). The Windows floor job stays on 2.11.0. The
  full CTest suite (839 tests, protoCore's 2.14.2 suite included) passes
  locally against 2.14.2 built from `../protoCore` (ldd-verified).

- **CI builds protoCore 2.14.1** (tag `v2.14.1`, commit `94951559`) in the
  Linux job and the macOS and Windows jobs, instead of 2.12.0: the
  collector's control law (2.13.0), the sweep's helper threads (2.14.0) and
  the thread-exit quorum fix (2.14.1). The Windows floor job stays on 2.11.0.
  No source change was needed: the full CTest suite (823 tests, protoCore's
  2.14.1 suite included) passes locally against 2.14.1 built from
  `../protoCore` (ldd-verified). protoPython does not enable protoCore's
  adaptive heap.

- **protoCore 2.11.0 is required** (was 2.7.0): attribute write groups use
  `ProtoObject::setAttributes`. A sibling `../protoCore` without it is refused
  at configure time. CI builds protoCore 2.12.0 (tag `v2.12.0`, `f969d151`) on
  Linux, macOS and Windows, and the 2.11.0 floor (`69b56afe`) in the Windows
  floor job. protoPython does not enable protoCore's adaptive heap.

- **CI builds protoCore 2.10.2** (tag `v2.10.2`, commit `b7f6d82a`) in the
  Linux job and the macOS and Windows jobs, instead of 2.9.4; the Windows
  floor job stays on 2.7.0. No source change was needed: the full CTest suite
  (765 tests, protoCore's suite included) passes locally against 2.10.2 built
  from `../protoCore`. protoPython does not enable protoCore's adaptive heap.

- **`protopy` with no target runs the program on standard input** when standard
  input is not a terminal (`echo 'print(1)' | protopy`), and starts the REPL
  when it is, as CPython does; it used to print the usage text and exit with
  status 64. `protopy - [args...]` reads the program from standard input in
  either case. `PythonEnvironment::executeString` now compiles the source under
  the name it is given, so tracebacks name it (`"<stdin>"`, or an embedder's
  name) rather than always `"<string>"` (test: `protopy_cli_stdin`).
- **protoCore 2.7.0 is required** (was 2.0): `proto::proto_long` first exists
  there. The DEB/RPM dependency floor follows (`8c9e1b45`). CI builds
  protoCore 2.9.4 (`9cb0ef54`, tag v2.9.4) in every push and pull-request job
  on Linux, macOS and Windows, and the 2.7.0 minimum (`fc5d79db`) in one
  Windows job of `cross-platform.yml`, the only floor job; the nightly
  schedule of `ci.yml` builds protoCore `master`.
- **Embedder conformance host and protoCore 2.9.4's `releaseFlagRaised()`.**
  protoCore 2.9.4 made the `joinBlockingThread` release flag atomic and added
  `proto::conformance::releaseFlagRaised()` as the read hosts must use.
  protoPython's host never reads the flag (its worker is bounded by the clock,
  not by the flag), so there was no plain read to replace; the host's comment
  now records this, and that any future poll must go through the helper.
- **Scripts no longer call `main()` implicitly.** protopy used to call a
  module-level `main()` after running a script, so a script that called
  `main()` itself ran it twice. Scripts now behave as in CPython. Wall-clock
  and RSS figures recorded for protopy before this change include that extra
  run; inner timings are unaffected (`bd6bd4f0`).
- **`vars(obj)` and `obj.__dict__` of plain instances iterate in hash order**,
  not insertion order, because instance attribute writes no longer maintain a
  separate key list. Dicts built with `{...}`, `dict(...)` and item assignment
  keep insertion order (`6368ec16`).
- **Blocking calls no longer stall garbage collection.** `time.sleep`,
  `os.read`, `os.write`, `os.waitpid`, `select.select`, `signal.pause`,
  `input()`, lock acquisition and thread joins run inside protoCore's
  unmanaged regions, so a thread blocked in the kernel does not hold up
  collection for the other threads (`d1f126b6`).
- **Packaging:** the DEB and RPM packages depend on the `protocore` package
  instead of bundling libprotoCore (`ca6b47d7`).
- **Build:** `libprotoPython` is compiled with `-ftls-model=initial-exec`, so
  it must be loaded at program start rather than with a later `dlopen`
  (`4ceb8694`). Diagnostic accessors are compile-time false in release builds;
  compile with `-DPROTOPY_DIAG_ENABLED=1` to re-enable them (`82a5dd08`).
- **protoCore requirements:** several changes use recent protoCore features,
  for example the unmanaged-region API (protoCore `79a725c9`, used by
  `d1f126b6`) and exact numeric comparison (protoCore `0335411b`, used by
  `2cfb1e22`). Build against a current protoCore.
- **Benchmarks:** `memory_pressure` is reported but excluded from the geometric
  mean, because protoCore's collector defers reclamation by design
  (`0515365b`).
- **Removed `JsonModule` and `HeapqModule`:** both native modules were
  compiled but never registered, and both were incorrect (`JsonModule` copied
  escape sequences literally, rejected exponents and integers of 20 or more
  digits and dumped floats with six significant digits; `HeapqModule`'s
  `heappush` and `heappop` did not modify the caller's list). Registering
  either would have shadowed the working pure-Python `json` and `heapq`, which
  remain in use; `_json` and `_heapq` are not provided. The comments of
  `ROT_THREE`, `ROT_FOUR`, `WITH_CLEANUP` and `COMPARE_OP` in
  `ExecutionEngine.h` now describe what the engine does, and opcodes 197-201
  (`GEN_START`, `GET_LEN`, `MATCH_MAPPING`, `MATCH_SEQUENCE`, `EXTENDED_ARG`)
  are marked as reserved: never emitted and not handled.

### Fixed

- **list, tuple and dict reprs show every item; a self-containing container
  shows `[...]` / `{...}`** (2026-10-11). The reprs stopped after 20 items with
  `, ...`, and a list or dict containing itself recursed until the stack
  overflowed (a segmentation fault). A per-thread guard (CPython's
  `Py_ReprEnter`) marks the containers whose repr is being built. The reprs are
  new strings, no longer interned (an interned string is never collected).
  Test: `protopy_repr_containers`.
- **protopyc** (2026-10-11): a method's function-storage key
  (`__protopyc_fs_<name>__`) no longer shows in `vars(cls)`, `cls.__dict__` or
  `dir()`; `a in b` / `a not in b` compiled to concatenation (`'a' in 'abc'`
  was `'aabc'`) and now run the interpreter's `COMPARE_OP`; tuple and list
  targets unpack (`a, b = v`, `for k, v in ...`; they compiled to nothing);
  `__class__` in a method is the class; the self-recursive call's argument array
  was named `__argv`, a macro of the MSVC runtime, so the emitted C++ of a
  recursive function did not compile on Windows; the generated-code preamble is
  split into literals under MSVC's 16,380-byte limit (error C2026).
- **protopyc: compiled attribute reads and method calls cost less**
  (2026-10-11), recovering most of what `933da717` added. Instructions per
  300,000 iterations, loop included (perf stat, Release):

  |                | 52620c49 | 5b37c419 | now   |
  |----------------|---------:|---------:|------:|
  | method call    | 1,038 M  | 1,405 M  | 876 M |
  | attribute read |   141 M  |   248 M  | 193 M |
  | attribute write|   490 M  |   671 M  | 623 M |
  | fib(25)        |   215 M  |   214 M  | 217 M |

  `loadAttr` first tries the interpreter's `LOAD_ATTR` own-attribute path
  (`PythonEnvironment::ownAttributeFast`, protoCore's per-thread attribute
  cache); `callMethod(ctx, obj, name, {args})` takes a name interned at the call
  site and calls a function found on the class with `obj` first, without a bound
  method; `storeAttr(ctx, ...)` takes the context, and the store check (shared
  with the interpreter's `STORE_ATTR`) answers a name no class holds with one
  lookup instead of two. What remains above `52620c49` is the attribute
  protocol itself, which that build did not apply.

- **Interpreter: `finally` runs however a `try` statement is left; exceptions
  are chained** (2026-10-11). The compiler protected only the body with the
  `finally` clause: an exception raised in an `except` or `else` clause, an
  exception no `except` clause matched, and `return` / `break` / `continue`
  leaving an `except` or `else` clause skipped it. The stack of exceptions being
  handled (`sys.exc_info()`, a bare `raise`) was also left unbalanced on those
  paths and by `with` statements, so `sys.exc_info()` went on reporting a
  handled exception after the statement, and an exception passing through a
  `try` statement got a duplicate traceback entry for each clause it crossed.
  `try` statements are now laid out as CPython lays them out
  (`Compiler::compileTry`): one block runs the `finally` clause for the body,
  the `except` clauses and the `else` clause; each `except` clause, and the
  `finally` clause run for an exception, has a cleanup block of its own
  (`compileActiveExceptionClause`), and leaving one by `return`, `break` or
  `continue` pops it and the exception it handles (`BlockType::ExceptHandler`).
  `OP_RERAISE`, a stub that looped forever, re-raises the active exception and
  pops it, adding no traceback entry (nor does a bare `raise`). An exception
  raised while another is handled gets it as `__context__`; `raise X from Y`
  sets `__cause__` and `__suppress_context__` (the cause was ignored).
  *Measured* (Release, `perf stat`, interleaved with `52620c49`, on a machine
  shared with other builds; these numbers include the protopyc changes below,
  which share `ExecutionEngine.cpp` and `PythonEnvironment.cpp`): `fib(27)`
  +1% instructions, +8% cycles (median of 8); a `try`/`finally` loop +1.4%
  instructions, +7% cycles; a raising `try`/`except` loop +1% cycles. Most of
  the cycle difference is code layout: inlined into the dispatch loop, the new
  exception paths alone cost `fib` 15-20% more cycles at the same instruction
  count, so they are out of line, and a by-reference lambda capture in
  `BUILD_CLASS` added 4% instructions until it captured by value.
  *Test*: `protopy_try_finally_semantics` (19 cases, every expected value
  CPython's): 0/19 before, 19/19 after.
- **protopyc: attribute reads, classes and methods follow Python's rules**
  (2026-10-11). The limitations `docs/PROTOPYC_SPECIFICATION.md` listed in
  section 5 for attribute reads, compiled classes and lambdas used as methods.
  - *Attributes.* Compiled `obj.name` was a raw protoCore lookup: a missing
    attribute read as `None`, and properties, other descriptors,
    `__getattribute__` and `__getattr__` were not applied; `obj.name = v`
    bypassed data descriptors and `__setattr__`; `obj.m()` called a
    `classmethod` or `staticmethod` object itself. Generated code now reads
    through `PythonEnvironment::loadAttr` (the read `LOAD_ATTR` and
    `getattr()` do, now one function) and writes through `storeAttr`, and
    `callMethod` passes `obj` first only to a function found on its class.
  - *Classes.* A compiled class was an object with `type` as `__class__` and
    its bases as protoCore parents: no `__mro__` of its own, so an interpreted
    subclass did not inherit its methods, no metaclass, `__init_subclass__`,
    `Enum`, dataclass or `__slots__`. The interpreter's `BUILD_CLASS` is now
    `PythonEnvironment::buildClass`, which compiled classes use too: the
    metaclass and `__prepare__`, the body run with that namespace as frame
    (with `__qualname__` and `__annotations__`), the metaclass call.
    `PythonEnvironment::storeName` records new names in the namespace's
    `__keys__` and stores through a `__prepare__` mapping's `__setitem__`, as
    `STORE_NAME` does. Zero-argument `super()` in a compiled method reads the
    class from a cell its storage keeps.
  - *Methods and closures.* The interpreter rebound a compiled function found
    on a class to the instance, losing its storage: its free variables read as
    `None` unless the class namespace held its storage key, which a lambda in a
    class body or a function attached from outside did not, and the
    interpreter's `with` called a compiled `__enter__` without the instance. A
    compiled function (`PythonEnvironment::isCompiledFunction`) now binds as a
    Python function (`bindNativeMethod`) in attribute reads, method calls,
    `super()`, special-method lookups and `__getattribute__` dispatch.
  - *Exceptions.* An exception raised in a compiled `except` or `finally`
    clause or `__exit__` gets the handled exception as `__context__`, and the
    `finally` clause sees the exception in transit in `sys.exc_info()`.
  - Also: a compiled slice (`a[i:j]`) built an empty object and raised
    `TypeError` (`PythonEnvironment::buildSlice` dropped the attributes it set
    on an immutable object), and a call with `*args` or `**kwargs` did not
    compile (it named protoCore functions that do not exist).
  - *Cost* (compiled module, Release, 300,000 iterations, against
    `52620c49`): a method call through `callMethod` 0.17-0.21 s before,
    0.19-0.29 s after (a fast path keeps compiled methods off the full
    lookup); an attribute read 0.018 s before, 0.033-0.041 s after; an
    attribute write 0.085 s before, 0.11 s after (`storeAttr` takes
    `OP_STORE_ATTR`'s plain-instance path when nothing intercepts the write).
    `fib(27)` compiled is unchanged (0.09-0.13 s).
  - *Tests*: `protopyc_python_classes` (CPython's output,
    `test/compiler/python_classes.expected`; the interpreter, and the compiled
    module under a heap limit with `PROTOCORE_GC_CONTEXT_THRESHOLD=200`, the
    limit alone and the adaptive controller) and
    `protopyc_emitted_cpp_python_classes` (the C++ compiles with this build's
    compiler, MSVC on Windows). Before, all six groups of the driver raised.
- **Interpreter: attribute errors** (2026-10-11). `None.name` read as `None`
  instead of raising `AttributeError`; a class's missing attribute said
  `'type' object has no attribute` instead of `type object 'C' has no
  attribute`; `getattr(obj, name, default)` returned the default for any
  exception the lookup raised, not only `AttributeError`.

- **protopyc: exceptions, `with` statements and the methods of classes defined
  in functions** (2026-10-11). The three limitations
  `docs/PROTOPYC_SPECIFICATION.md` listed in section 5.
  - *Exceptions.* Outside generators, a statement that raised left its
    exception pending and the rest of its statement list ran: the exception
    reached the caller only when the function returned, or a `try` statement
    when its body ended. Arguments after a raising one were still evaluated and
    the call made with `nullptr`, and `raise Cls` raised the class itself. Now
    generated code tests the pending-exception flag after every operation that
    can raise and after every statement, and unwinds with the C++ exception
    `__PyUnwind`, which each clause of a `try` statement, a `with` statement and
    the callable catch (the callable returns `nullptr` with the exception
    pending; a module initializer fails its import). `except` clauses push the
    exception they handle for `sys.exc_info()` and a bare `raise` (which now
    re-raises; it compiled to nothing); `return`, `break` and `continue` leave
    through `finally` clauses and `with` statements (`goto` to the clause, then
    the exit continues), and a `return` in a `finally` clause replaces the
    exception in transit. `raise X` instantiates a class; `raise X from Y` sets
    `__cause__`. An exception raised by a comprehension's iterator was
    discarded; it propagates. `PythonEnvironment::pendingExceptionFlagAddress()`
    gives generated code the flag's address once per call, so the test is a
    load, not a cross-library thread-local lookup.
  - *`with`.* The statement evaluated its context expressions and ran its body;
    it called neither `__enter__` nor `__exit__` and bound no target. Each item
    now looks up both on the context manager's type
    (`PythonEnvironment::lookupBoundSpecialMethod`, the interpreter's
    `SETUP_WITH` lookup moved out of `ExecutionEngine.cpp` to be shared), calls
    `__enter__`, binds the target, and calls `__exit__` with the exception's
    type, the exception and its traceback, or three `None`, also when the body
    is left by `return`, `break` or `continue`; a true result suppresses the
    exception. Several items nest.
  - *Methods of classes defined in functions.* The interpreter binds a native
    method found on a class to the instance, so a compiled method called as
    `obj.m()` lost its function storage and read its free variables as `None`.
    A `def` in a class body now also binds its storage in the class namespace
    under a key of its own (`__protopyc_fs_<C++ name>__`), and the method's
    entry point finds it through the instance's class, with the module
    namespace. Free variables are also transitive now: a method that defines a
    function using a variable of the function that defined the class captures
    it (the generated C++ did not compile).
  - The interpreter does not run a `finally` clause when an exception or a
    `return`/`break`/`continue` leaves an `except` or `else` clause, or when no
    `except` clause matches; compiled code runs it, as CPython does. Section 5
    of the specification lists this and the remaining limitations found while
    testing (no `__context__`; attribute reads do not raise `AttributeError` or
    apply descriptors; an interpreted class cannot inherit from a compiled one).
  - Tests: `protopyc_exceptions_with` compiles
    `test/compiler/exceptions_with_probe.py` (raising statements, arguments and
    tests; handlers, bare `raise`, nested `try`, `finally` with `return`,
    `break` and `continue`; exceptions in loops, methods and the module body;
    `with` with suppression, nesting, several items, exits and a truthy
    non-bool `__exit__` result; methods of classes defined in functions called
    through instances, bound methods, the class, compiled callers and a
    compiled subclass) and requires the compiled module's output to equal the
    interpreter's on the same source, and, for the `finally` cases the
    interpreter does not run, to equal CPython's
    (`exceptions_with_cpython.expected`), under a 1.5 M-cell heap limit with a
    200-cell context threshold, under the same limit with the default
    threshold, and under the default adaptive heap controller;
    `protopyc_emitted_cpp_exceptions_with` compiles its C++ with the build's
    compiler (MSVC on Windows). Against `c72e8cad` the first configuration
    failed: `try_withdraw` raised instead of returning, `with` bodies raised
    `NameError` on their target, and `g.greet()` raised `TypeError: 'NoneType'
    object is not callable`.
  - Performance (Release, protoCore 2.14.5, same runtime, interleaved runs,
    2026-10-11): `fib(27)` 0.078-0.090 s (median 0.080), against 0.081-0.090 s (median 0.088)
    on `c72e8cad`: the flag test is a load, and a comparison's result is tested
    without a call. A loop raising and catching
    `ValueError` in every other of 100,000 iterations takes 0.58-0.66 s,
    against 0.49-0.57 s on `c72e8cad`, which did not stop at the `raise` and
    returned 200,000 instead of 150,000 (the interpreter: 0.94-1.01 s). Each
    raise now unwinds with a C++ exception.

- **protopyc: compiled generators, and compiled code called from another
  module.** Two defects that `README.md` listed as known gaps of protopyc.
  - *Generators.* A compiled generator was a `switch` whose `case` labels sat
    inside the C++ blocks of its loops: a `yield` inside a `for` loop, a `try`
    statement or an assignment did not compile; the continuation read the
    generator from `self`, while `py_generator_send_impl` passes it as the
    first argument, so it restored no parameters (a generator consumed by a
    compiled `for` loop yielded nothing); `throw()` and `close()` never reached
    it. The continuation now re-enters the body structurally: the yields are
    numbered, and each statement list and `if`/`while`/`for`/`try`/`with`
    statement on the path to the suspended yield is re-entered without
    evaluating its test, iterator or context expressions again. A suspension
    stores every slot of the continuation's context (locals, a `for` loop's
    iterator, a caught exception) in `gi_locals` and the yield's number in
    `gi_pc`, on the generator object, where the collector sees them.
    `send()`, `throw()`, `close()` (a `finally` clause runs), the `return`
    value in `StopIteration` and `yield from` (with `send`/`throw` forwarded
    as `OP_YIELD_FROM` does) behave as in the interpreter;
    `py_generator_send_impl` now passes the exception of `throw()` and
    `close()` to a native generator's callback. A `yield` must be a statement
    or the value of an assignment; one inside another expression is a
    compile-time error.
  - *Module namespace.* Generated code resolves module-level names through
    the current frame and globals, which were the caller's: a compiled
    function called from another module raised `NameError` on its own
    module's classes and functions (a method naming its class, a function
    calling a function defined later). Every compiled callable is now bound to
    a storage object recording its module's namespace and its free variables,
    and enters that namespace while it runs (`protoPython::ModuleNamespaceScope`,
    which `CompiledModuleProvider` uses for the module initializer too). A
    compiled function that the interpreter binds to an instance (`obj.m(a)`)
    receives the receiver first in its arguments, as the prologue expects.
    `fib(27)` takes 0.075-0.078 s, against 0.078-0.083 s on `5cd5f040`
    (Release, both against protoCore 2.14.4 built from source, 2026-10-11).
  - Also: `def` and `class` statements in a function bind function locals; a
    class body binds names in the class namespace; lambdas keep their
    parameters in slots and capture enclosing locals as a `def` does; a `for`
    loop no longer stops at a `None` item or discards an exception raised by
    its iterator; a `try` statement's `else` clause runs; `except E as name`
    binds a function local. Inside a generator, a statement that leaves an
    exception pending ends its statement list (outside generators, compiled
    code still runs the rest of the list; see `docs/PROTOPYC_SPECIFICATION.md`,
    section 5).
  - Tests: `protopyc_generators_classes` compiles
    `test/compiler/generators_classes_probe.py` and requires the compiled
    module's output to equal the interpreter's on the same source, under a
    1.5 M-cell heap limit with a 200-cell context threshold, under the same
    limit with the default threshold, and under the default adaptive heap
    controller; `protopyc_emitted_cpp_generators` compiles its C++ with the
    build's compiler (MSVC on Windows). Against `5cd5f040` the probe did not
    compile, and smaller probes printed a generator sum of 0 and raised
    `NameError: name 'Point' is not defined`.

- **protopyc: compiled code was not safe against garbage collection**
  ([issue #2](https://github.com/gamarino/protoPython/issues/2)). A compiled
  function ran in its caller's context, kept its locals and temporaries only in
  C++ variables, and called `ctx->safepoint()` at loop heads, which hands the
  *caller's* young generation to the collector while the caller is in the
  middle of a call and while the compiled locals are reachable only from C++
  (protoCore's embedder rule 3). Once collections ran, a compiled module with an
  allocating loop gave wrong results or crashed in its caller; with protopy's
  adaptive heap controller collections always run, so this hit the default
  configuration. Its garbage was also not collectable while it ran: it stayed
  in the caller's young generation, which is a root. The code generator now
  runs every compiled callable (function, generator, lambda, module
  initializer) in its own context through `protoPython::ContextScope`, keeps
  its locals in that context's automatic slots (`local_<name>` is a reference
  to a slot), stores the value of every non-constant expression node in a
  temporary slot, keeps loop iterators, caught exceptions and class
  namespaces in slots, clears the temporary slots at each loop safepoint, and
  promotes the result to the caller as `runUserFunctionCall` does
  (`docs/PROTOPYC_SPECIFICATION.md`, section 4). Each compiled call now pays
  for a context, as an interpreted one does: `fib(27)` 0.11-0.14 s instead of
  0.07-0.08 s.
  - Tests: `protopyc_gc_rooting` compiles `test/compiler/gc_rooting_probe.py`
    (an allocating `while` loop, `for` loops, a closure called per iteration,
    list/dict/set comprehensions and a generator expression, an exception
    raised and caught per iteration, a lambda, string building, and a
    module-level loop) and checks every result under a 1.5 M-cell limit with
    a 200-cell context threshold, under the same limit with the default
    threshold, and under the default adaptive controller. Before the fix it
    failed on the first configuration (wrong results, then SIGSEGV); the issue's
    reproduction failed 6 of 6 runs in each of those configurations, and a
    1,000,000-iteration allocating `for` loop returned 100,340 instead of
    2,000,000 even under a 20 M-cell limit.
  - Also fixed on the way: a generator's continuation called
    `env->isStopIteration(val)`, which does not exist, so no module with a
    `yield` compiled. Compiled generators still failed at run time; that is
    fixed by the entry above.

- **A nested function lost the variables of its enclosing function once
  collections ran** (`NameError: name 'target' is not defined`, intermittent).
  The frame of a running Python call was referenced only from C++: the
  `frame` locals of `runUserFunctionCall` and `executeBytecodeRange` and the
  thread-local current frame, none of which protoCore scans. It was safe only
  while it stayed in its context's young generation, and the interpreter's own
  `ctx->safepoint()` (every 64 opcodes) hands that generation to the collector
  once the context has allocated `PROTOCORE_GC_CONTEXT_THRESHOLD` cells (10,000
  by default; protoCore's embedder rule 3). The next cycle freed the frame of
  any long-running call, the cell was reused, and `LOAD_DEREF` /
  `STORE_DEREF` walked a stranger: at the failing `LOAD_DEREF` the "frame" had
  `f_code` None, no parents and no closure. `executeBytecodeRange` now keeps
  the frame in its context's last automatic slot, which the operand stack no
  longer uses, and refreshes it before every safepoint. protoCore was not at
  fault: with protoCore built with a detector that aborts when a mutator uses
  a mutable handle the collector finalized, the failure still occurred with no
  report (the frame of a `CO_OPTIMIZED` function is immutable); with threshold
  submission disabled (`PROTOCORE_GC_CONTEXT_THRESHOLD=4000000000`, 30 M-cell
  limit) it failed 0 of 12 runs, and with a 200-cell threshold 24 of 24.
  Collection pacing (protoCore 2.12.0) only changed how often a cycle fell in
  the window: the same binary failed 10 of 12 runs against protoCore 2.11.0
  too, which contradicts the earlier "0 of 24 against 2.11.0" bisection.
  - Tests: `protopy_frame_survives_collection` (closure reads, `nonlocal`
    writes and a nested definition after allocation, run with a 200-cell
    context threshold and a 1.5 M-cell limit) failed 10 of 10 runs before the
    fix and 0 of 20 after; the `def race(n)` reproduction failed 8 of 24 runs
    under `PROTOCORE_HEAP_LIMIT_CELLS=1500000` and 0 of 24 after. The
    conformance host's producer/consumer workload now drains the queue in a
    nested function, so `gc.host_stress` reaches the path: 2 of 10 runs failed
    before the fix, 0 of 10 after.

- **What a function created through native helpers stayed live until it
  returned.** Several helpers allocate in `PythonEnvironment::getCurrentContext()`:
  iteration (`env->iter`, `next`), exception construction, the
  pending-exception slot, and 43 sites with a `s_threadContext` fallback.
  That function returned the thread-local context, which is set where code
  starts running (module execution, a new thread) and which calls do not
  update. So inside a call, the helpers allocated in an *outer* activation's
  young generation, a GC root until that activation reaches its own safepoint.
  For a program whose work runs in `main()`, that meant never. Under a fixed
  2.5 M-cell limit, 120,000 calls of a function that loops over a list, calls
  `iter`/`next`, raises and catches, or runs a comprehension each exhausted the
  heap. It now returns the thread's innermost live protoCore context (the one
  `chainParent` uses), and all of those complete with the live set flat at
  170-390 K cells. Test: `protopy_helper_garbage_is_collected` (out of memory
  on the previous build, 5.8 s now). This, more than the cycles below, is the
  rising live set seen while the adaptive heap was being adopted.
- **A frame-mapped function's operand stack was not a GC root.** A function
  whose locals a nested scope captures is compiled frame-mapped with
  `co_automatic_count` 0. Its context had no slots, so the interpreter kept its
  operand stack in a `std::vector` the collector never scans, and could not
  anchor its frame. The compiler now gives it the slots an optimized function
  gets. Its locals still live on the frame, and the two readers that took
  "has slots" for "locals are in slots" now test `CO_OPTIMIZED`
  (`BUILD_FUNCTION`'s copy into the closure frame, and bare `dir()`).
  The frame self-cycle below had hidden this by keeping every such frame
  alive: with that cycle removed, `protopy_frame_survives_collection` failed
  10 of 10 runs until this fix.
- **`f_back` named the previous callee, not the caller.** A call made its frame
  the thread's current frame and never restored the caller's, so
  `[f(), f(), f()]` saw `f_back` names `main, f, f` (CPython: `main` three
  times). It also chained consecutive frame-mapped calls into one chain of
  mutable frames that protoCore releases one level per cycle. Test:
  `protopy_frame_f_back_after_return`.
- **Each function created added a permanent GC root.** `createUserFunction`
  pinned the function's metadata cache for the life of the environment: one
  root per comprehension, lambda or nested def executed, in a set the
  collector scans inside every stop-the-world pause. The function already
  holds the cache, and the cache references no cell. Test:
  `MutableCycles.CreatingAFunctionAddsNoPermanentRoot` (203 roots for 200
  functions before, 0 after).
- **Comprehensions, lambdas and calls of closure-making functions were kept
  for ever.** Two edges closed cycles between mutable objects, which protoCore
  never collects (rule 13 in `docs/CONFORMANCE.md`), once per *execution*.
  This is the "growing live set" observed while the adaptive heap was being
  adopted.
  - `OP_BUILD_FUNCTION` bound every new function's name in its own closure
    frame. Only a recursive nested function reads that binding. Now the name
    is bound only when it is in the function's `co_freevars`, and never when it
    is not an identifier (`<listcomp>`, `<lambda>`, `<genexpr>`).
  - A function whose locals a nested scope captures (a non-`CO_OPTIMIZED`,
    frame-mapped function such as `def scale(xs, k): return [x * k for x in
    xs]`) stored its frame in the frame itself as the namespace that
    zero-argument `super()` reads. It now stores a marker, and `super()` takes
    the object on the frame's chain that owns it.
  - Measured under the controller with a 3 M-cell budget: a list
    comprehension in a 20,000-iteration loop went from 161 K to 1.27 M live
    cells over 189 cycles, and now stays at 158-190 K. 60,000 calls of
    `scale` drove the live set to 2.99 M cells (at the ceiling).
  - Not changed then: a named nested def stored in such a frame still formed
    one cycle per call of its defining function, structural to the whole-frame
    capture of closures (closures over cells removed it, 2026-10-11, above). Chains of instances (`n = Node(j, n)`, 50
    deep) were also checked: the live set rises to about 1.9 M cells within
    30 cycles and stays flat for the next 400. That is acyclic mutable chains
    draining one level per cycle, not a leak.
  - Tests: `MutableCycles.ComprehensionsAndLambdasLeaveNoCycle` (360 extra
    handles in cycles without the first fix, 0 with it) and
    `MutableCycles.AMappedFrameDoesNotReferenceItself` (30 cycles before, 0
    after). The conformance probe now drops its closure after use, so its one
    structural cycle stays attributable.

- **`obj.name = value` bypassed a `__setattr__` or a data descriptor inherited
  from a base class.** `OP_STORE_ATTR`'s fast path wrote the attribute
  directly unless the instance's own class owned `__setattr__` or an attribute
  of that name, so `class B(A)` with `A.__setattr__`, or a property defined on
  `A`, was ignored for instances of `B` (CPython calls them). The fast path
  now asks protoCore's parent-chain lookup (one `getAttribute` /
  `hasAttribute` walk, served by the attribute cache) whether a class on the
  chain defines `__setattr__` or holds a data descriptor of that name
  (`typeChainInterceptsStore`). Test: `protopy_store_attr_inherited_hooks`.

- **`io.StringIO` counted UTF-8 bytes.** `read(n)` returned `n` bytes, splitting
  multi-byte characters, and `tell()`/`seek()` were byte offsets. Positions are
  now character indices and `seek()` follows CPython's rules (`ValueError` for a
  negative position, `OSError` for a nonzero relative seek). Its buffer is no
  longer interned on every write (test: `protopy_stringio_char_positions`).
- **A `str` with an embedded NUL built by the runtime was truncated at it**
  (`PythonEnvironment::newStr` used protoCore's NUL-terminated constructor);
  it now passes the length.
- **Concurrent reads of `sys.stdin` tore lines.** Threads calling
  `sys.stdin.readline()` at once shared the read-ahead buffer without a lock and
  got torn or duplicated lines, or crashed the process. Each operation on a
  descriptor file now holds the descriptor's lock (test: `protopy_cli_stdin`,
  case `stdin_threads`, eight threads reading a pipe).
- **`compile()`** accepts an empty program in `exec` mode, and the `SyntaxError`
  it raises carries `filename`, which the error report prints instead of always
  `"<stdin>"`.
- **`protopy_re_results_not_interned` was flaky on macOS** (35744 KB against a
  35000 KB bound): it bounded RSS growth from the start of the process, which
  depends on the platform allocator. It now bounds the growth after a warm-up,
  measured in the same process: about 0 MB with collectable results, 24-34 MB
  on Linux, macOS and Windows with results interned again (checked by mutation
  in CI).
- **`sys.stdin` could not be read.** It was a stub with no `read`,
  `readline`, `readlines` or iteration, and `input()` read `std::cin`
  separately. `sys.stdin` (and `sys.__stdin__`) is now a text file over
  descriptor 0 as CPython's: UTF-8, `newline="\n"` on POSIX and universal
  newlines on Windows, `readline(size)`, `read(n)` in characters, a binary
  `sys.stdin.buffer`, `fileno`, `isatty`, `encoding`, and end of file reported
  once per read (a terminal can continue after Ctrl+D). `input()` writes its
  prompt to `sys.stdout` and reads `sys.stdin.readline()`, so it and
  `sys.stdin` consume one stream in order and a replaced `sys.stdin` is read;
  protopy's REPL reads through the same buffer. On a Windows console the
  reads use `ReadConsoleW` (this replaces protopy's console `std::cin`
  buffer). `io.StringIO` gained `readline`, `readlines` and iteration, and
  `readline(size)` works on files. Test: `protopy_cli_stdin`.
- **`sys.path[0]` was the standard library, and `-m` did not search the
  current directory.** `sys.path` is now ordered as in CPython 3.14: the
  script's directory (absolute, symbolic links resolved on POSIX), the current
  directory for `-m` or `''` for `-c` and the REPL, then `-p` and
  `PROTO_PYTHONPATH`, the standard library and the user site-packages. A
  script named like a standard module (`random.py`) is now found before it.
  New option `-P` (also implied by `-I`) prepends nothing. A `''` entry is the
  current directory at import time. Test: `protopy_cli_sys_path`.
- **Collection under a heap limit freed objects of running frames.** With
  `PROTOCORE_HEAP_LIMIT_CELLS` set, about one Windows run in ten of a workload
  that opens files while allocating crashed or read a live module-level string
  back as NUL bytes (CI run 36994197731: `protopy_heap_limit_live_frames`
  segfaulted on its second run). `ContextScope` and `runCodeObject` named the
  context they were handed as the new context's `previous`. protoCore's root
  scan starts at the thread's current context and follows `previous`, and
  native code is routinely handed a context that is not current (the
  interpreter's thread-local context, which function calls do not update; a
  generator's resumer): the new context hid the frames in between from every
  collection and, once destroyed, left an outer context current while those
  frames still ran. New contexts are now chained onto the thread's current
  context (`chainParent`, `include/protoPython/MemoryManager.hpp`). A
  `PythonEnvironment`'s root context, created without a parent, is chained onto
  the context current on the main thread, so destroying it no longer leaves no
  context current (the mismatches at startup and exit). Builds with
  `PROTOPY_CHECK_CONTEXT_CHAIN` (on in Debug) abort on a context handed a
  context that is not live on its thread or destroyed out of order; the whole
  suite passes with it. `protopy_file_close_on_collect` runs on Windows again,
  and the cross-platform workflow runs it and `protopy_heap_limit_live_frames`
  25 times each. Tests: `test_context_chain`, `protopy_heap_limit_live_frames`.
- **Read-mode files had no `seek`/`tell`.** `open(path)` and `open(path, "rb")`
  objects had no `seek`, `tell`, `readable`, `writable` or `seekable` (so
  `tokenize.open` failed and `traceback.format_exc()` printed no source lines),
  read `n` bytes instead of `n` characters in text mode, accepted `write`, and
  read nothing after `close()` instead of raising `ValueError`. They now follow
  CPython; a text-mode `tell()` cookie is a byte offset in the decoded text.
  Also: append mode starts at the end of the file; `sys.stdin`/`stdout`/`stderr`
  have `fileno`, `readable`, `writable`, `seekable`, `isatty`, `tell` and
  `seek`; `StringIO.seekable()`; `fileno()` of `StringIO`/`BytesIO` raises
  `io.UnsupportedOperation`, which is now a real exception class
  (`OSError`, `ValueError`) rather than a stub object. Test:
  `protopy_file_seek_tell`.
- **Functions had `__annotate__ = None`**, so `functools.singledispatch`'s
  plain `@f.register` on an annotated function raised `TypeError`. A function
  with annotations now has an `__annotate__` (PEP 649/749) returning its
  annotations for `Format.VALUE` and `FORWARDREF` and their strings for
  `STRING`. Test: `protopy_function_annotate`.
- **protopyc: `break`/`continue` in a loop's `else:` clause** acted on the
  finished loop, as the interpreter did before `23f4f85b`. Test:
  `protopyc_loop_else`.
- **`__main__.__file__` was the relative path given** (`sub/m.py`,
  `./m.py`). It is absolute, as in CPython since 3.9 (the current directory
  joined with the path, unnormalized on POSIX); `sys.argv[0]` keeps the path
  as given. `-c` code and the REPL no longer get `__main__.__file__ =
  "<string>"`/`"<stdin>"`: CPython's `__main__` has none. Tests:
  `protopy_main_file_absolute`, `protopy_c_main_has_no_file`.
- **`weakref.WeakKeyDictionary` never found its keys.** `weakref.ref` objects
  had no `__hash__`/`__eq__`, so `ref(obj)` built for a lookup was never equal
  to the `ref(obj)` stored as the key: `get`/`in`/`[]` missed, setting a key
  again added a second entry, and `functools.singledispatch` never hit its
  dispatch cache. A reference now hashes like its referent (`TypeError` once
  the referent is gone) and two references compare their referents while both
  are alive, identity otherwise, as in CPython; its repr is
  `<weakref at 0x...; to 'C' at 0x...>`. (`id()`/`hash()` of an instance were
  already stable across attribute mutation; the test pins that too.) Test:
  `protopy_weakref_identity_keys`.
- **`break`/`continue` in a loop's `else:` clause acted on the finished
  loop.** The compiler emitted the `else` clause of `for`, `while` and
  `async for` while the loop was still the innermost one, so a `break` there
  popped the enclosing loop's iterator and fell through, and the enclosing
  loop ran again. `functools._c3_merge` uses that shape: every
  `singledispatch` call with a type not in the registry (and
  `functools._c3_mro(object)`) looped until the process ran out of memory.
  Test: `protopy_loop_else_break`.
- **Traceback entries showed `File "<unknown>"` and wrong lines.** Only frames
  that had a frame object with a matching code object got a usable entry:
  small functions run without a frame object, and module-level and
  `exec`/`eval` code run with their namespace as the frame, so their entries
  had no `f_code` (dropped by `traceback.extract_tb`, `<unknown>` in the
  uncaught-exception report) or carried the calling module's file and name.
  Every entry now holds the running code object (a synthesised frame when the
  activation has none), and `tb_lineno` is the line of the instruction that
  raised: the line table (`co_lnotab`, a tuple) was never decoded because it
  was read as a list, its offsets were compared in the wrong unit, and line
  jumps over 127 lines were clamped. `exec`/`eval` code is named `<string>`
  with lines counted inside the string; `compile()` keeps its file name. The
  uncaught-exception report no longer prints a trailing `<unknown>` entry for
  the end of the chain and shows each entry's source line, as CPython does.
  Tests: `protopy_traceback_locations`, `protopy_traceback_uncaught`.

- **atexit handlers ran at the end of the next import.** `PythonEnvironment::
  executeModule` ran every registered handler when any module finished
  executing, so a handler registered by `weakref.finalize` (for example
  `tempfile.TemporaryDirectory`'s) fired while the program was still running
  as soon as another module was imported, such as `importlib.util` from a `.pth`
  line processed by `site.addsitedir`. protopy now runs them once at
  interpreter exit, also after `SystemExit`, an unhandled exception and in
  `-c` programs (which never ran them). A handler that raises is reported as
  in CPython, through `sys.unraisablehook` (now a real default hook rather
  than a no-op): `Exception ignored in atexit callback <function f at 0x...>:`
  and the traceback, instead of `atexit: handler raised an exception;
  continuing.`; keyword arguments given to `atexit.register` are passed to the
  handler (they were dropped). The bundled `gc` module named `isenabled` as
  `is_enabled`, so `weakref.finalize`'s exit handler raised `AttributeError`
  whenever a finalizer was alive at exit; `disable()`/`enable()` now change
  what `isenabled()` reports. Functions have CPython's repr, `<function
  qualname at 0x...>` (tests `protopy_atexit_callback_error`,
  `protopy_pth_importlib_empty_home`, `protopy_weakref_finalize_at_exit`).
- **Path-based import machinery.** `sys.path_hooks` was empty, so
  `importlib.machinery.PathFinder.find_spec` found nothing: a setuptools
  `*-nspkg.pth` namespace package became an empty placeholder module, and
  `pkgutil.iter_modules` and other users of path hooks did not work.
  `sys.path_hooks` now holds `zipimport.zipimporter` and `FileFinder`'s hook
  (for `.py` source files) and `sys.path_importer_cache` caches the
  finders; both are filled by importing `importlib` on their first use.
  importlib's bootstrap modules are also registered as `_frozen_importlib`
  and `_frozen_importlib_external`, so `import zipimport` works. Fixed on the
  way: `posix.fspath` (used by importlib's path functions), `compile()` of
  `bytes` source, `sys.dont_write_bytecode`, `_imp.extension_suffixes()`
  returning a real list, and weak references to classes (which
  `functools.singledispatch`, used by `pkgutil.iter_modules`, needs). `_imp`
  no longer has a `create_dynamic` that only raised `ImportError`. The native
  importer imports PEP 420 namespace packages, and also searches a parent
  package's `__path__` for its submodules. See
  `docs/PYTHON_COMPATIBILITY.md`, "Import system" (test
  `protopy_import_path_hooks`).
- **Windows ZIP is self-contained.** Like protoST, protoScala, protoClojure
  and protoJS, the ZIP (and the NSIS installer, built only when `makensis` is
  found) now bundles protoCore's DLL, copied from the linked `protoCore` target
  whatever its name, and the MSVC runtime through `InstallRequiredSystemLibraries`.
  `cross-platform.yml` checks the DLLs are in the ZIP and runs the unpacked
  `protopy.exe` on a script with only the Windows system directories on `PATH`.
- **Frame objects expose `f_locals` and `f_lineno`.** `import site` failed
  with a setuptools `*-nspkg.pth` in the user site directory (e.g.
  sphinxcontrib-jsmath's), whose line reads `sys._getframe(1).f_locals['sitedir']`.
  `f_locals` is the module namespace at module level and a snapshot dict of the
  local variables in a function (fast-slot locals included, read from the running
  activation); bare `exec()` in a function now runs in its own frame whose `f_back`
  is the caller, and sees the caller's variables (it raised `NameError` for them);
  functions that name `_getframe`/`currentframe` always get a frame. The native
  `_warnings.warn` ignores `ImportWarning`, `PendingDeprecationWarning` and
  `ResourceWarning`, as CPython's default filters do. Differences are listed in
  `docs/PYTHON_COMPATIBILITY.md` (test `protopy_frame_f_locals`).
- **Windows-port review (branch `fix/windows-review`).** Each item has a
  regression test that runs on Linux, macOS and Windows unless stated.
  - `tempfile.NamedTemporaryFile`/`TemporaryFile` work: `open()` supports
    read-write modes (`w+`, `r+`, `a+`, `x+`), `opener=`, `seek`/`tell`/
    `truncate`/`writelines` and text-mode newline translation (`\n` is written
    as `os.linesep` on Windows, as CPython); on Windows `os.O_TEMPORARY`,
    `O_NOINHERIT`, `O_SHORT_LIVED`, `O_SEQUENTIAL`, `O_RANDOM` and `O_TEXT`
    exist and are honoured (`protopy_tempfile_roundtrip`,
    `protopy_text_newline_write`).
  - A file object that is never closed releases its descriptor when it is
    collected (it never did); Windows deletes with POSIX semantics where NTFS
    has them. Files still close on `close()`/`with`/collection, not on losing
    their last reference (documented) (`protopy_file_close_on_collect`;
    `protopy_unclosed_files_directory_cleanup`).
  - `time.gmtime`/`localtime`/`ctime` and `datetime` timestamp conversions
    before 1970 and after 3000 no longer dereference a null `struct tm`:
    UTC conversions work for every year, local ones raise `OSError` where
    the C runtime cannot represent them; `struct_time.tm_wday` counts from
    Monday; `date.timetuple()` no longer returns None
    (`protopy_time_out_of_range`).
  - `re`: Python's line rules for `^`, `$`, `\A`, `\Z` and `.` on every
    platform (MSVC matched `^`/`$` at every line); leading inline flags,
    `pos`/`endpos`, module-level `flags` (`protopy_re_anchors`).
  - protopyc emits fixed-width integers and portable overflow checks
    (`CheckedArith.h`); the interpreter's `2**40 * 2**40` evaluated to 0
    (`test_checked_arith`, `protopyc_emitted_cpp_portable`,
    `protopyc_portable_ints` (Linux/macOS)).
  - `re_results_not_interned` checks its token count everywhere and reads RSS
    through `sys._current_rss()` on all three platforms.
  - `select.select`: `ValueError` for unrepresentable sets; on Windows a real
    wait for sockets and a classification that cannot mistake a socket for a
    descriptor (`protopy_select_pipes`).
  - `os.symlink`, `os.DirEntry`, `DirEntry.*(follow_symlinks=False)`;
    `os.readlink` raises `OSError`; on Windows `st_file_attributes`,
    `st_reparse_tag` and `stat.IO_REPARSE_TAG_*` (`protopy_copytree_symlinks`).
  - Windows: the invalid-parameter handler is per thread in the library
    (process-wide only in protopy.exe) (`embedding_invalid_parameter_scope`);
    console code pages are restored at exit; console input uses
    `ReadConsoleW`; `/W3` for protoPython's targets; NSIS is packaged only
    when `makensis` is found, and CI builds the ZIP and runs it from a clean
    directory.
  - `print(file=f)` writes to `f` (it wrote to stdout for any file object);
    diagnostic `printf` formats use `%llu` with explicit casts.

- **macOS (by reading; no macOS host):** build-tree executables get an rpath
  to an installed protoCore's lib directory; `<sys/sysmacros.h>` is included on
  Linux only; `environ` comes from `_NSGetEnviron()` in the shared library
  (`a3e255b9`).
- **Files opened in binary mode read bytes.** `open(path, "rb").read()`,
  `readline()`, `readlines()` and iteration answered `str`; they answer `bytes`
  now, as in CPython. `io.open_code(path)` opens in binary, as `open(path,
  "rb")`. `import site` failed with "'str' object has no attribute 'decode'"
  whenever a `.pth` file was installed (an editable install in the user's
  site-packages, for example), because `site.addpackage` decodes what
  `open_code` reads. Test: `protopy_io_open_code`.

- **`re` no longer interns its results:** every match, group, `sub`, `split`,
  `findall` and `escape` result, and the pattern source and scanned subject,
  were created with `getInternedString`, which returns a symbol protoCore never
  collects and records in a process-wide pool. Each distinct result was
  therefore permanent, so `re` over varying input grew the heap without bound.
  They are ordinary collectable strings now. Over 50000 calls producing
  distinct results, under a heap limit so the collector runs, RSS growth falls
  from 114948 KB to 49804 KB. The cost is 21.5% more instructions on code whose
  results repeat, which interning used to serve from a thread-local cache
  without allocating. `PythonEnvironment::newStr` is the helper for computed
  text; the header now states that `getInternedString` is for vocabulary only.
- **Native truth test:** `PythonEnvironment::isTrue` answered `False` for every
  user-defined object. It tried the container fast paths before the dunder
  lookups, and `asSparseList()` matches the own-attribute storage of an
  ordinary instance, so an object with no own attributes measured as size 0:
  `if obj:` was false for an object whose class defines `__len__` returning 3,
  and for one that defines nothing at all. A Python-defined `__bool__` or
  `__len__` was ignored as well, because only native methods were invoked. The
  dunders now run first, Python-defined ones included, and the raw-container
  checks that remain demand pointer identity so they cannot match a wrapped
  instance or an attribute map. Large integers are no longer converted with
  `asLong`, which raised "LargeInteger value exceeds long long range"; the
  interpreter's own truth test (`if` / `while`) had the same defect, so
  `while big_int:` raised.
- **`frozenset`:** the non-mutating set methods are available on frozenset —
  `union`, `intersection`, `difference`, `symmetric_difference`, `issubset`,
  `issuperset`, `isdisjoint` and `copy`. Only the operator forms (`|`, `&`,
  `-`, `^`) had been installed, so the named methods raised `AttributeError`.
  The set implementations are receiver-generic and already return a frozenset
  for a frozenset receiver. `frozenset.copy()` also returns a frozenset now; it
  used to return a `set`, because the copy was rebuilt from the receiver's
  first parent without its `__class__`. The mutating half (`add`, `remove`,
  `discard`, `pop`, `clear`, `update`) stays absent, as it must.
- **`sys.path`:** a directory inserted into `sys.path` at runtime is searched by
  `import` again. The Python module provider walked only the search paths
  captured when the environment was created, so `sys.path.insert(0, d)` before
  an import had no effect and the import raised `ModuleNotFoundError`. The
  provider now reads the live `sys.path` on every load, falling back to the
  paths it was built with so the standard library stays importable if a program
  reassigns or clears `sys.path`.
- **Ordering comparisons:** `object() < object()` returned `True` instead of
  raising `TypeError: '<' not supported between instances of 'object' and
  'object'`. `compareObjects` refused to raise when either operand's type was
  `object`, so a pair of plain objects fell through to a pointer comparison and
  answered an arbitrary boolean. `object` is no longer treated as a type that
  orders natively; `sorted()` over plain objects raises too, and the built-in
  containers keep their existing behaviour.
- **`random`:** `random.Random(seed)` is reproducible again. `_random.Random`
  was a stub that ignored the seed and drew every value from `os.urandom()`, so
  two generators built from the same seed produced different sequences and
  `random.seed(n)` did not make the module sequence repeatable.
  `lib/python3.14/_random.py` now implements MT19937 with CPython's seeding
  (`init_by_array` over the 32-bit words of `abs(seed)`), so an integer seed
  replays CPython's own stream: `random.Random(42).random()` is
  0.6394267984578837. The state is allocated in `seed()`, not `__init__`,
  because `random.Random` subclasses this class and never calls our `__init__`
  (CPython builds the state in `tp_new`).
- **`with` blocks:** `return` from inside a loop that runs inside a `with`
  block raised `TypeError: 'object' object is not callable`. The loop's
  iterator sits on the operand stack above the context manager's `__exit__`,
  and `OP_WITH_CLEANUP` consumes the two topmost entries, so it took the
  iterator for `__exit__` and called it. The unwind now drops the iterators of
  any loops opened inside the with-block before the cleanup runs. `return
  f.read()` was unaffected; `for line in f: return line` was not.
- **Exit status:** `SystemExit.code` follows CPython (`None`, `args[0]`, or the
  arguments tuple); an uncaught `SystemExit` exits with 0 for `None`, with the
  value for an int, and with 1 after printing any other value (`27bd35f9`).
  The same rules apply to `protopy -c`, which previously exited with 70; its
  usage text now reports version 1.0.0 (`b8f36d11`).
- **Local variables:** fast locals start unbound, so reading or deleting one
  before assignment raises `UnboundLocalError` (`1e96e596`), also in the fused
  loop opcodes (`92971f9e`). Names assigned inside `while` bodies are compiled
  as function locals instead of globals (`d7dfec38`).
- **Thread safety without a GIL:**
  - list, set, dict and deque mutators publish their new contents atomically
    and retry on conflict, so concurrent `append`, `pop`, `add`, `d[k] = v` and
    similar operations no longer lose or duplicate items (`1614224f`,
    `bacdd8ca`, `1cc408a4`, `08911529`, `7e363525`); deque items are now traced
    by the GC (`7e363525`).
  - the keyword-names stack and the native coroutine queue are per thread,
    runtime helpers use the calling thread's context, and the exception
    filename cache is thread-local (`a776cccb`, `2ade50ac`, `9df4969b`,
    `e731474d`).
  - address-keyed caches (the type cache and the `LOAD_METHOD` inline cache)
    are invalidated on every GC cycle, fixing crashes when freed addresses are
    reused (`72ec5b35`); the `LOAD_METHOD` inline cache no longer binds calls
    to the first receiver it saw (`9338190a`).
  - `threading.Thread.join()` waits for the thread (`24bd0930`); `with lock:`
    acquires and releases native locks (`55bff9c6`); `Lock` and `RLock`
    acquisition cannot stall a stop-the-world pause (`a2a7fdb7`).
- **GC roots:** six native functions now pin transient objects they hold across
  calls that may trigger garbage collection (`df58d298`).
- **Dicts and numbers:** dict keys are looked up by hash alone, with the same
  key hash on every construction and lookup path (`3bf97555`, `12ce26cb`);
  unhashable keys raise `TypeError` (`641819d1`); equal ints and floats
  compare, hash and act as dict keys as one value beyond the `long long` range
  (`2cfb1e22`).
- **CPython semantics, 2026-09-13 and 2026-09-14 rounds** (summarised with the
  known remaining divergences in `docs/CPYTHON_CONFORMANCE.md`, `f800e14c`):
  `del name`; `exec` and `eval` of code objects; `__init_subclass__` for
  `type()`, `types.new_class()` and metaclasses; `reversed()`, `sorted()` and
  `map(strict=True)`; `OrderedDict`, `deque` and `defaultdict`; zero-argument
  `__class__`; rich comparison results and `TypeError` for unsupported
  orderings; `globals()` and `vars(module)` as mappings; `str()` of exceptions
  and of `KeyError`; duplicate call keywords; bytes and str comparisons;
  `delattr()`; submodule imports; `@dataclass(slots=True)`;
  `function.__closure__` and `types.CellType`; `type(module)`.
- **CPython semantics, 2026-05-13 to 2026-05-18 rounds**, driven by
  `test/cpython/test_descr.py`: `__mro__` derived from the protoCore parent
  chain in C3 order (`21d13bff`, `8410c1ba`); validated `__bases__` and
  `__class__` assignment; `__slots__` descriptors and name mangling; `super`
  as a real type (`c92e4ea8`); metaclass calculation and `ABCMeta.register`
  (`eef3b7fa`, `998712f2`); getset and member descriptors; the copyreg and
  pickle reduce protocol; `dir()` filtering; module objects. The commit that
  closed these rounds reports `test_descr.py` at 148 of 155 non-skipped tests
  passing, with the 7 remaining failures depending on synchronous garbage
  collection (`861b77b0`).
- **protopyc:** for-loop targets inside functions and globals in spawned
  threads (`eab663bd`); `and` and `or` as short-circuit expressions
  (`1084f969`); rebinding of module-level names (`aeb0e969`); `is` and
  `is not`, string literal escaping and SmallInt bitwise operators
  (`da79a3e7`); conditional expressions (`d6900b3d`); default arguments and
  closures (`c857fea2`); a GC safepoint on every loop iteration (`b159c48e`).
  The `Makefile` written by `--emit-make` no longer contains the original
  developer's paths: its include and library directories come from the build
  tree or the installation that runs `protopyc`, can be overridden with
  `PROTOPYC_INCLUDE_DIRS`, `PROTOPYC_LIBRARY_DIRS` and `PROTOPYC_CXX`, and are
  also passed as an RPATH; `make` honours `CXX`. Modules built this way can be
  imported by protopy: module-level names are now bound on the imported module
  instead of being lost.
- **Standard library location:** an installed protopy finds
  `<prefix>/<libdir>/protoPython/python3.14`, resolved relative to the
  executable; the compiled-in path used to be `../<libdir>/python3.14`, which
  the install rules never create. protopy no longer resolves that path against
  the working directory or searches parent directories (which could select
  CPython's `/usr/local/lib/python3.14`), and no longer exports
  `PROTO_PYTHONPATH=1`, which it then read back as a search directory `1`.
- **Embedding example:** `examples/embedding_sample.cpp` used functions that do
  not exist (`PythonEnvironment::initialize`, `ProtoSpace::getContext`, a
  four-argument `executeModule`). It is rewritten against the real
  `PythonEnvironment` API, built by default (`PROTOPYTHON_BUILD_EXAMPLES`) and
  run by CTest.
- **HPy handles are GC roots:** `HPyContext` kept handles in a plain
  `std::vector` that the collector does not scan, although its header
  described them as roots. Each context now pins its handles in its own
  `ProtoRootSet`; `HPy_Close` releases the pin and destroying the context
  releases the rest.
- **Struct sequences:** `sys.version_info` and `sys.implementation.version`
  were bare lists (`sys.version_info[:2]` printed `list[slice(None, 2, None)]`,
  `len()` was 0, `.major` and comparison with tuples failed); `sys.flags`,
  `sys.hash_info`, `sys.int_info`, `sys.thread_info` and `sys.float_info` were
  plain objects that returned `None` for any index; `os.stat_result` raised a
  C++ exception when sliced, and `tuple(os.stat(...))` never terminated because
  indexes past the end returned `None`. All of them, and `time.struct_time`,
  are now built by one helper (`newStructSequence`) as tuples of their visible
  fields with every field also available as an attribute, with CPython 3.14's
  field names and order: `version_info` has five fields, `sys.flags` 18 visible
  fields (`dev_mode` and `safe_path` are booleans), and `os.stat_result` ten
  visible fields whose indexes 7-9 are integer timestamps, plus the float
  `st_atime`/`st_mtime`/`st_ctime`, the `*_ns` timestamps, `st_blksize`,
  `st_blocks` and `st_rdev`. `os.fstat` returns the same object instead of a
  plain tuple. Unlike CPython, `type()` of these objects is `tuple` and their
  repr is a plain tuple repr.
- **REPL namespace and version strings:** `protopy -i` ran each statement in a
  new immutable object, so names bound by assignments, `def` and `import` were
  lost after the statement (from a terminal as well as from piped input) and
  `__name__` resolved to `builtins`. Statements now run in `__main__`, the same
  module `protopy -c` uses, and `PROTOPYSTARTUP` shares it. The REPL banner and
  `help()` reported version 0.1.0 and the banner ended with "[HPy Integrated]";
  the banner, `help()`, `protopy --help` and `sys.version` now take the version
  from a header generated from `project()` in `CMakeLists.txt`. The banner reads
  `protoPython 1.0.0 (Python 3.14 compatible)` and no longer includes the build
  date.
- **Containment and NaN:** containers now compare elements as CPython does,
  `x is e or x == e`. `nan in [nan]` and `nan in (nan,)` were False, as were
  `in` over a deque or any other iterable, `{1: nan} == {1: nan}` and equality
  of tuple subclasses holding the same NaN; `list.__contains__` and
  `tuple.__contains__` matched only identical objects, ints and strings, so
  `[2.5].__contains__(float('2.5'))` was False.
- **NaN in float subclasses:** an instance of a float subclass holding NaN
  compared equal to itself, and `<=`/`>=` between two such instances were True;
  they now follow IEEE 754 like a plain float.
- **NaN as a dict or set key:** NaN was hashed by its bit pattern, so every NaN
  was the same key (`len({float('nan'), float('nan')})` was 1). NaN now hashes
  by object identity, as in CPython 3.10 and later.
- **math.isclose:** `math.isclose(nan, nan)` was True, the `rel_tol` and
  `abs_tol` keywords were ignored (only extra positional arguments were read),
  and negative tolerances were accepted. It now follows CPython: NaN is close
  to nothing, the tolerances are keyword-only and a negative one raises
  `ValueError`.
- **json.dumps escaping and re:** `json.dumps("é\n")` returned the string
  unescaped, and so did `json.dumps('a"b')`: the native `re` module ignored a
  replacement function in `sub` (the pure-Python json encoder uses one) and
  matched the UTF-8 bytes of a str. `re` now matches code points, so match
  positions, groups and character classes such as `[^\ -~]` see whole
  characters; `sub` and `subn` accept replacement functions, expand `\1` and
  `\g<name>` templates and honour `count`; `Pattern.subn` exists; and
  `re.escape` leaves non-ASCII characters intact.
- **`__next__` of built-in iterators and heapq.merge:** calling `__next__`
  directly on an exhausted list, tuple, str, dict, set, range, zip, map,
  filter, enumerate or reversed iterator returned None instead of raising
  `StopIteration`, so `list(heapq.merge([1, 3], [2, 4]))` raised `TypeError`
  and loops that stop on the exception never ended. Such calls now raise
  `StopIteration`; `for` loops and `next()` still stop without creating one.
- **`import site`:** it raised `StopIteration`. `next(iterator, default)`
  let a `StopIteration` raised by `__next__` escape instead of returning the
  default (every generator given a default, including the one in
  `site.venv()`), and `next()` rejected iterators whose `__next__` is a Python
  method as "not an iterator". Behind that, `sys.copyright` was missing, and
  `ImportError` had no `name`, `path` or `name_from` attributes: the
  constructor ignored those keywords and the import system did not set `name`,
  so `site` printed errors for the absent `sitecustomize` and `usercustomize`
  modules. All three are fixed and `import site` completes silently.
- **platform.python_version():** it raised `ValueError: failed to parse
  CPython sys.version`. `sys.version` now has CPython's layout,
  `3.14.0 (protoPython 1.0.0, Apr 2026) [GCC 13.3.0]`, with the compiler that
  built the runtime in brackets, so `platform.python_version()`,
  `python_build()` and `python_compiler()` work.
- **pathlib:** `pathlib.Path` could not be called. A native `pathlib` module
  with a non-callable `Path` object was registered ahead of the standard
  library package; it is removed. Two runtime defects kept the package from
  working once it loaded: attribute lookups made inside another lookup (such
  as `self._drv` inside the `PurePath.drive` property) skipped data
  descriptors, so an unset `__slots__` attribute read as None instead of
  raising `AttributeError`; and native `os` functions (`stat`, `lstat`,
  `listdir`, `scandir`, `mkdir`, `rmdir`, `remove`, `rename`, `access`,
  `chdir`, `open`, `utime`, `readlink`) and `io.open` accepted only str paths,
  ignoring `os.PathLike` arguments (`os.stat(Path(...))` returned None, so
  `Path.exists()` was always True). Both are fixed.
- **copy of built-in subclass instances:** `copy.copy()` and `copy.deepcopy()`
  of an instance of a `tuple`, `str`, `bytes`, `int` or `float` subclass
  returned an empty or zero value (`()` for a tuple subclass). These types
  had no `__getnewargs__`, so the reduce protocol rebuilt the instance from
  no arguments; they now define it as CPython does.
- **Inherited comparison methods:** the comparison operators ignored
  `__eq__`, `__lt__` and the other rich comparison methods defined on a base
  class (`S() == S()` was False and `S() < S()` raised `TypeError` when only a
  base of `S` defined the method), so `pathlib.Path("a", "b") ==
  pathlib.Path("a/b")` was False. The methods are now looked up on the
  operand types through their MRO, not on the instances; a subclass right
  operand gets priority even when it inherits its reflected method, and an
  exception raised by a comparison method propagates.
- **OSError subclasses:** native OS calls (`os.stat`, `open`, `os.mkdir`,
  `os.remove` and the rest) raised a plain `OSError` whose only argument was
  the formatted message, without `strerror`, where CPython raises the
  subclass for the errno (`FileNotFoundError`, `PermissionError`,
  `IsADirectoryError`, ...). `OSError(errno, strerror[, filename])` now
  builds that subclass and sets `errno`, `strerror`, `filename` and
  `filename2`, with CPython's `args` and `str()`; native errors are raised
  through it. `ConnectionAbortedError`, `ConnectionRefusedError`,
  `ConnectionResetError` and `ProcessLookupError` were missing and are
  defined. `open()` for reading reports the real errno (it always reported
  ENOENT) and `IsADirectoryError` for a directory, `os.listdir` and
  `os.scandir` raise instead of returning an empty result, and uncaught
  exceptions print `str(exc)`.
- **Writing files:** `open(path, "w")` (and `"a"`, `"x"`, text or binary)
  returned an object that kept the written data in memory and discarded it
  on close, so nothing reached the file. These modes now open the file and
  write through its descriptor; `"x"` fails on an existing file and a text
  `write()` returns the number of characters. Descriptor-backed file
  objects (also `io.open(fd)`, used by `subprocess`) take their methods
  from a shared prototype, so `with` finds `__exit__` and closes them; it
  used to leave the descriptor open.
- **io.text_encoding:** it was a placeholder class, so calling it raised
  `TypeError` and `pathlib.Path.read_text` and `write_text` failed. It now
  returns an explicit encoding unchanged and, for None, `"locale"` (or
  `"utf-8"` in UTF-8 mode), as CPython does.
- **os.DirEntry.is_junction:** the method was missing, so `os.walk`,
  `shutil.rmtree` and the cleanup of `tempfile.TemporaryDirectory` raised
  `AttributeError`. It returns False, as CPython does outside Windows.
- **frozenset methods:** a method read from a frozenset instance was not
  bound to it: `frozenset([7]).__contains__(7)` returned False, `__len__()`
  0 and `__iter__()` None. Every frozenset carried the `__is_python_class__`
  class marker, so attribute lookup treated it as a class; the marker is no
  longer set on instances.
- **copy of set subclass instances:** `copy.copy()` and `pickle` rebuilt a
  `set` or `frozenset` subclass instance without its elements (for a
  `frozenset` subclass, after the fix above; before it the copy raised
  `TypeError`). `set` and
  `frozenset` now define `__reduce__` as CPython does, returning the class,
  the elements as a list and the instance state.
- **operator comparisons:** `operator.eq`, `ne`, `lt`, `le`, `gt` and `ge`
  converted both operands to C++ integers: with floats or strings they
  compared truncated or meaningless values, and with a float subclass
  instance they raised `RuntimeError: internal C++ exception`. They now
  compare as the operators do, including rich results and `TypeError` for
  unorderable operands.
- **operator.contains:** the function (and its alias `operator.__contains__`)
  was missing. It evaluates `b in a` with the `in` operator's semantics.
- **collections.deque.count:** the method was missing. It counts the
  elements equal to its argument, comparing by identity first as
  `list.count` does.
- **platform.python_implementation():** it returned `'CPython'`, while
  `sys.implementation.name` is `'protopython'`. `platform` recognises
  protoPython's `sys.version` and returns `'protoPython'`; the version,
  build and compiler fields are parsed as before.
- **sys.version build date:** the date in `sys.version` was the fixed text
  `Apr 2026`. CMake now generates the build date into `Version.h` when it
  configures the build (`Sep 15 2026`, CPython's format); it uses
  `SOURCE_DATE_EPOCH` when set, so builds can be reproducible.
- **ImportError messages:** every `ImportError` raised by the import system
  had `"\nSearch path: [...]"` appended, so `str(exc)` differed from
  CPython (`cannot import name 'y' from 'x' (<file>)`, `No module named 'x'`).
  The suffix is gone, and the error from `from x import y` also sets `path`
  to the module's file.
- **Non-ASCII strings:** `len()` and `ord()` counted code points, but
  indexing, slicing, iteration, `reversed()`, `find`/`index`/`rfind`/`rindex`/
  `count` and `startswith`/`endswith` offsets, `center`/`ljust`/`rjust`/
  `zfill`/`expandtabs` widths, and `repr()`/`ascii()` worked on UTF-8 bytes:
  `"é"[0]` was `''`, `list("é")` had two elements, `repr("é")` was
  `'\xc3\xa9'` and `json.loads('["é", 1]')` failed. They now use code points,
  through protoCore's code point string API where it provides one (indexing,
  slicing and iteration); searches still scan UTF-8 and convert offsets, with
  no conversion for ASCII strings. `repr()` keeps printable non-ASCII
  characters and escapes the rest, and the fill character of the padding
  methods may be any single character.

### Performance

Figures are quoted from the commit messages that report them; they were
measured on the development machine at the time. Full reports are in
`benchmarks/reports/`.

- A peephole pass fuses three accumulator and loop-test bytecode sequences
  into single opcodes (`OP_ACC_FAST_FAST`, `OP_INC_FAST_K`,
  `OP_LT_FAST_FAST_JF`, numbered 212 to 214). The commit reports a geometric
  mean of 6.62x → 4.72x the CPython 3.14t time; set `PROTOPY_NO_PEEPHOLE=1`
  to disable the pass (`48d81bbd`).
- Compiling names assigned in `while` bodies as locals: `nqueens` with N=9 went
  from 9.4 s to 1.56 s (`d7dfec38`).
- Removing per-call `getenv` from the interpreter hot path: `binary_trees`
  6640 ms → 2005 ms (`45a25c41`); every remaining runtime `getenv` is read once
  (`0515365b`).
- Release diagnostics compiled out, the initial-exec TLS model, and a 256-slot
  stack buffer for call-frame locals (`82a5dd08`, `4ceb8694`, `c011dee9`);
  together `call_recursion` went from 2704.40 ms to 98.70 ms (`c011dee9`).
- A per-thread type cache for `LOAD_ATTR` (`10ae9596`, `8e50cbe4`, `9c4f1578`);
  an inline cache for `LOAD_METHOD` (`cb6bb965`); string concatenation through
  protoCore rope append, `str_concat_loop` −39 % (`0d06e617`);
  single-allocation argument lists (`f1ac4149`, `ac4a2505`); name resolution
  without string conversion (`5bc4d228`).
- Instance attribute writes no longer maintain a key list: `binary_trees`
  −30 % (`6368ec16`).
- Dict construction is no longer quadratic: 2000 keys took 4.4 s before, 20000
  keys now take a few milliseconds (`3bf97555`).
- protopyc: inline SmallInt arithmetic and comparisons (`abe3aa30`), direct
  self-recursive calls (`ab8d673c`), and C++ `return` instead of exceptions
  for function returns, which took `fib(25)` from about 1.80 s to about
  0.24 s (`7d02dfcc`).
- The data-descriptor check of attribute lookups reads a class's `__mro__`
  tuple directly instead of through a nested lookup. The check has run at
  every nesting depth since the pathlib fix (`0073e0d5`), which cost 2.4 %
  more instructions on a property and `__slots__` micro-benchmark; with the
  direct read the same workload runs 6.3 % fewer instructions than before
  that fix, and the pyperformance subset and `attr_lookup` are unchanged.

- Chaining every context onto the thread's current context (`f84ec514`)
  costs at most 0.82 % instructions on call-heavy benchmarks (fib), with
  cycle differences of both signs within layout noise; under the 3 %
  threshold, so it stays as is. Measured with `benchmarks/perf_stat_ab.py`;
  report: `benchmarks/reports/2026-10-02-context-chain-cost.md`.

### Repository and documentation

- Build trees, profiler output and debug logs are no longer tracked
  (`52bd70c8`, `bc032b22`, `b3aefbcf`).
- Documentation restructured: audits moved to `docs/audits/`, the conformance
  history and obsolete plans moved to `docs/archive/`, `docs/README.md`
  rewritten as an index, and the conformance and debug scripts default to
  `build_release` (`2183208d`, `3f461d3c`).
- Conformity tooling: `tests/conformity/run_conformity.py` finds protopy in
  `build_release/`, `build/` or on `PATH` and passes the import directory
  through `PROTO_PYTHONPATH`; CTest runs the suite (`conformity_suite`) and the
  runner's self-test. `check_const_cast.sh` reports counts and exits 0 unless
  `--strict` is given.

## [0.3.0] - 2026-05-11

### CPython conformance sweep (test_descr.py)

Brought `test/cpython/test_descr.py` from **104 failing tests** (at the start
of the sweep) down to **73 issues** (54 failures + 19 errors out of 165 tests)
in three rounds of root-cause-only fixes.  Every change
was paired with `ctest --test-dir build-release`; ctest stayed at
**199/199** throughout.

Across those rounds the test_descr.py count improved from 104 → 73 via
~20 root-cause fixes spread across str unbound dispatch, dict()
strictness, bound-method ordering, complex hashing, type()
validation, py_object_new rules, OP_STORE_SUBSCR MRO walk, deque
trampolines, raiseIndexError, and more — every one paired with
ctest green.

### Fixed: str unbound dispatch sweep (May 2026)

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

### Fixed: 20-commit sweep — user __iter__ / __next__ + dispatch fixes + format minilanguage + datetime repr + mapping protocol

Seventh twenty-commit sweep of the 0.3.0 cycle.  Theme: get user-class
iterators and Python-level dunders to flow end-to-end through
the builtins.  Most of the round is wiring the same env->iter /
env->next + invokePythonCallable dispatch that the previous
rounds opened up — into the consumers that had bespoke
asMethod-only walks (sum, set, all, any, sorted, list.sort,
starmap, accumulate, islice, dict literal **).  Also: itertools
gains pairwise, PEP 378 thousands separators land, signed=
keyword on int.from_bytes / to_bytes, %s str-dispatch, str /
datetime / time / timedelta proper str via __mro__.

`ctest --test-dir build` stayed at **199/199** throughout.

Highlights:

- **str.istitle**: the only is*-family method missing from
  strPrototype.  Walks the bytes ASCII-only with a two-bit
  cased/saw-cased state machine.
- **datetime / time / timedelta __str__ + __mro__**: str(dt) etc.
  returned the generic `<X object at 0x...>` because the three
  types had __repr__ registered but no __mro__, so the dunder
  dispatcher walked an empty MRO and fell through to
  object.__repr__.  Added MRO chains and __str__ aliases.
- **bytes.removeprefix / removesuffix** reject str argument with
  the canonical "argument must be bytes-like, not str" — the
  bytes_view silent coercion was eating the str needle.
- **env->next** dispatches Python-level `__next__` via
  invokePythonCallable, not only native methods.  Fixes
  list(MyIter()), for x in MyIter(), set(MyIter()), and every
  downstream consumer that drives an iterator through env.
- **set() constructor** routes iteration through env->iter /
  env->next so user iterables work (previously `set(MyIter())`
  raised "not iterable").
- **sum()** drives iteration through env, accepts user
  iterables, mixes ints + floats correctly (`sum([1, 2.5])` ==
  3.5; was 1), and dispatches user-defined __add__ via
  getAttribute + invokePythonCallable.
- **all() / any()** route through env so `all(MyIter())` /
  `any(MyIter())` actually iterate (were always True / False).
- **sorted() / list.sort()** dispatch user-defined `__lt__` for
  arbitrary types — `sorted([C(3), C(1), C(2)])` was returning
  the input unchanged because protoCore's identity compare
  produced 0 for any two distinct user objects.
- **__ne__ derivation** invokes Python-level `__eq__` via
  invokePythonCallable when negation is needed; previously the
  `op == 1` fast path checked only `asMethod` and fell through
  to identity for any class whose `__eq__` was a Python method.
- **OP_SET_UPDATE** iterates user __iter__ for the `{*iterable,
  ...}` set-literal form.  `{*Src(), 'x'}` produced `{'x'}` only
  before; now produces `{'a', 'b', 'c', 'x'}`.
- **itertools.islice** honours `step` (the third positional
  was stored but never applied to the result iterator) and
  accepts user-class iterables.
- **NaN comparison**: `nan == nan` returned True (because
  identity short-circuited before float dispatch); add an IEEE
  754 NaN guard in both py_int_cmp and compareObjects.  The
  guard MUST run before the identity check or `x = float('nan');
  x == x` would silently match by pointer.
- **int.from_bytes / int.to_bytes**: honour `signed=` keyword.
  from_bytes flips the sign via 2^(8N) subtraction when the
  high bit is set; to_bytes rejects negatives when signed=False,
  encodes two's-complement when signed=True, and raises on
  overflow.
- **str.__mod__**: `'%s' % x` walks type(x).__mro__ for __str__
  before falling through to reprObject.  User classes with both
  __str__ and __repr__ now correctly render via __str__ for %s
  (the documented contract); %r remains reprObject.
- **itertools.starmap** unpacks tuple args correctly (was
  passing the tuple itself as one arg), accepts user iterables
  via env->iter, and dispatches lambdas via invokePythonCallable.
- **itertools.accumulate** honours the `initial=` keyword (3.8+):
  yields the initial value first AND seeds the running total.
- **itertools.pairwise** added (was missing entirely; the 3.10+
  helper that yields successive overlapping pairs).
- **str.format + int.__format__**: PEP 378 thousands separator
  (`{:,}` / `{:_}`) now inserts the separator every 3 digits
  for decimal types and every 4 for hex/oct/bin.  Earlier the
  separator was parsed but discarded.
- **OP_DICT_UPDATE** mapping protocol fallback: `{**M()}` /
  `f(**M())` produced `{}` for any user class with `keys()` and
  `__getitem__` because the OP_DICT_UPDATE handler only knew
  the native __keys__/__data__ shape and every Python class
  inherits an empty internal __data__ via dictPrototype.  Gate
  the fast path on `isInstanceOf(dictPrototype)` and add a
  keys()-based fallback for arbitrary Mapping objects.
- **str.encode(errors=)**: 'ignore' / 'replace' / 'strict'
  handlers actually apply to the ascii / latin-1 paths.  The
  errors keyword was being parsed but dropped, so every error
  defaulted to strict regardless of the explicit choice.

### Fixed: 20-commit sweep — range / slice protocol + f-string format spec + int/float parsing + py_print kwargs + bytes.hex/fromhex

Sixth twenty-commit sweep of the 0.3.0 cycle.  Theme: the sequence
protocol on `range` and `slice`, f-string format spec dispatch,
PEP 515 underscore parsing in `int()` / `float()`, missing
`print()` kwargs, and round-trip-correct `bytes.hex` /
`bytes.fromhex`.  `ctest --test-dir build` stayed at **199/199**
throughout.

Highlights:

- **`range` is finally a real sequence**:
  - `__getitem__` implemented for int, bool, slice, and the
    `__index__` protocol.  `range(10)[5]` returns 5, slicing
    builds a new range with the right (start, stop, step), and
    out-of-bounds raises `IndexError`.
  - `.start` / `.stop` / `.step` exposed as public attributes
    (CPython's read-only properties).
  - `.count(x)` / `.index(x)` implemented via a shared
    `range_locate` closed-form (`x = start + i*step`).
  - `__repr__` / `__str__` produce `range(0, 10)` /
    `range(start, stop, step)`; rangeClass also gained an
    `__mro__` so MRO-driven dunder lookups find `range.__repr__`
    before falling through to `object`.
- **`slice.indices(length)`** implemented (CPython sequence-mapping
  helper).  Normalises (start, stop, step) against a sequence
  length with the full CPython direction / bounds rules; raises
  `ValueError` on step==0 or negative length.
- **`str.format` field accessors** — `'{0[0]}'.format([10,20])`
  used to return `'[10, 20]'`; the dotted attribute and indexed
  access tail of the field reference are now walked.  `{0.real}`,
  `{x[key]}`, `{0[a][b]}` all work.
- **`bytes.startswith` / `endswith` accept a tuple of prefixes**.
  `b'hello'.startswith((b'xy', b'zz'))` returned True (any tuple
  matched); now iterates the tuple and returns True only when one
  alternative matches.
- **Exception hierarchy** — ArithmeticError and LookupError are
  now proper intermediate tiers, so `issubclass(ZeroDivisionError,
  ArithmeticError)`, `issubclass(KeyError, LookupError)`, and
  `issubclass(IndexError, LookupError)` return True (all returned
  False before).  `except ArithmeticError:` / `except LookupError:`
  catch the corresponding concrete errors.
- **`property.setter` / `.getter` / `.deleter`** — `@x.setter`
  produced a property whose `p.x` access returned the property
  object instead of firing fget.  py_property_copy_with dropped
  setAttribute returns AND nested the copy under the previous
  property (data-descriptor walk found `__set__` two levels up but
  the descriptor protocol short-circuited).  Both fixed: every
  `prop = prop->setAttribute(...)` rebinds, and the copy now
  parents directly off `self.__class__` (the property class).
- **`dict.update` rounding** — the previous round's commit broke
  `import enum` (mappingproxy's `__keys__` / `__data__` are
  inherited, not own, so the fast path was skipped).  Relax the
  detection to `getAttribute` and add a `keys()` mapping protocol
  branch before the iterable-of-pairs branch so dicts proxies and
  custom Mapping subclasses route correctly.
- **`int()` string parsing** — three correctness gaps:
  - `int('0x1F')` returned 31 (auto-detected prefix at base 10).
    Auto-detect now requires explicit base=0; default base=10
    rejects prefixes.
  - `int('1_000_000')` returned 1.  PEP 515 underscores between
    digits are now accepted; leading / trailing / consecutive
    underscores raise.
  - `int('0x1F')` (with auto-detect off) and `int('42.5')`
    silently returned 0 / 42.  Every digit is validated against
    the resolved base's alphabet — invalid chars raise
    `ValueError("invalid literal for int() with base N: '...'")`.
- **`float()` string parsing** — PEP 515 underscores accepted
  (rejecting placements adjacent to '.', 'e', 'E', or the sign).
  Trailing garbage now raises — `float('1.5abc')` was silently
  returning 1.5 because `std::stod` consumed only the prefix and
  the consumed count was never checked.
- **f-string format spec** — `f'{42:05d}'` returned `'42'` and
  `f'{3.14:.2f}'` returned `'3.140000'`; FormattedValueNode
  captured `format_spec` but the compiler dropped it.  Emit
  `format(value, spec)` so f-strings and `'{:spec}'.format(x)`
  share one code path.
- **`str.split` / `bytes.split` keyword args** — `maxsplit` and
  `sep` are now accepted as keyword arguments via
  `env->getCurrentKwNames()`.  `'a b c'.split(maxsplit=1)` returns
  `['a', 'b c']`; the bytes form was similarly broken (always
  splitting everywhere).
- **`datetime.date.__str__`** + `__mro__` — `str(date(2026,5,12))`
  emitted the generic `<date object at 0x...>` because the MRO
  walk on a class with no `__mro__` fell through to
  `object.__str__`.  Register `__str__` = `isoformat` and set
  `__mro__ = (date, object)`.
- **`print()` honours `__str__` / kwargs**:
  - For non-primitive types, walk type's MRO for `__str__` first;
    only fall back to reprObject when absent.  Previously
    `print(date(2026,5,12))` showed `datetime.date(2026, 5, 12)`
    (the repr).
  - Honour `sep`, `end`, and `file=sys.stderr` keyword arguments.
    Buffer the rendered output in an ostringstream so the final
    destination is selected at flush time.
- **`bytes.fromhex`** tolerates whitespace between byte pairs.
  `bytes.fromhex('00 01 02')` returned `b'\x00\x02'`; the parser
  stepped by 2 and silently dropped one nibble whenever a space
  landed on an odd index.  Now raises `ValueError` on invalid
  chars or a dangling nibble.
- **`bytes.hex(sep, bytes_per_sep=1)`** accepts the separator and
  group-size arguments (positive → group from the right, negative
  → group from the left), making it round-trip compatible with
  the fixed `bytes.fromhex`.

### Fixed: 21-commit sweep — format minilanguage + __index__ protocol + iter validation + dict.update iterables + bool spelling

Fifth twenty-commit sweep of the 0.3.0 cycle (21 commits in this round).
Theme: the PEP 3101 format minilanguage, the `__index__` protocol on
all four sequence subscripts, tighter error fidelity on bytes
methods, and one regression cleanup for dict.update against
iterables.  `ctest --test-dir build` stayed at **199/199** across
every commit.

Highlights:

- **`int.__format__`** implements the full PEP 3101 minilanguage —
  fill / align (`<`, `>`, `^`, `=`), sign (`+`, `-`, ` `), `#`
  alternate form, `0` zero-pad, width, and type (`d`, `b`, `o`, `x`,
  `X`, `c`, `n`).  Previously the spec was ignored, so `format(42,
  'x') == '42'` instead of `'2a'`.  Subclasses of `int` and the
  `True` / `False` singletons unwrap correctly through `format()`.
- **`str.__format__`** adopts the str-side subset (fill / align /
  width).  Without it, `format('hi', '<5')` was returning `'hi'`.
- **`format()` builtin** unwraps int / float subclass instances via
  `__data__` and routes `True` / `False` through the integer fast
  path — previously `format(MyInt(42), 'x')` was `None` and
  `format(True, 'd')` raised the internal C++
  `"Object is not an integer type"` panic.
- **`__index__` protocol** is now dispatched by `list[idx]`,
  `tuple[idx]`, `str[idx]`, and `bytes[idx]` whenever the index is a
  user-class instance with a `__index__` method.  CPython contract:
  any object that defines `__index__` is a valid sequence subscript.
- **`int / float`** carry the missing numeric ABC dunders
  `__round__`, `__floor__`, `__ceil__`, `__trunc__` so the standard
  library's `Real` /`Integral` checks succeed and `math.floor(x.f)`
  works for user instances that delegate to the numeric primitives.
- **`float.as_integer_ratio()`** gcd-reduces the returned (num, den)
  pair and preserves the original sign — `(-2.5).as_integer_ratio()`
  is now `(-5, 2)`, not `(5, 2)` (it was being double-negated through
  `frexp`).
- **`dict` ordering** (`<`, `<=`, `>`, `>=`) raises `TypeError` like
  CPython.  The previous behaviour silently compared by hash and
  produced arbitrary results.
- **`bytes` strictness sweep**:
  - `bytes.__contains__` rejects raw `str` needles with the canonical
    `"a bytes-like object is required, not 'str'"`.  Was silently
    coercing the str needle and returning bogus results.
  - `bytes.find / rfind / count / index / rindex` and
    `bytes.startswith / endswith` reject `str` arguments with the
    same message — every byte-input method now follows the same
    bytes-only protocol.
- **`UNPACK_SEQUENCE` error messages** embed the expected and
  actual counts: `"not enough values to unpack (expected 3, got 2)"`
  and `"too many values to unpack (expected 2)"`, mirroring CPython
  word for word.
- **`bytes[True]` / `bytes[False]`** accept bool as an integer index
  (last sequence type that was missing the bool-as-int handling).
- **`ord()`** accepts a length-1 `bytes` / `bytearray` argument and
  returns the single byte's integer value.  Previously only str was
  accepted, with `ord(b'A')` falling through to attribute walking and
  returning bogus values.
- **`iter()`** validates that a user-defined `__iter__` returns an
  iterator (an object with `__next__`).  Previously `iter(obj)` could
  return an arbitrary value when `__iter__` returned None or a
  non-iterator instance.
- **`py_print`** mirrors `py_float_format_short`'s decimal-window
  cleanup from commit ae63e77f — `print(1200.0)` is now `'1200.0'`
  instead of `'1.2e+03'`.  Without it the `repr` / `__str__` path got
  the fix but bare `print` of a float didn't.
- **`dict.update(iterable_of_pairs)`** and **`dict.update(**kw)`**
  finally work.  The previous implementation only handled the
  mapping-with-`__keys__`/`__data__` shape and silently produced `{}`
  for `d.update([('a', 1), ('b', 2)])` and friends.  Iterable
  elements unpack as (key, value) tuples (or lists) of length two —
  malformed pairs raise the canonical `ValueError` /`TypeError`.
  Keyword arguments are recovered via `env->getCurrentKwNames()` and
  merged after the positional argument so kwargs override matching
  keys.
- **`bool.__str__` / `bool.__repr__`** spell `'True'` and `'False'`.
  Without dedicated bool dunders, `str(True)` walked MRO up to
  `int.__str__` (py_int_repr), which doesn't recognise the
  PROTO_TRUE / PROTO_FALSE singletons as integers and rendered them
  as `'0'`.  Only `print(True)` happened to be correct because
  py_print intercepts the singletons specially.

### Fixed: 20-commit sweep — bytes-aware constructors + bool subclass parity + None-key dict + complex parser

Fourth twenty-commit sweep of the 0.3.0 cycle.  Theme: round out the
int/bool subclass story, add missing parser branches, and close
a handful of correctness gaps around hash-keyed lookups.

Highlights:

- **`hash(True)` / `hash(False)`** short-circuit to `1` / `0` so they
  agree with `hash(1)` / `hash(0)`.  Previously they fell through to
  identity hash and broke dict / set bucketing for bool/int collisions.
- **`+True` / `+False`** return int `1` / `0` (not bool) — `type(+True)
  is int`.  The identity fast path returned the bool sentinel.
- **`list[True]` / `tuple[True]` / `str[True]`** accept bool as a valid
  index (subclass-of-int).  Was raising
  `TypeError: list indices must be integers or slices`.
- **`divmod(True, 2)`** now works — promotes bool operands to int 1/0.
- **`int(bytes)` / `float(bytes)`** parse ASCII digits from a bytes /
  bytearray:  `int(b'123') == 123`, `float(b'-5.5') == -5.5`.
- **`complex('1+2j')`** full grammar parse: bare 'j' / '+j' / '-j' →
  ±1j; surrounding parentheses stripped; scientific notation in
  either component.  Previously only the real part was read.
- **`complex('abc')` / `complex([])`** raise typed errors
  (`ValueError: complex() arg is a malformed string`,
  `TypeError: complex() first argument must be a string or a number, not 'list'`).
- **`pow(0, -n)` / `0 ** -n`** raise `ZeroDivisionError`.  Previously
  the float path returned `inf` and a downstream op could hang.
- **`slice()`** requires at least 1 arg (TypeError); repr always shows
  three components: `slice(None, 5, None)`, `slice(1, 10, 2)`.
- **`frozenset` repr** — `frozenset()` and `frozenset({1, 2, 3})`
  instead of `<frozenset object at 0x…>`.
- **`str.rfind` / `str.count`** raise `TypeError("substring must be
  str")` / `TypeError("must be str, not int")` for non-str needles
  (mirror `str.find`'s existing strictness).
- **`str.split` / `rsplit` / `replace`** validate maxsplit / count as
  integer (or bool).  Non-int silently fell back to default.
- **`round(3.5, 1.5)` / `round(3.5, 'a')`** raise `TypeError` instead
  of crashing with the internal C++ panic
  `"Object is not an integer type"`.
- **`len()` validates `__len__` return** — negative raises ValueError,
  non-int raises TypeError, matching CPython's protocol.
- **`map(func, *iterables)`** supports the N-iterable form via splat
  call.  Previously only the first iterable was used.
- **`iter(callable, sentinel)`** — the 2-arg form is now implemented;
  builds a native iterator that calls callable() and stops at sentinel.
- **`ascii()`** routes through `reprObject` so primitives (`int`,
  `str`, `None`) repr correctly.  Previously `ascii('abc')` returned
  `"<class 'str'>"`.
- **`float` repr / `str(float)`** prefer decimal notation for
  `|val|` in `[1e-4, 1e16)`, matching CPython.  `repr(1e10)`
  becomes `'10000000000.0'` (was `'1e+10'`).
- **`{None: 1}` repr / `.values()` / `.items()`** return `1` /
  `[1]` / `[(None, 1)]` (was `None` / `[None]` / `[(None, None)]`).
  Root cause: dict views used `key->getHash()` but setitem stored
  with `dictKeyHash()`; reunify on the latter.

### Fixed: 20-commit sweep — operator strictness + correctness gaps

Third twenty-commit sweep of the 0.3.0 cycle.  Targets operator-form
strictness for built-in types (where the named method accepts loose
inputs but `+` / `*` / `|` / `&` / etc. should require matching
types) and a couple of deeper correctness bugs (floor div/mod
semantics, str.center fill validation).

Notable items:
- **`int // int` and `int % int` floor semantics** — protoCore
  truncates toward zero (C semantics); CPython requires floor toward
  −∞.  `-5 // 2` now returns `-3` (was `-2`); `-5 % 2` now returns
  `1` (was `-1`).  Invariant `a == (a // b) * b + (a % b)` holds for
  every (a, b) ∈ [-7..7] × [-3..-1, 1..3].  Bignum-safe.  Followed
  by a fast-path commit that skips the adjustment when operands
  share a sign (3-4× faster on int-heavy code).
- **`str.center` / `ljust` / `rjust` / `zfill` fill validation** —
  multi-char or empty or non-str fill silently fell back to space.
  Now raises `TypeError("The fill character must be exactly one
  character long")`.  zfill width raised the internal C++ panic
  instead of TypeError when the width was non-int — both fixed.
- **`list.pop` / `list.insert` / `range`** — non-int index / count /
  step crashed with the internal C++ panic.  Now raises
  `TypeError("'X' object cannot be interpreted as an integer")`.
  `range(True, 5)` and `range(False, True)` now work (bool is int).
  `range(stop=10)` raises `TypeError: range() takes no keyword
  arguments`.
- **`bytes()` / `bytes` + / *** — `bytes('abc')` was crashing on
  the asLong path; now raises the standard `string argument without
  an encoding`.  `bytes([1, 2, 'a'])` and `bytes([1, 2, 300])`
  raise typed TypeError and ValueError.  `b'ab' + 'cd'` no longer
  silently encodes — returns NotImplemented for the operator path so
  the dispatcher emits the canonical TypeError.
- **`set` operators `&`, `|`, `-`, `^`** — accepted any iterable
  on the rhs and produced a set silently (`{1,2} & [1,2] == {1,2}`).
  Now restricted to set / frozenset / dict_keys / dict_items via the
  NotImplemented sentinel path.
- **`dict | non-dict`** — returned the self dict unchanged.  Now
  raises `TypeError("unsupported operand type(s) for |: 'dict' and 'X'")`.
- **`zip(strict=True)`** — previously ignored the kwarg.  Now raises
  `ValueError("zip() argument N is shorter/longer than argument 1")`
  when iterables disagree in length.
- **`min` / `max` / `sorted` / `list.sort` / `filter`** — non-callable
  `key=` argument raised TypeError instead of silently passing values
  through.  Argument-count validation for filter() too.
- **`int.conjugate()`** — installed as a method on intPrototype
  (returns self).  Numeric ABC contract.
- **Error message polish** — `len(5)` now says `object of type 'int'
  has no len()` instead of embedding the literal value
  `'5' has no len()`.  `int()` / `float()` ValueError quotes the
  offending literal and uses CPython's exact wording.  `iter()` and
  `'in <string>'` errors name the operand type.
- **`exception_repr`** renders args via `reprObject` instead of the
  placeholder `<obj>`; `KeyError(3)` now prints as `KeyError(3)`.
- **__slots__ accepts `__doc__`** — exempt the special-cased
  docstring-populated slot; otherwise `_typing._SpecialForm` failed
  to load and cascaded into typing/asyncio/unittest.mock imports.
- **str.encode validates encoding type** — non-str / None now raise
  `TypeError("str.encode() argument 'encoding' must be str, not X")`.

### Fixed: 20-commit sweep — silent-None / silent-zero → proper exceptions

Another twenty back-to-back root-cause fixes, every one paired with
ctest 199/199.  Theme: replace silent PROTO_NONE / asLong panic /
extractDouble-returns-0 paths with CPython-shaped exceptions that
name the operand type and tell the user what went wrong.

- **`abs(non_numeric)`** → `TypeError("bad operand type for abs(): 'str'")`
  (was: silent None).
- **`a[::0]` for list/tuple/str/range/del** → `ValueError("slice step
  cannot be zero")` (was: silent normalise step→1 and return the whole
  thing).
- **`format(value, non_str_spec)`** → `TypeError("format() argument 2
  must be str, not int")` (was: silent unformatted value).
- **`round()` / `round(non_numeric)` / `round(True)`** →
  `TypeError("round() missing required argument …")` /
  `TypeError("type str doesn't define __round__ method")` / correct
  bool→int return (was: silent None / silent 0 / wrong 0).
- **`int(bad_string)` / `float(bad_string)`** error text quotes the
  offending literal and uses CPython's exact wording
  (`could not convert string to float: 'abc'`).
- **`'%d' % 'a'`** and every other integer / float / char `%`-format
  → `TypeError("%d format: a number is required, not str")` (was:
  silent 0 / 0.0 / internal C++ panic for `%c`).
- **`'%s %s' % ('only one',)`** → `TypeError("not enough arguments for
  format string")` (was: rendered "None" for the missing slot).
- **`[1,2] * 2.5` / `(1,2) * 'a'`** → CPython's `unsupported operand
  type(s) for *` via the NotImplemented sentinel (was: silent None).
- **`iter()` / `iter(5)`** → `TypeError("iter expected at least 1
  argument, got 0")` / `TypeError("'int' object is not iterable")`
  with the operand type embedded (was: silent None / bare message).
- **`sum(['a','b'], '')`** → `TypeError("sum() can't sum strings [use
  ''.join(seq) instead]")` (was: int-only accumulator silently
  produced 0).
- **`range(0, 10, 0)`** → `ValueError("range() arg 3 must not be
  zero")` raised at construction (was: silent None → internal C++
  panic on iteration).
- **`enumerate(it, 'abc')` / `enumerate(it, start=1.5)`** → typed
  TypeError (was: silently dropped, start stayed 0).
- **`next(map(non_callable, …))`** → `TypeError("'X' object is not
  callable")` (was: silently passed through the value).
- **`5 in 10` / `'x' in None`** → `TypeError("argument of type 'int'
  is not iterable")` (was: silently False).
- **`repr(KeyError(3))`** → `"KeyError(3)"` (was: `"KeyError(<obj>)"`
  with non-string args replaced by a placeholder).
- **`del l[10]` past end-of-list** → `IndexError("list assignment
  index out of range")` (was: silent no-op).
- **`bin('a')` / `oct('a')`** → `TypeError("bin() argument must be an
  integer or have an __index__ method")` (was: silent None; only
  `hex` raised).
- **`pow()` / `pow(2)`** → `TypeError("pow expected at least 2
  arguments, got N")` (was: silent None).
- **`list.insert('a', 0)`** → `TypeError("'str' object cannot be
  interpreted as an integer")` (was: internal C++ panic
  "Object is not an integer type").

Sibling fix to the prior round: `exception_repr` now resolves the
class via `env->getType` so `repr(ValueError(...))` shows
`ValueError(...)` rather than `type(...)`.

### Fixed: 20-commit sweep — error fidelity + missing dunders

Twenty back-to-back root-cause fixes for divergences from CPython's
public surface, every one verified by probe and with ctest 199/199
throughout.  Highlights:

- **`exception_repr` resolves the actual class via `env->getType`** —
  the previous `instance->getAttribute("__class__")` walk reached
  the exception class's `__class__` slot (= `type`) because
  `exception_init` never set an own `__class__`, so every
  exception printed as `type('msg')` rather than
  `ValueError('msg')`, `KeyError('k')`, etc.
- **`dict.fromkeys(non_iterable)`** now raises
  `TypeError("'X' object is not iterable")` instead of silently
  returning `None`.
- **`set(non_iterable)`** now raises the same `TypeError` instead
  of silently returning `set()`.
- **`reversed(obj)`** error message names the type
  (`'int' object is not reversible`) instead of embedding the
  receiver's `repr` (`'5' object is not reversible`).
- **`isinstance`/`issubclass` arg2 validation** — both reject
  non-class, non-tuple, non-union arguments with TypeError instead
  of silently returning `False`.  Includes `__args__`-driven union
  detection so `isinstance(x, int|str)` keeps working.
- **`getattr` / `hasattr` / `setattr`** raise TypeError when the
  attribute name is not a string.
- **`any` / `all`** evaluate truthiness via `env->isTrue` so user
  `__bool__` / `__len__` dunders run.
- **`enumerate(it, start=N)`** honours the `start` keyword.
- **`min` / `max`** raise on empty iterable with no default, accept
  the `default=` kwarg, and dispatch `key=` correctly.
- **`ord` / `chr`** raise the standard `TypeError` / `ValueError`
  with CPython-matching messages.
- **`range.__reversed__` / `dict.__reversed__` / `str.__reversed__`**
  installed for direct unbound dispatch and reverse iteration.
- **`set` update / `isdisjoint` / `intersection_update` /
  `difference_update` / `symmetric_difference_update`** added.
- **`math.factorial`** raises `TypeError` on non-integral input,
  not `RuntimeError`.
- **`dict.popitem`** routes through `dictKeyHash` so custom
  `__hash__` types pop correctly.
- **`dict.pop` / `set.remove`** raise `KeyError`, not bare
  RuntimeError or ValueError-tagged-as-KeyError.

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

### Fixed: str subclass type() + dict-key hash dispatches __hash__

Two coupled bugs that together broke every str-subclass test:

1. `getType` on a str-subclass wrapper returned `str` instead of
   the subclass.  `ProtoObject::isString` returns true for both
   raw string primitives AND wrapper Objects whose `__data__` is
   a string — so the `obj->isString()` branch folded every wrapped
   subclass instance back to `strPrototype`.  Detect the wrapper
   shape (POINTER_TAG_OBJECT with own `__data__`) and prefer the
   wrapper's own `__class__` / first parent before falling back
   to strPrototype.

2. Dict bucketing used `key->getHash()` everywhere, which for a
   str-subclass returns the underlying string's hash and ignores
   any user `__hash__` override.  cistr's case-insensitive
   `__hash__` was therefore silently bypassed: storing
   `cistr('TWO'): 1` and looking up `d[cistr('two')]` landed in
   different buckets despite agreeing on `__eq__` and `__hash__`.
   `dictKeyHash` now consults `type(key).__hash__` when the type
   is not a built-in primitive (whose stored `__hash__` already
   matches the protoCore hash by construction).  Exposed it as
   `pyDictKeyHash` so OP_BUILD_MAP / OP_MAP_ADD use the same
   bucketing as py_dict_getitem / setitem.

Effect: `class cistr(str)` instances now report `type(s) == cistr`,
case-insensitive dict lookups land in the right bucket, and
`test_str_subclass_as_dict_key` passes.

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
