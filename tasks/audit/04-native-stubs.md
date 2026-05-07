# P4 — Native module stubs vs CPython semantics

## Problem statement

protoPython ships ~40 native C++ modules under `src/library/*Module.cpp`. Most provide a *minimal subset* of the CPython module's API — enough to make common stdlib code import-and-run, but with significant gaps that break user code silently.

The pattern that surfaced in this session: `_collections_abc.MutableMapping.update` was a no-op (`py_abc_call` returned `self.newChild()`), so `UserDict({'a':1})` left `data` empty. The shadowed `.py` file was never executed because the native module is registered with the same name and wins the import lookup.

This audit characterizes the gap per module so the future maintainer knows where the silent failures live.

## Methodology

For each module:
1. Count exposed methods (sites in `initialize` that `setAttribute(... fromMethod(...))`).
2. Compare against CPython's documented module API (https://docs.python.org/3.14/library/).
3. For each missing or stubbed method: classify the user impact.

The audit favours **breadth over depth** — calling out the riskiest modules with concrete examples, not exhaustive method tables for each.

## Module inventory and risk assessment

### CRITICAL — modules that silently break common patterns

#### `CollectionsAbcModule` (`_collections_abc`)

The module is a stub that creates dummy `Mapping` / `MutableMapping` / `Iterable` / etc. classes whose methods are mostly no-ops (`py_abc_call` returns `self.newChild()`).

