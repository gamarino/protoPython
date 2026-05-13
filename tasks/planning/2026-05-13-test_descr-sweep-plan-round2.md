# 20-commit sweep, round 2 — `test_descr.py` push

**Date:** 2026-05-13 (continuation)
**Scope:** another 20 root-cause commits + 1 final doc commit
**Carryover from round 1:** the 8th sweep (commits `30381e7d`..`933e9ef7`)
landed 17 commits hitting Phases A / B / C / D / E.  Phase F + E-18 +
deep slot-class pickling were deferred to this round.

## Reference baselines

| Sweep | Run | Fail | Error | Skipped | Notes |
| :--- | ---: | ---: | ---: | ---: | :--- |
| 2026-05-07 (pre-sweep)    | 159 | 93 | 34 | 10 | per docs/CPYTHON_CONFORMANCE.md |
| 2026-05-13 baseline       | 165 | 51 | 21 | 10 | post-A-01 (`tasks/planning/test_descr_baseline_2026-05-13.md`) |
| 2026-05-13 post-round-1   | 165 | 54 | 53 | 10 | post-G-21 (this is the new start line) |

The fail+error count went up across round 1 because every commit
unblocked tests so they now report sub-test errors individually
(see the G-21 entry of `docs/CPYTHON_CONFORMANCE.md` for the
justification).  Round 2 keeps the same "one root cause per commit"
discipline.

## Working rules (unchanged from round 1)

