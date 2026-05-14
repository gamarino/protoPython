# 20-commit sweep, round 3 — test_descr.py + adjacent surfaces

**Date:** 2026-05-13 (continuation, round 3)
**Scope:** 21 commits (one root cause per commit + final docs)

## Reference baselines

| Sweep | Run | Fail | Error | Skipped |
| :--- | ---: | ---: | ---: | ---: |
| 2026-05-13 post-round-2 (current) | 165 | 50 | 53 | 10 |
| 2026-05-13 post-round-1            | 165 | 54 | 53 | 10 |
| 2026-05-13 baseline                | 165 | 51 | 21 | 10 |

## Working rules

* One root cause per commit. No bundling.
* ctest --test-dir build stays 199/199 green on every commit.
* No --no-verify.
* Standard `Co-Authored-By:` trailer.
* Commit message body cites the test method when applicable.

## Commit roster

Phase L — small CPython contract gaps (8 commits):

* **L-43** mappingproxy `pop` / `popitem` raise TypeError.
* **L-44** mappingproxy `clear` / `update` / `setdefault` raise TypeError.
* **L-45** `type.__name__` setter rejects non-string values with TypeError.
* **L-46** `__qualname__` setter rejects non-string values with TypeError.
* **L-47** `__module__` writable / readable as documented.
* **L-48** `vars(obj)` for module returns the module's `__dict__` directly.
* **L-49** `len(cls.__dict__)` matches the keys() count exactly.
* **L-50** `iter(cls.__dict__)` produces strings, not key objects.

Phase M — copyreg/pickle corner cases (4 commits):

* **M-51** `copyreg._reduce_ex` rejects `__getnewargs_ex__` returning a
  non-tuple-of-(tuple, dict) with TypeError.
* **M-52** `copyreg._reduce_ex` proto >= 2 path tolerates the
  `__getnewargs_ex__()` returning `(args, {})` empty-kwargs form by
  routing through `__newobj__` instead of `__newobj_ex__`.
* **M-53** `object.__getstate__()` returns `None` when slots and
  dict are BOTH empty (not the empty 2-tuple).
* **M-54** classmethod / staticmethod expose `__wrapped__` regardless
  of the wrapped value's shape.

Phase N — built-in type strictness (4 commits):

* **N-55** assigning to `int.__name__` / `str.__name__` raises TypeError
  with "cannot set '__name__' attribute of immutable type" message.
* **N-56** assigning to `list.__doc__` / `dict.__doc__` raises TypeError
  with "cannot set '__doc__' attribute of immutable type" message.
* **N-57** `delattr(list, 'lower')` raises TypeError (immutable type).
* **N-58** `int.__add__` raises TypeError when called with mismatched
  argument shape (different from len(args) issue).

Phase O — slot mechanics (4 commits):

* **O-59** `__slots__ = ("__dict__",)` re-exposes a per-instance
  `__dict__` AND keeps the slot machinery for any other named slots.
* **O-60** `__slots__` enforcement allows direct assignment via
  `object.__setattr__(instance, slot_name, value)`.
* **O-61** `__slots__` containing a string `(SubStr('x'),)` honours
  the SubStr value as a real slot name.
* **O-62** Subclass instance with mixed `__slots__` AND `__dict__`
  works: `class C: __slots__ = ('a', '__dict__')`.

Phase P — final (1 commit, total = 21):

* **P-63** `docs/CPYTHON_CONFORMANCE.md` post-tenth-sweep entry.

## Risk register

* Many of these targets are TypeError-shape fixes whose exact
  CPython message matters for `assertRaisesRegex` — if our message
  diverges by a punctuation mark the test still fails.  Test the
  regex match string-by-string before claiming a commit complete.
* O-59 and O-62 touch the strict-slots walk added in I-27.  Verify
  no regression on `class C(tuple): __slots__ = ['a']`-style tests.

## Definition of done

* 21 commits landed, each green on ctest --test-dir build.
* `test_descr.py` fail+error count strictly lower than
  round-2 final (50F+53E = 103).
* `docs/CPYTHON_CONFORMANCE.md` updated with round-3 section.