**Recently fixed** (this session, task #87/#90):
- `MutableMapping.update` — now propagates correctly.
- `Mapping.__contains__` — now consults `__getitem__`.

**Still broken**:
- `Mapping.get` — has a real impl (`py_abc_get`), looks correct.
- `Mapping.keys` / `values` / `items` — have real impls (`py_mapping_keys`/`values`/`items`), correct.
- `MutableMapping.pop` — `py_abc_call` no-op. Returns `self.newChild()`. **Wrong**.
- `MutableMapping.popitem` — same.
- `MutableMapping.clear` — same.
- `MutableMapping.setdefault` — same.
- `MutableSequence.append` / `extend` / `insert` / `remove` / `reverse` — all `py_abc_call`. **All wrong.**
- `Sequence.index` / `count` — same.
- `Set.__or__` / `__and__` / `__sub__` / `__xor__` / `isdisjoint` etc. — likely all wrong.

**Severity: HIGH.** Dozens of stdlib classes (UserList, UserDict, OrderedDict via MutableMapping, set abstractions) inherit from these ABCs and rely on the methods being correct. Any user method that calls e.g. `mapping.pop(k)` on a custom Mapping subclass silently does nothing.

**Recommendation**: replace `py_abc_call` no-ops with real implementations mirroring `_collections_abc.py`. Approx 15-20 methods to write. 4-8 hours of work.

#### `HeapqModule` (`heapq`)

Exposes only `heappush` and `heappop`. CPython exposes 8: `heapify`, `heappushpop`, `heapreplace`, `nlargest`, `nsmallest`, `merge`, plus the two above.

**Severity: HIGH.** Plus the existing `heappush` is **broken**: it builds a new heap list internally but never publishes it back to the caller's list (the comment in the code acknowledges this). `heappop` has a similar return-but-don't-mutate bug. So even the two "implemented" functions are silently wrong.

**Recommendation**: rewrite as a thin Python `lib/python3.14/heapq.py` that uses list slicing and comparison — drop the broken native module entirely. Or fix the in-place mutation. 2-4 hours.

#### `BisectModule` (`bisect`)

Exposes `bisect`, `bisect_left`, `bisect_right`. Missing: `insort_left`, `insort_right`, `insort`. Also missing `key=` keyword arg support introduced in Python 3.10.

**Severity: MEDIUM.** Search functions work; insertion functions silently absent (will raise AttributeError if called). The `key=` gap is a silent bug for code that uses it.

#### `SignalModule` (`signal`)

Exposes only `signal()` and `getsignal()`. The handler dispatch does NOT actually invoke the registered Python handler — it just logs. The comment in the source explicitly calls this out as "DANGEROUS".

**Severity: HIGH** for any code that relies on signal handling for cleanup, KeyboardInterrupt, alarm, etc.

**Recommendation**: either implement proper signal-pipe + main-loop check, or remove the module so callers fail at import time instead of silently miss signals.

#### `WeakrefModule` (`weakref`)

Exposes `ref`, `proxy`, `getweakrefcount`, `getweakrefs`, `WeakrefRef`, etc. — but the underlying weak-reference mechanism in protoCore needs to be checked. Likely the references ARE strong (regular reachable handles) and `getweakrefcount` always returns the same number. Need source review to confirm.

**Severity: MEDIUM** for code that relies on weakref semantics (caches, observer patterns).

### MAJOR — modules with significant gaps

#### `IOModule` (`_io`)

Exposes only `open` and `open_code`. CPython's `_io` exposes 30+: `BufferedReader`, `BufferedWriter`, `BufferedRandom`, `FileIO`, `TextIOWrapper`, `BytesIO`, `StringIO`, `IncrementalNewlineDecoder`, etc.

The current `open()` returns a fake file object with `read`, `readlines`, `write`, `close`, `__enter__`, `__exit__`. Missing on file objects:
- `__iter__` / `__next__` (file objects in CPython are iterable line-by-line).
- `seek`, `tell`, `truncate`, `flush`.
- `readline` (singular).
- `readable`, `writable`, `seekable`.
- `mode`, `name`, `closed` (some present).
- Buffer management (the underlying `__file_buffer__` is read-once at open time; subsequent writes may not flush).

**Severity: HIGH** for any non-trivial I/O code. The `__iter__` gap broke pdb's rcLines reader (fixed via FileNotFoundError workaround in #91).

#### `OsModule` (`os`)

Largest native module (1590 lines, 116 methods exposed). Most common functions covered but many stubs exist (69 sites return `PROTO_NONE` directly). Spot checks:

- `os.write(fd, bytes)` — was missing in the doctest path debugging earlier (raised AttributeError).
- `os.fork`, `os.spawn*`, `os.exec*` — likely stubbed.
- `os.scandir` — needs check.
- `os.path.expanduser` — uses Python `lib/python3.14/posixpath.py`; this works after the `'HOME' in os.environ` fix from #91.

**Severity: HIGH variance** by sub-area. Common file-and-env operations work; process control likely stubbed.

#### `ThreadModule` (`_thread`)

663 lines, 41 methods exposed, 25 PROTO_NONE returns. Threading is one of the harder things to fake; many stubs likely produce silent races or no-ops.

**Severity: HIGH** for threaded code. Since protoPython explicitly markets "GIL-free concurrency" this is a key selling-point area; any divergence from CPython's threading.Lock semantics is a serious issue.

#### `MathModule` (`math`)

751 lines, 60 methods exposed, 69 PROTO_NONE returns. The PROTO_NONE count is high relative to method count, suggesting many stubs. The functions that work probably cover trig/log/exp basics; rarer functions (gamma, lgamma, comb, perm, isclose with kwargs) may be missing.

**Severity: MEDIUM.** Numeric code that uses common functions works; less common ones silently return None (causing TypeError downstream).

### MODERATE — modules that may be functional but unaudited

| Module | LOC | Exposed | Status |
|---|---|---|---|
| `BuiltinsModule` | (large) | many | Reviewed during fixes; major builtins implemented. |
| `SysModule` | (large) | many | Mostly works; encoding/file attributes added in #85. |
| `BinasciiModule` | small | ~5 | Most encode/decode work; `Error` class added in #95. |
| `CollectionsModule` | medium | ~10 | Provides deque, Counter, OrderedDict; semantics need check. |
| `OperatorModule` | medium | many | Likely covers `add`, `sub`, etc. — need to verify the "fast path bypass" pattern doesn't apply here too. |
| `FunctoolsModule` | medium | ~10 | `reduce` has GC root gap (F3.1). `partial`, `cache`, etc. need check. |
| `ItertoolsModule` | medium | many | Generator-heavy; many use `env->next` so likely have F3.1 leaks. |
| `ContextvarsModule` | tiny | 5 | 50 lines for the entire contextvars protocol. Almost certainly stubbed. |
| `JsonModule` | medium | many | Encode/decode; needs CPython-spec check on edge cases. |
| `ReModule` | medium | many | Uses regex backend; depth of compatibility unknown. |
| `DatetimeModule` | medium | many | Date/time arithmetic; needs check vs `datetime.datetime` semantics. |
| `MarshalModule` | tiny | 2 | `dumps`/`loads`; needs check whether bytecode marshalling is round-trip safe. |
| `StructModule` | medium | many | Format string handling; alignment quirks likely missing. |

### LOW — modules that are mostly cosmetic

| Module | Why low |
|---|---|
| `CodecsModule` | UTF-8 only, fine for our use. |
| `LoggingModule` | log methods are implemented but the framework is shallow; users rarely notice. |
| `ExceptionsModule` | exception classes; verified via test_descr / test_grammar. |
| `WarningsModule` | warning suppression is a no-op; not user-visible. |
| `AstModule` | AST traversal; exposed but probably partial. |
| `ImpModule` | legacy, deprecated in CPython; minimal stub OK. |
| `OpcodeModule` | bytecode introspection; protopy compiler uses it internally. |
| `AtexitModule` | register callbacks; minimal stub, fine for non-server use. |

## Aggregated findings

### F4.1 — `CollectionsAbcModule` has 15+ ABC methods that are still no-ops

After fixing `update` and `__contains__` this session, dozens of `MutableMapping`/`MutableSequence`/`Set`/`MutableSet` methods remain stubbed. Any class that subclasses one of these ABCs and relies on inherited `pop`/`clear`/`setdefault`/`append`/`extend`/etc. will silently misbehave.

**Severity: HIGH.** Affects `UserList`, `UserDict`, `OrderedDict`, `deque`, `Counter`, set abstractions in `_collections_abc`.

### F4.2 — `HeapqModule` and `BisectModule` are broken/incomplete

- HeapqModule's `heappush`/`heappop` don't actually mutate the user's list (they build a new one and discard it).
- HeapqModule is missing 6 of 8 functions.
- BisectModule is missing all 3 `insort` variants.

**Severity: HIGH for heapq, MEDIUM for bisect.** A standard library swap of these modules with pure-Python implementations would fix both fast.

### F4.3 — `SignalModule` doesn't actually deliver signals

`signal.signal(SIGINT, handler)` registers the handler, but the C-level handler just logs. Python handler never runs.

**Severity: HIGH** for KeyboardInterrupt, SIGTERM, etc.

### F4.4 — `IOModule` file objects miss `__iter__` and basic positioning

`for line in file:` doesn't work. `f.seek()`, `f.tell()`, `f.flush()`, `f.readline()` likely missing or broken.

**Severity: HIGH** for any non-trivial I/O.

### F4.5 — Thread/Math/Os modules have many PROTO_NONE stub returns

Some methods are implemented; many silently return None where CPython would return a real value. The user gets a downstream `'NoneType' object has no attribute 'X'` error far from the source of the problem.

**Severity: MEDIUM-HIGH** per module, depending on which functions are stubbed.

### F4.6 — Many secondary modules unaudited

`Itertools`, `Functools`, `Datetime`, `Json`, `Re`, `Struct`, `Contextvars`, `Marshal` — each has 5-30 methods exposed, none verified against CPython spec. Each is a potential silent-failure pocket.

**Severity: TBD until inspected.** Each module needs a comparable audit.

## Architectural recommendation

The native module pattern in protoPython has been "implement the methods needed to make popular code paths run; stub the rest". For an early bring-up phase this is reasonable. For Phase 6 → production-readiness, it is a liability:

- Stubs that silently succeed are harder to debug than missing methods.
- Each stubbed method is a chance for a CPython-spec divergence to leak into user code.
- The cost of "I'll fix it when someone hits it" compounds with every user.

**The shared fix path**: convert as many modules as possible from native C++ to pure-Python (`lib/python3.14/<name>.py`) using the now-functional Python facilities. This:
- Makes the implementation self-documenting (it IS the CPython source).
- Eliminates the C++ vs Python semantic divergence class of bug entirely.
- Reduces native code surface area, which is where ABI / GC / dispatch bugs live.

The natural candidates for Python-isation (in order of effort):
1. `HeapqModule` — pure algorithmic, no C-only feature needed. **2 hours.**
2. `BisectModule` — same. **1 hour.**
3. `FunctoolsModule` (most of it) — `reduce`, `partial`, `lru_cache` are pure Python in CPython. **3-4 hours.**
4. `ItertoolsModule` — most are generators expressible in Python. **4-6 hours.**
5. `JsonModule` — CPython has both _json (C) and json (Python). The Python fallback works. **6-8 hours.**

This is a substantial project (15-25 hours total) but each step is independent and immediately reduces the audit surface.

For modules that *must* stay native (IO, Os, Thread, Signal, Math): formalise an "implementation-status" header comment in each, listing per-function: ✅ implemented, ⚠ partial, ❌ stub. Updated as functions are completed. Future developers can see at a glance what's safe.

## Action items

1. **F4.1** — finish `CollectionsAbcModule`: implement remaining ABC methods. (4-8h)
2. **F4.2 heapq** — replace native module with `lib/python3.14/heapq.py`. (2h)
3. **F4.2 bisect** — same. (1h)
4. **F4.3** — fix or remove SignalModule. (4h to do properly; 30min to remove)
5. **F4.4 IOModule** — add `__iter__`/`__next__`, `seek`, `tell`, `flush`, `readline`. (3-4h)
6. **F4.5** — audit and complete OsModule / ThreadModule / MathModule per-function. (8-12h spread)
7. **F4.6** — bench-test each unaudited module against a 30-line CPython smoke-test script. (4h)
8. **Architectural** — convert HeapqModule/BisectModule/FunctoolsModule/ItertoolsModule/JsonModule to pure Python where possible. (15-25h)

Total: 30-50 hours of focused work, but each item is independently shippable.
