# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of MutableMapping."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("MutableMapping", "__setitem__", abstract=True)
def _mutable_mapping_setitem(self, key, value):
    raise KeyError


@_define("MutableMapping", "__delitem__", abstract=True)
def _mutable_mapping_delitem(self, key):
    raise KeyError


# CPython's MutableMapping.__marker.
_marker = object()


@_define("MutableMapping", "pop")
def _mutable_mapping_pop(self, key, default=_marker):
    '''D.pop(k[,d]) -> v, remove specified key and return the corresponding
    value.  If key is not found, d is returned if given, otherwise
    KeyError is raised.
    '''
    try:
        value = self[key]
    except KeyError:
        if default is _marker:
            raise
        return default
    else:
        del self[key]
        return value


@_define("MutableMapping", "popitem")
def _mutable_mapping_popitem(self):
    '''D.popitem() -> (k, v), remove and return some (key, value) pair
       as a 2-tuple; but raise KeyError if D is empty.
    '''
    try:
        key = next(iter(self))
    except StopIteration:
        raise KeyError from None
    value = self[key]
    del self[key]
    return key, value


@_define("MutableMapping", "clear")
def _mutable_mapping_clear(self):
    'D.clear() -> None.  Remove all items from D.'
    try:
        while True:
            self.popitem()
    except KeyError:
        pass


@_define("MutableMapping", "update")
def _mutable_mapping_update(self, other=(), /, **kwds):
    ''' D.update([E, ]**F) -> None.  Update D from mapping/iterable E and F.
        If E present and has a .keys() method, does:
            for k in E.keys(): D[k] = E[k]
        If E present and lacks .keys() method, does:
            for (k, v) in E: D[k] = v
        In either case, this is followed by:
            for k, v in F.items(): D[k] = v
    '''
    if isinstance(other, Mapping):
        for key in other:
            self[key] = other[key]
    elif hasattr(other, "keys"):
        for key in other.keys():
            self[key] = other[key]
    else:
        for key, value in other:
            self[key] = value
    for key, value in kwds.items():
        self[key] = value


@_define("MutableMapping", "setdefault")
def _mutable_mapping_setdefault(self, key, default=None):
    'D.setdefault(k[,d]) -> D.get(k,d), also set D[k]=d if k not in D'
    try:
        return self[key]
    except KeyError:
        self[key] = default
    return default


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ('Mapping',))
