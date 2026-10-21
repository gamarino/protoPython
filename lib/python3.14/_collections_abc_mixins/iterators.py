# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of Iterable, Iterator, Reversible and Generator."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("Iterable", "__iter__", abstract=True)
def _iterable_iter(self):
    while False:
        yield None


@_define("Iterator", "__next__", abstract=True)
def _iterator_next(self):
    'Return the next item from the iterator. When exhausted, raise StopIteration'
    raise StopIteration


@_define("Iterator", "__iter__")
def _iterator_iter(self):
    return self


@_define("Reversible", "__reversed__", abstract=True)
def _reversible_reversed(self):
    while False:
        yield None


@_define("Generator", "__next__")
def _generator_next(self):
    """Return the next item from the generator.
    When exhausted, raise StopIteration.
    """
    return self.send(None)


@_define("Generator", "send", abstract=True)
def _generator_send(self, value):
    """Send a value into the generator.
    Return next yielded value or raise StopIteration.
    """
    raise StopIteration


@_define("Generator", "throw", abstract=True)
def _generator_throw(self, typ, val=None, tb=None):
    """Raise an exception in the generator.
    Return next yielded value or raise StopIteration.
    """
    if val is None:
        if tb is None:
            raise typ
        val = typ()
    if tb is not None:
        val = val.with_traceback(tb)
    raise val


@_define("Generator", "close")
def _generator_close(self):
    """Raise GeneratorExit inside generator.
    """
    try:
        self.throw(GeneratorExit)
    except (GeneratorExit, StopIteration):
        pass
    else:
        raise RuntimeError("generator ignored GeneratorExit")


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ())