* One root cause per commit.  Never bundle unrelated fixes.
* `ctest --test-dir build` 199/199 green on every commit.
* No `--no-verify`, no signed-bypass.
* Commit message: 70-char headline; body cites the offending
  test method; `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.

## Commit block

### Phase H — instance dict cleanup (3 commits)

* **H-22.  `__class__` (and other type-shape keys) must not appear
  in `instance.__dict__`.**  `py_object_get_dict`'s snapshot path
  filters them on the built-in-subclass / staticmethod / classmethod
  branches; the regular path (Python user classes) does not, so
  `class C15(dict): pass; c = C15(); c.__dict__` exposes
  `__class__`.  Cascades into `test_reduce.C15` (state field carries
  the spurious `__class__` entry) and `test_set_dict`'s
  `verify_dict_readonly` write-through check.
* **H-23.  `obj.__dict__` for a regular Python instance should hide
  `__data__` / `__keys__` / `__pydict_*` bookkeeping unconditionally.**
  Confirm the filter applies to ALL three paths (built-in subclass,
  sm/cm, regular).
* **H-24.  `obj.__getstate__()` rejects `__class__` and friends so
  `obj.__reduce_ex__(0)[3]` carries the real user state only.**

### Phase I — eval / exec scope (2 commits)

* **I-25.  `eval(expr)` and `exec(stmt)` default to the caller frame's
  globals AND locals.**  Source: `test_classic_comparisons` (line
  3285: `eval("c[x] %s c[y]" % op)`) — currently `NameError: name 'c'
  is not defined`.  Same root cause shows up in `test_rich_comparisons`.
* **I-26.  `eval(expr, globals)` resolves names through `globals` for
  lookup and `locals` falls back to the caller frame.**

### Phase J — descriptor introspection on built-in types (4 commits)

* **J-27.  Native method `__name__` carries the descriptor key.**
  `[].__add__.__name__` must be `'__add__'`, not `'method'`.  Implement
  via a heap wrapper that the binding lambda returns instead of the
  raw POINTER_TAG_METHOD pointer (setAttribute on the tagged pointer
  is a no-op — see C-11 / J-27 reasoning).
* **J-28.  `int.__add__.__qualname__ == 'int.__add__'`, `str.lower.__qualname__
  == 'str.lower'`.**  `__qualname__` derives from owner's name + descriptor
  key.  Source: `test_qualname` (line 5151).
* **J-29.  `[].__add__.__self__ is []`.**  Source: `test_method_wrapper`
  line 4837 + `test_builtin_function_or_method` line 4856.
* **J-30.  `list.__add__.__objclass__ is list`.**  Source: `test_special_unbound_method_types`
  line 4870 + 4879.

### Phase K — pickling deep cuts (3 commits)

* **K-31.  Slot-pickling for classes that DO define `__getstate__`.**
  After H-24 + D-13 / D-15, the slot reconstruction round-trip needs
  one more nudge — `test_pickle_slots` C with `__getstate__` /
  `__setstate__` defined and `pickle.dumps(C(), proto)`.
* **K-32.  `test_reduce.C15(dict)` round-trip: dict subclass +
  `dictitems` iter survives `pickle.loads(pickle.dumps(obj, proto))`.**
* **K-33.  `obj.__reduce_ex__(2)` for an `__init__`-overridden Python
  class respects user `__getnewargs__` vs `__getnewargs_ex__` and
  preserves CPython's "must return tuple" / "args must be tuple of len-2"
  invariants exactly.**  Source: residual C2 / C4 / C5 / C7 sub-tests
  in `test_reduce` (assertRaises TypeError / ValueError patterns).

### Phase L — slot enforcement corner cases (3 commits)

* **L-34.  `__slots__` containing `__weakref__` adds a weakref slot
  without re-introducing a `__dict__`.**  Source:
  `test_slots_special`, `test_slots_special2`.
* **L-35.  `__slots__` containing `__dict__` re-introduces the
  instance dict on top of the slot machinery.**  Same source.
* **L-36.  Slot assignment after slot deletion raises AttributeError,
  not a stale-cell reuse.**  Source: `test_slots_after_items`,
  `test_slots_multiple_inheritance`.

### Phase M — supers and inheritance (3 commits)

* **M-37.  `super().__init__(...)` chains traverse the MRO
  consistently — `test_supers` walks the diamond cases.**
* **M-38.  `super()` zero-arg form resolves the lexically enclosing
  class and the first positional argument from the calling frame.**
* **M-39.  `test_basic_inheritance` — class-level attribute lookup
  walks parent `__dict__`s in MRO order.**  (Likely a fallout of
  earlier C-class commits; verify once round 2's earlier commits
  land.)

### Phase N — docstring + signature surface (2 commits)

* **N-40.  `_io.FileIO.closed.__doc__` is the user-facing CPython
  docstring "True if the file is closed".**  Source: `test_descrdoc`
  line 3377.
* **N-41.  `complex.real.__doc__ == "the real part of a complex number"`.**
  Same source, line 3378.

### Phase O — doc (1 commit, total = 21)

* **O-42.  `docs/CPYTHON_CONFORMANCE.md` post-ninth-sweep entry.**
  Re-measure, tabulate, summarise the per-theme fixes.  Pair with the
  CHANGELOG.

## Risk register

* **J-27 / J-28 / J-29 / J-30 (native method introspection).**
  The architectural shift is heap-allocated wrappers replacing the
  current POINTER_TAG_METHOD tagged pointers when crossing the
  descriptor protocol boundary.  Risk: every `fromMethod(nullptr, fn)`
  site can return either a tagged pointer or a heap object depending
  on context — callers that strip the wrapper expecting a tagged
  pointer break.  Mitigation: keep the tagged pointer as the
  internal callable, attach the wrapper only at the
  `getAttribute` exit when the user asks for the descriptor's
  introspection.  Land J-27 as a small isolated commit before
  J-28..J-30 build on it.
* **K-31..K-33 (slot pickling).**  Touches the same code path as
  D-13/D-15 but with subtle subTest-level assertions; risk that
  fixing one subTest regresses another.  Mitigation: track
  per-subTest pass count diff per commit.
* **L-34/L-35 (slot dict / weakref).**  Adds an instance dict
  conditionally — interacts with C-08's strict-slot AttributeError
  path.  Mitigation: re-verify `test_slots`'s
  "assertNotHasAttr(x, '__dict__')" lines after each commit.

## Definition of done

* 21 commits landed, each green on `ctest --test-dir build`.
* `test_descr.py` fail+error count strictly lower than the
  round-1 post-sweep number (54F+53E = 107).
* `docs/CPYTHON_CONFORMANCE.md` updated with the 2026-05-13
  (round-2) section.
* No `--no-verify`, no force-push, no skipped hooks across the
  block.
