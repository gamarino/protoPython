# Access WeakSet through the weakref module.
# This code is separated-out because it is needed
# by abc.py to load everything else at startup.

from _weakref import ref
GenericAlias = type(list[int])

# protoPython DEVIATION, and the reason every method below that drops an element
# also calls `_wr_evict`.
#
# protoPython has no GC-level weak reference.  `_weakref.ref(obj)` registers
# `obj` in the `_weakref` module's `__active__` presence set and that membership
# is a STRONG reference (see src/library/WeakrefModule.cpp); an entry leaves only
# when `_weakref._evict(obj)` is called.  A WeakSet that never evicts is
# therefore an unbounded leak rather than a weak container: `threading._dangling`
# grew by one permanently-reachable Thread per Thread ever created, ~150 marked
# cells each, which is most of what made the rule-8 embedder-conformance case
# reach its heap ceiling (docs/CONFORMANCE.md).
#
# Membership in the set is what keeps the object alive, so leaving the set must
# release it.  That makes this WeakSet as weak as protoPython can express:
# deterministic on removal instead of automatic on collection.  It does NOT make
# an element vanish when the last other reference to it goes away -- only an
# explicit `discard` / `remove` / `pop` / `clear` releases it.
try:
    from _weakref import _evict as _wr_evict
except ImportError:                     # a CPython-compatible _weakref
    def _wr_evict(item):
        pass

__all__ = ['WeakSet']


class WeakSet:
    def __init__(self, data=None):
        self.data = set()

        def _remove(item, selfref=ref(self)):
            self = selfref()
            if self is not None:
                self.data.discard(item)

        self._remove = _remove
        if data is not None:
            self.update(data)

    def __iter__(self):
        for itemref in self.data.copy():
            item = itemref()
            if item is not None:
                # Caveat: the iterator will keep a strong reference to
                # `item` until it is resumed or closed.
                yield item

    def __len__(self):
        return len(self.data)

    def __contains__(self, item):
        try:
            wr = ref(item)
        except TypeError:
            return False
        return wr in self.data

    def __reduce__(self):
        return self.__class__, (list(self),), self.__getstate__()

    def add(self, item):
        self.data.add(ref(item, self._remove))

    def clear(self):
        for itemref in self.data:
            item = itemref()
            if item is not None:
                _wr_evict(item)
        self.data.clear()

    def copy(self):
        return self.__class__(self)

    def pop(self):
        while True:
            try:
                itemref = self.data.pop()
            except KeyError:
                raise KeyError('pop from empty WeakSet') from None
            item = itemref()
            if item is not None:
                _wr_evict(item)
                return item

    def remove(self, item):
        # ref(item) re-registers item in the presence set, so evict AFTER.
        self.data.remove(ref(item))
        _wr_evict(item)

    def discard(self, item):
        self.data.discard(ref(item))
        _wr_evict(item)

    def update(self, other):
        for element in other:
            self.add(element)

    def __ior__(self, other):
        self.update(other)
        return self

    def difference(self, other):
        newset = self.copy()
        newset.difference_update(other)
        return newset
    __sub__ = difference

    def difference_update(self, other):
        self.__isub__(other)
    def __isub__(self, other):
        if self is other:
            self.data.clear()
        else:
            self.data.difference_update(ref(item) for item in other)
        return self

    def intersection(self, other):
        return self.__class__(item for item in other if item in self)
    __and__ = intersection

    def intersection_update(self, other):
        self.__iand__(other)
    def __iand__(self, other):
        self.data.intersection_update(ref(item) for item in other)
        return self

    def issubset(self, other):
        return self.data.issubset(ref(item) for item in other)
    __le__ = issubset

    def __lt__(self, other):
        return self.data < set(map(ref, other))

    def issuperset(self, other):
        return self.data.issuperset(ref(item) for item in other)
    __ge__ = issuperset

    def __gt__(self, other):
        return self.data > set(map(ref, other))

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return NotImplemented
        return self.data == set(map(ref, other))

    def symmetric_difference(self, other):
        newset = self.copy()
        newset.symmetric_difference_update(other)
        return newset
    __xor__ = symmetric_difference

    def symmetric_difference_update(self, other):
        self.__ixor__(other)
    def __ixor__(self, other):
        if self is other:
            self.data.clear()
        else:
            self.data.symmetric_difference_update(ref(item, self._remove) for item in other)
        return self

    def union(self, other):
        return self.__class__(e for s in (self, other) for e in s)
    __or__ = union

    def isdisjoint(self, other):
        return len(self.intersection(other)) == 0

    def __repr__(self):
        return repr(self.data)

    __class_getitem__ = classmethod(GenericAlias)
