# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Changes on `main` since the 0.3.0 entry, from commit `038eb6ea` (2026-05-12)
onwards. Commit hashes are given for reference.

> **Version numbering.** `CMakeLists.txt` declares version 1.0.0. The version
> was raised from 0.2.0 to 1.0.0 in `fd29013c` (2026-04-20), the commit tagged
> `v1.0.0`; the only other tag is `v0.2.0` (2026-02-13). This file has no
> `[1.0.0]` entry. The `[0.3.0]` entry below was added on 2026-05-10
> (`9fa74ad1`), after the `v1.0.0` tag, and has no matching tag or CMake
> version.

### Added

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
