# protoPython Design Decisions

This document consolidates the main architectural decisions for onboarding new collaborators. Each section is brief; see the linked documents for details.

---

## Minimal Bytecode Executor

**Decision:** protoPython is a minimal bytecode executor built inside protoPython, not a fork of CPython with a swapped eval loop.

**Rationale:** Keeps all runtime code in protoPython; no CPython build dependency. The executor is GIL-less by design from the start. Enables incremental delivery: run scripts via `PythonEnvironment`, resolve modules, execute bytecode through `ProtoContext`.

**Reference:** [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md)

---

## L-Shape Architecture

**Decision:** Single `ProtoSpace` per process; M:N thread mapping (each OS thread backed by a `ProtoThread`); explicit context stack with RAII; lock-free hot path.

**Rationale:** One memory topology simplifies ownership. O(1) context lookup via thread-local storage. Deterministic cleanup when contexts are destroyed. Work-stealing readiness: `ProtoSpace::threads` registry allows future scheduler integration without protoPython changes.

**Reference:** [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md)

---

## GIL-Free by Design

**Decision:** No `std::mutex` or `std::lock_guard` in the bytecode interpreter hot path. Context lookup uses thread-local storage. protoCore handles GC and allocation; its internal locks are outside the L-Shape mandate for protoPython.

**Rationale:** True parallel execution. Each thread runs independently. User-level locks (`_thread.allocate_lock`) and per-deque mutexes are Python API features, not a runtime GIL.

**Reference:** [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md), [POST_REFACTOR_AUDIT.md](POST_REFACTOR_AUDIT.md)

---

## Zero-Copy Object Promotion

**Decision:** Return values are promoted by pointer transfer. `ctx->returnValue` is set before context destruction; the `ProtoContext` destructor moves the object to the parent via `addCell2Context`. No memcpy.

**Rationale:** Eliminates copy overhead on function return. Objects allocated in a callee context are either promoted to the caller or reclaimed by GC when the context is destroyed.

**Reference:** [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md) §4

---

## Immutable-by-Default

**Decision:** Shared state uses Copy-on-Write. Python objects map directly to protoCore cells (e.g. `int` → `Integer`, `str` → `ProtoString`, `list` → `ProtoList`). No CPython API wrappers; zero-copy data flow as proto objects only.

**Rationale:** Thread safety by design. Structural sharing and CoW reduce contention. Aligns with protoCore's memory model.

**Reference:** [REARCHITECTURE_PROTOCORE.md](REARCHITECTURE_PROTOCORE.md)

---

## HPy for C Extensions

