# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of Mapping."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("Mapping", "__getitem__", abstract=True)
def _mapping_getitem(self, key):
    raise KeyError


@_define("Mapping", "get")
def _mapping_get(self, key, default=None):
    'D.get(k[,d]) -> D[k] if k in D, else d.  d defaults to None.'
    try:
        return self[key]
    except KeyError:
        return default


@_define("Mapping", "__contains__")
def _mapping_contains(self, key):
    try:
        self[key]
    except KeyError:
        return False
    else:
        return True


@_define("Mapping", "keys")
def _mapping_keys(self):
    "D.keys() -> a set-like object providing a view on D's keys"
    return KeysView(self)


@_define("Mapping", "items")
def _mapping_items(self):
    "D.items() -> a set-like object providing a view on D's items"
    return ItemsView(self)


@_define("Mapping", "values")
def _mapping_values(self):
    "D.values() -> an object providing a view on D's values"
    return ValuesView(self)


@_define("Mapping", "__eq__")
def _mapping_eq(self, other):
    if not isinstance(other, Mapping):
        return NotImplemented
    return dict(self.items()) == dict(other.items())


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ('Mapping', 'KeysView', 'ItemsView', 'ValuesView'))
