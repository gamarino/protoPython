# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of Hashable, Sized, Container, Buffer and Callable."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("Hashable", "__hash__", abstract=True)
def _hashable_hash(self):
    return 0


@_define("Sized", "__len__", abstract=True)
def _sized_len(self):
    return 0


@_define("Container", "__contains__", abstract=True)
def _container_contains(self, x):
    return False


@_define("Buffer", "__buffer__", abstract=True)
def _buffer_buffer(self, flags, /):
    raise NotImplementedError


@_define("Callable", "__call__", abstract=True)
def _callable_call(self, *args, **kwds):
    return False


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ())