**Decision:** Use an [HPy](https://hpyproject.org/)-style handle API for native extension modules instead of the CPython C API. protoPython implements a C++ subset of that API (`include/protoPython/HPyContext.h`) and a loader, `HPyModuleProvider`, registered in module resolution (see [HPY_USER_GUIDE.md](HPY_USER_GUIDE.md)).

**Rationale:** CPython's C API exposes reference counting and assumes a GIL. HPy provides handles, type/attribute/call operations, and number/sequence protocols without those assumptions, so extensions can fit the GIL-less runtime.

**Reference:** [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md)

---

## Type Representations and Metadata

**Decision:** Standardize `__repr__` and `__str__` lookups in `PythonEnvironment`; include metadata like `co_name` in code objects for improved introspection. Ensure `type(None)` correctly returns `NoneType`.

**Rationale:** Improves REPL UX and diagnostic clarity. `co_name` allows generators and functions to report their actual names in tracebacks and `repr()`. Standardizing dunder lookups ensures consistency across builtin and user-defined types.

**Reference:** [INTERNALS_DEEP_DIVE.md](INTERNALS_DEEP_DIVE.md#5-metadata-and-prototyping)

---

## Asynchronous comprehensions follow `async for` (agent, pending review)

**Decision (2026-10-25):** `[x async for x in it]` compiles as the `async for`
statement does: `GET_AITER`, then `FOR_ITER`. The earlier lowering (an awaited
`GET_ANEXT` step inside a protected range that catches `StopAsyncIteration`,
CPython's shape) never ended, and could not work as written: the runtime's
async generators return values, not awaitables, from `__anext__`.

**Rationale:** Least surprise: a comprehension behaves exactly like the loop
it abbreviates. The limitation both now share, synchronous asynchronous
iteration, is listed in docs/VM.md section 6 and needs its own design
(async generators whose `__anext__` returns an awaitable).

---

## `match` keeps its values on the operand stack (agent, pending review)

**Decision (2026-10-25):** `compileMatch` / `compilePattern` keep the
subject and every candidate on the operand stack, as CPython does, and a
failing test jumps to a chain of `POP_TOP`s that clears what it left. The
built-in calls (`isinstance`, `len`, ...) get NULL markers.

**Rationale:** Named temporaries (`__match_subj`, `__match_tmp_<n>`) were
visible in every namespace and, inside a function, were globals; a hidden
local slot would not exist at module or class level. The stack needs neither.
Capture names are bound in the enclosing scope (`collectDefinedNames`).

---

## File reads and str method results are collectable, not interned (agent, pending review)

**Decision (2026-10-26):** Read-mode file results (`read`, `readline`,
`readlines`, iteration) and the str methods that compute data (`split`,
`rsplit`, `splitlines`, `strip` family, `partition`, `replace`, case
conversions, `format`, `%`, f-string assembly in `buildString`,
`bytes.decode`, `bytes.hex`, number reprs) return `PythonEnvironment::newStr`
instead of `getInternedString(...)->asObject()`.

**Rationale:** Interning computed data made every distinct line permanent and
grew protoCore's intern table with the input; each later lookup in that table
(including the attribute-name lookups every native method does) slowed down,
so iterating a 200,000-line file took 83.6 s instead of about 1 s. Equality,
hashing, dict keys, `getattr` and keyword arguments do not depend on interning
(setAttribute / getAttribute intern names themselves), which
`test/regression/file_lines_not_interned.py` and `re_results_not_interned.py`
check. A side effect is a fix: `c_str()` cut a string at its first NUL
character; `newStr` keeps it. Name-like strings (module, attribute, type
names, exception messages) still go through `getInternedString`.

---

## The re module runs on PCRE2 (agent, pending review)

**Decision (2026-10-26):** `src/library/ReModule.cpp` implements `re` over
PCRE2's 8-bit library with its JIT, replacing `std::wregex`. A Python pattern
is translated to PCRE2 syntax by the module's own parser, which raises
`re.error` with CPython's message and position for everything CPython
rejects. The choices that are not obvious:

- **One library, UTF-8 for str.** str patterns compile in UTF mode
  (`PCRE2_UTF | PCRE2_MATCH_INVALID_UTF`, plus `PCRE2_UCP` unless re.ASCII);
  bytes patterns compile in byte mode. The subject is protoCore's UTF-8, so no
  re-encoding is needed; positions are mapped back to code points through an
  offset table built only for non-ASCII subjects. The 32-bit library would
  need a conversion per call and a second library to link.
- **The translation output is ASCII.** Every character that is not a letter,
  digit or `_` is written as `\x{...}`, so no pattern character can take a
  PCRE2 meaning Python does not give it. PCRE2-only syntax (`(?R)`, `(?1)`,
  `(?&n)`, `(?|`, `(*VERB)`, `\K`, `\Q..\E`, `\p{..}`) is rejected the way
  CPython rejects it.
- **Python's character sets, spelled out.** `\w` is `[\p{L}\p{N}_]`
  (`str.isalnum()` or `_`), `\s` is the 29 code points `str.isspace()`
  accepts, `\d` is `\p{Nd}`; re.ASCII sets are written as ASCII ranges, so a
  scoped `(?a:...)` or `(?u:...)` works whatever PCRE2_UCP says. `\b` is
  PCRE2's own where its word notion is Python's for the scope, else spelled
  out with lookarounds. PCRE2's `\W` remains only inside a character class
  in Unicode mode (no class body is the complement of `[\p{L}\p{N}_]`); from
  PCRE2 10.43 it also counts combining marks and connector punctuation as
  word characters.
- **re.ASCII | re.IGNORECASE on str is emulated.** PCRE2 folds non-ASCII
  letters in UTF mode even without UCP; the translator writes `[aA]` and the
  other-case ranges of a class, and PCRE2 matches case-sensitively in that
  scope. A case-insensitive back reference cannot be emulated that way and is
  `re.error`.
- **Errors, never silent differences.** Backtracking beyond PCRE2's match,
  depth, heap or JIT-stack limits is `RecursionError` (CPython takes
  exponential time instead). Repeat counts above 65535 (PCRE2's limit;
  CPython allows 4294967294), `\N{...}` (no Unicode name database) and the
  look-behind width limit are `re.error`. A str subject containing a lone
  surrogate is `ValueError`, because PCRE2 would skip it as invalid UTF-8; a
  surrogate in a pattern compiles to a class that matches nothing, which is
  then exact. The compiled-pattern size is PCRE2's (64 KiB with the usual
  link size 2): CPython's `test_big_codesize` pattern is `re.error` here.
- **Compiled code per pattern, three variants.** Search, match (compiled
  `PCRE2_ANCHORED`) and fullmatch (`PCRE2_ANCHORED | PCRE2_ENDANCHORED`),
  the last two compiled on first use under `std::once_flag`; match-time
  anchoring would bypass the JIT. The code is owned by the Pattern object
  through an external pointer whose finalizer frees it. Module-level calls
  go through a cache of 512 patterns (a ProtoSparseList on the module, keyed
  by protoCore's content hash and checked against the text), as CPython's.
- **Per-thread state with `thread_local`.** Match data, match context and
  JIT stack (32 KiB growing to 8 MiB) are per thread, created on first use;
  no native thread API is called. Each thread also keeps its last long str
  subject (4 KiB or more) converted to UTF-8, pinned by a module slot only
  that thread writes, so `pattern.match(text, pos)` loops stay linear.
  Subjects of 64 KiB or more are matched inside an `UnmanagedScope`.
- **Python's empty-match rule** (an empty match may not follow another at the
  same position) is `PCRE2_NOTEMPTY_ATSTART` after an empty match.
- **Results.** Group strings are slices of the subject (`getSlice`: they may
  share its rope nodes), computed when asked for; a Match holds code point
  spans. Match and Pattern objects are immutable. `lastindex` is the group
  that closed last (CPython's definition), computed from the group tree.

**Known gaps:** flags are plain ints (no `re.RegexFlag`), `Match.regs`,
pickling of patterns, `BufferError` for a resized buffer, the DeprecationWarning
for positional `count` / `maxsplit` / `flags` and the FutureWarning for
possible set operations are absent; memoryview and array subjects are copied
through `tobytes()` and their groups are bytes. `re.LOCALE` is accepted for
bytes and behaves as ASCII.

**Rationale:** The wregex implementation recompiled the pattern on every
call (47.5 us per search where CPython takes 0.5 us), had no `re.error`, no
bytes support, a wrong `lastindex` and a broken Scanner. PCRE2 is the engine
protoClojure already uses (same CMake resolution: pkg-config, CMake package,
else the pinned 10.46 release built static), JIT-compiled, and its semantics
are close enough to SRE that the differences can be translated or rejected
explicitly. CPython's own test_re.py (run case by case, its unittest loader
does not work on protopy) passes 119 of 151 runnable cases, against a crash
before; the 403-entry re_tests table passes except for 5 entries that fail on
protopy string-literal bugs, not on re.

---

## Lists are recognised by `listStorage`, never by `asList` (agent, pending review)

**Decision (2026-10-26):** protoPython code asks `protoPython::listStorage(ctx, obj)`
(include/protoPython/PythonEnvironment.h) for the ProtoList behind a value,
instead of protoCore's `ProtoObject::asList`. The helper returns the same
list for a bare ProtoList or a wrapper's `__data__`, and nullptr for a
string, bare or as `__data__`. Every `obj->asList(ctx)` on a `ProtoObject`
in the runtime was rewritten mechanically; the calls left are on
`ProtoTuple`, `TupleView`, `ProtoArguments` and similar receivers.

**Rationale:** `asList` answers a string with a new list of its characters.
Used as a type test, that made `isinstance("abc", list)` True, let a str
into every list-only path (`[1] + s`, `l.extend(s)`, unpacking) as protoCore
unicode-char cells, which are not Python objects, and turned `len(s)` into an
O(n) copy. No caller wanted that conversion, so a protoPython-side helper is
the whole fix; protoCore's `asList` is left as it is. The helper relies on
pointer tag 0 meaning "object cell", as `isStringTagFast` and existing
protoPython code already do.

---

## Unicode character names come from a generated, compiled-in table (agent, pending review)

**Decision (2026-10-26):** `\N{NAME}` escapes resolve through
`protoPython::unicode_names::lookup` (src/library/UnicodeNames.cpp) over
`src/library/UnicodeNameTable.inc`, which `tools/gen_unicode_names.py`
generates from CPython's `unicodedata` and the Unicode `NameAliases.txt` of
the same version (16.0.0 for CPython 3.14). The generated file is checked
in. Names are stored as word numbers into a frequency-ordered lexicon
(about 490 KB in the library, 1.4 MB of source); Hangul syllables and
`CJK UNIFIED IDEOGRAPH-XXXX` are computed, other `PREFIX-XXXX` families kept
as code-point ranges. Lookup is a linear scan of the encoded entries (tens of
microseconds), paid only when a literal is compiled.

**Rationale:** It matches CPython exactly (every name and alias was checked
against CPython, including its case rules) on Linux, macOS and Windows with
no runtime dependency: ICU is not a protoPython dependency, and a data file
would need an install location. Generating at build time would make the
table depend on the build machine's Python. The same table can back
`unicodedata.lookup` / `name` later; `unicodedata.py` is still a stub, and
`unicode_escape` decoding is not implemented by the codecs module, so
neither uses it yet.
