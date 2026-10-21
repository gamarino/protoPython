# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of Set and MutableSet."""

import sys

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("Set", "__le__")
def _set_le(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    if len(self) > len(other):
        return False
    for elem in self:
        if elem not in other:
            return False
    return True


@_define("Set", "__lt__")
def _set_lt(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    return len(self) < len(other) and self.__le__(other)


@_define("Set", "__gt__")
def _set_gt(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    return len(self) > len(other) and self.__ge__(other)


@_define("Set", "__ge__")
def _set_ge(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    if len(self) < len(other):
        return False
    for elem in other:
        if elem not in self:
            return False
    return True


@_define("Set", "__eq__")
def _set_eq(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    return len(self) == len(other) and self.__le__(other)


@_define("Set", "_from_iterable")
def _set_from_iterable(cls, it):
    '''Construct an instance of the class from any iterable input.

    Must override this method if the class constructor signature
    does not accept an iterable for an input.
    '''
    return cls(it)


@_define("Set", "__and__")
def _set_and(self, other):
    if not isinstance(other, Iterable):
        return NotImplemented
    return self._from_iterable(value for value in other if value in self)


_alias("Set", "__rand__", "__and__")


@_define("Set", "isdisjoint")
def _set_isdisjoint(self, other):
    'Return True if two sets have a null intersection.'
    for value in other:
        if value in self:
            return False
    return True


@_define("Set", "__or__")
def _set_or(self, other):
    if not isinstance(other, Iterable):
        return NotImplemented
    chain = (e for s in (self, other) for e in s)
    return self._from_iterable(chain)


_alias("Set", "__ror__", "__or__")


@_define("Set", "__sub__")
def _set_sub(self, other):
    if not isinstance(other, Set):
        if not isinstance(other, Iterable):
            return NotImplemented
        other = self._from_iterable(other)
    return self._from_iterable(value for value in self
                               if value not in other)


@_define("Set", "__rsub__")
def _set_rsub(self, other):
    if not isinstance(other, Set):
        if not isinstance(other, Iterable):
            return NotImplemented
        other = self._from_iterable(other)
    return self._from_iterable(value for value in other
                               if value not in self)


@_define("Set", "__xor__")
def _set_xor(self, other):
    if not isinstance(other, Set):
        if not isinstance(other, Iterable):
            return NotImplemented
        other = self._from_iterable(other)
    return (self - other) | (other - self)


_alias("Set", "__rxor__", "__xor__")


@_define("Set", "_hash")
def _set_hash(self):
    """Compute the hash value of a set.

    Note that we don't define __hash__: not all sets are hashable.
    But if you define a hashable set type, its __hash__ should
    call this function.

    This must be compatible __eq__.

    All sets ought to compare equal if they contain the same
    elements, regardless of how they are implemented, and
    regardless of the order of the elements; so there's not much
    freedom for __eq__ or __hash__.  We match the algorithm used
    by the built-in frozenset type.
    """
    MAX = sys.maxsize
    MASK = 2 * MAX + 1
    n = len(self)
    h = 1927868237 * (n + 1)
    h &= MASK
    for x in self:
        hx = hash(x)
        h ^= (hx ^ (hx << 16) ^ 89869747)  * 3644798167
        h &= MASK
    h ^= (h >> 11) ^ (h >> 25)
    h = h * 69069 + 907133923
    h &= MASK
    if h > MAX:
        h -= MASK + 1
    if h == -1:
        h = 590923713
    return h


@_define("MutableSet", "add", abstract=True)
def _mutable_set_add(self, value):
    """Add an element."""
    raise NotImplementedError


@_define("MutableSet", "discard", abstract=True)
def _mutable_set_discard(self, value):
    """Remove an element.  Do not raise an exception if absent."""
    raise NotImplementedError


@_define("MutableSet", "remove")
def _mutable_set_remove(self, value):
    """Remove an element. If not a member, raise a KeyError."""
    if value not in self:
        raise KeyError(value)
    self.discard(value)


@_define("MutableSet", "pop")
def _mutable_set_pop(self):
    """Return the popped value.  Raise KeyError if empty."""
    it = iter(self)
    try:
        value = next(it)
    except StopIteration:
        raise KeyError from None
    self.discard(value)
    return value


@_define("MutableSet", "clear")
def _mutable_set_clear(self):
    """This is slow (creates N new iterators!) but effective."""
    try:
        while True:
            self.pop()
    except KeyError:
        pass


@_define("MutableSet", "__ior__")
def _mutable_set_ior(self, it):
    for value in it:
        self.add(value)
    return self


@_define("MutableSet", "__iand__")
def _mutable_set_iand(self, it):
    for value in (self - it):
        self.discard(value)
    return self


@_define("MutableSet", "__ixor__")
def _mutable_set_ixor(self, it):
    if it is self:
        self.clear()
    else:
        if not isinstance(it, Set):
            it = self._from_iterable(it)
        for value in it:
            if value in self:
                self.discard(value)
            else:
                self.add(value)
    return self


@_define("MutableSet", "__isub__")
def _mutable_set_isub(self, it):
    if it is self:
        self.clear()
    else:
        for value in it:
            self.discard(value)
    return self


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ('Set', 'Iterable'))
