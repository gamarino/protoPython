# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of MappingView, KeysView, ItemsView and ValuesView."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("MappingView", "__init__")
def _mapping_view_init(self, mapping):
    self._mapping = mapping


@_define("MappingView", "__len__")
def _mapping_view_len(self):
    return len(self._mapping)


@_define("MappingView", "__repr__")
def _mapping_view_repr(self):
    return '{0.__class__.__name__}({0._mapping!r})'.format(self)


@_define("KeysView", "_from_iterable")
def _keys_view_from_iterable(cls, it):
    return set(it)


@_define("KeysView", "__contains__")
def _keys_view_contains(self, key):
    return key in self._mapping


@_define("KeysView", "__iter__")
def _keys_view_iter(self):
    yield from self._mapping


@_define("ItemsView", "_from_iterable")
def _items_view_from_iterable(cls, it):
    return set(it)


@_define("ItemsView", "__contains__")
def _items_view_contains(self, item):
    key, value = item
    try:
        v = self._mapping[key]
    except KeyError:
        return False
    else:
        return v is value or v == value


@_define("ItemsView", "__iter__")
def _items_view_iter(self):
    for key in self._mapping:
        yield (key, self._mapping[key])


@_define("ValuesView", "__contains__")
def _values_view_contains(self, value):
    for key in self._mapping:
        v = self._mapping[key]
        if v is value or v == value:
            return True
    return False


@_define("ValuesView", "__iter__")
def _values_view_iter(self):
    for key in self._mapping:
        yield self._mapping[key]


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ())
