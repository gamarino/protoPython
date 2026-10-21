# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of Sequence and MutableSequence."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("Sequence", "__getitem__", abstract=True)
def _sequence_getitem(self, index):
    raise IndexError


@_define("Sequence", "__iter__")
def _sequence_iter(self):
    i = 0
    try:
        while True:
            v = self[i]
            yield v
            i += 1
    except IndexError:
        return


@_define("Sequence", "__contains__")
def _sequence_contains(self, value):
    for v in self:
        if v is value or v == value:
            return True
    return False


@_define("Sequence", "__reversed__")
def _sequence_reversed(self):
    for i in reversed(range(len(self))):
        yield self[i]


@_define("Sequence", "index")
def _sequence_index(self, value, start=0, stop=None):
    '''S.index(value, [start, [stop]]) -> integer -- return first index of
       value.  Raises ValueError if the value is not present.

       Supporting start and stop arguments is optional, but
       recommended.
    '''
    if start is not None and start < 0:
        start = max(len(self) + start, 0)
    if stop is not None and stop < 0:
        stop += len(self)

    i = start
    while stop is None or i < stop:
        try:
            v = self[i]
        except IndexError:
            break
        if v is value or v == value:
            return i
        i += 1
    raise ValueError


@_define("Sequence", "count")
def _sequence_count(self, value):
    'S.count(value) -> integer -- return number of occurrences of value'
    return sum(1 for v in self if v is value or v == value)


@_define("MutableSequence", "__setitem__", abstract=True)
def _mutable_sequence_setitem(self, index, value):
    raise IndexError


@_define("MutableSequence", "__delitem__", abstract=True)
def _mutable_sequence_delitem(self, index):
    raise IndexError


@_define("MutableSequence", "insert", abstract=True)
def _mutable_sequence_insert(self, index, value):
    'S.insert(index, value) -- insert value before index'
    raise IndexError


@_define("MutableSequence", "append")
def _mutable_sequence_append(self, value):
    'S.append(value) -- append value to the end of the sequence'
    self.insert(len(self), value)


@_define("MutableSequence", "clear")
def _mutable_sequence_clear(self):
    'S.clear() -> None -- remove all items from S'
    try:
        while True:
            self.pop()
    except IndexError:
        pass


@_define("MutableSequence", "reverse")
def _mutable_sequence_reverse(self):
    'S.reverse() -- reverse *IN PLACE*'
    n = len(self)
    for i in range(n//2):
        self[i], self[n-i-1] = self[n-i-1], self[i]


@_define("MutableSequence", "extend")
def _mutable_sequence_extend(self, values):
    """S.extend(iterable) -- extend sequence by appending elements from the
    iterable"""
    if values is self:
        values = list(values)
    for v in values:
        self.append(v)


@_define("MutableSequence", "pop")
def _mutable_sequence_pop(self, index=-1):
    '''S.pop([index]) -> item -- remove and return item at index (default
    last).  Raise IndexError if list is empty or index is out of range.
    '''
    v = self[index]
    del self[index]
    return v


@_define("MutableSequence", "remove")
def _mutable_sequence_remove(self, value):
    '''S.remove(value) -- remove first occurrence of value.
       Raise ValueError if the value is not present.
    '''
    del self[self.index(value)]


@_define("MutableSequence", "__iadd__")
def _mutable_sequence_iadd(self, values):
    self.extend(values)
    return self


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ())
