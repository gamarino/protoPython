# 20-commit sweep round 5 — prototype + stdlib polish

**Date:** 2026-05-14 (round 5)
**Scope:** 21 commits.

## Working rules

* One root cause per commit; ctest 199/199 green per commit.
* No --no-verify. Standard Co-Authored-By trailer.

## Approach

Continue the round-4 surface broadening: native prototype __mro__
anchors that the bootstrap missed, plus stdlib helpers and small
protocol-conformance fixes.

## Commits (R5-81..R5-101 + final docs)

* R5-81  collections.deque prototype owns __mro__.
* R5-82  collections.OrderedDict / defaultdict __mro__ fix.
* R5-83  itertools prototypes own __mro__.
* R5-84  set / frozenset iterator prototypes own __mro__.
* R5-85  dict view (keys/values/items) prototypes own __mro__.
* R5-86  bytes / bytearray iterator prototypes own __mro__.
* R5-87  str iterator prototype owns __mro__.
* R5-88  enumerate / zip __reduce__ for pickling.
* R5-89  object.__format__ with empty spec returns str(self).
* R5-90  object.__ne__ delegates to __eq__ result negation.
* R5-91  stdlib: heapq merge handles key= argument.
* R5-92  stdlib: bisect insort handles key= argument.
* R5-93  stdlib: functools.partial repr matches CPython.
* R5-94  stdlib: operator.attrgetter __reduce__.
* R5-95  stdlib: collections.namedtuple _make classmethod.
* R5-96  stdlib: copy handles __reduce_ex__ 5-tuple with listitems.
* R5-97  stdlib: pickle handles __reduce_ex__ 5-tuple dictitems.
* R5-98  stdlib: weakref.ref repr matches CPython.
* R5-99  stdlib: types.SimpleNamespace __eq__ across subclasses.
* R5-100 stdlib: enum value lookup edge case.
* R5-101 docs round-5 entry.
