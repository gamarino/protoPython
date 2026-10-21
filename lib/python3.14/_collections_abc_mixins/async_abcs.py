# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Methods of Awaitable, Coroutine, AsyncIterable, AsyncIterator and AsyncGenerator."""

from _collections_abc_mixins import define as _define_in, alias as _alias_in, install as _install

_METHODS = {}


def _define(abc_name, name, abstract=False):
    return _define_in(_METHODS, abc_name, name, abstract)


def _alias(abc_name, name, other):
    _alias_in(_METHODS, abc_name, name, other)


@_define("Awaitable", "__await__", abstract=True)
def _awaitable_await(self):
    yield


@_define("Coroutine", "send", abstract=True)
def _coroutine_send(self, value):
    """Send a value into the coroutine.
    Return next yielded value or raise StopIteration.
    """
    raise StopIteration


@_define("Coroutine", "throw", abstract=True)
def _coroutine_throw(self, typ, val=None, tb=None):
    """Raise an exception in the coroutine.
    Return next yielded value or raise StopIteration.
    """
    if val is None:
        if tb is None:
            raise typ
        val = typ()
    if tb is not None:
        val = val.with_traceback(tb)
    raise val


@_define("Coroutine", "close")
def _coroutine_close(self):
    """Raise GeneratorExit inside coroutine.
    """
    try:
        self.throw(GeneratorExit)
    except (GeneratorExit, StopIteration):
        pass
    else:
        raise RuntimeError("coroutine ignored GeneratorExit")


@_define("AsyncIterable", "__aiter__", abstract=True)
def _async_iterable_aiter(self):
    return AsyncIterator()


@_define("AsyncIterator", "__anext__", abstract=True)
async def _async_iterator_anext(self):
    """Return the next item or raise StopAsyncIteration when exhausted."""
    raise StopAsyncIteration


@_define("AsyncIterator", "__aiter__")
def _async_iterator_aiter(self):
    return self


@_define("AsyncGenerator", "__anext__")
async def _async_generator_anext(self):
    """Return the next item from the asynchronous generator.
    When exhausted, raise StopAsyncIteration.
    """
    return await self.asend(None)


@_define("AsyncGenerator", "asend", abstract=True)
async def _async_generator_asend(self, value):
    """Send a value into the asynchronous generator.
    Return next yielded value or raise StopAsyncIteration.
    """
    raise StopAsyncIteration


@_define("AsyncGenerator", "athrow", abstract=True)
async def _async_generator_athrow(self, typ, val=None, tb=None):
    """Raise an exception in the asynchronous generator.
    Return next yielded value or raise StopAsyncIteration.
    """
    if val is None:
        if tb is None:
            raise typ
        val = typ()
    if tb is not None:
        val = val.with_traceback(tb)
    raise val


@_define("AsyncGenerator", "aclose")
async def _async_generator_aclose(self):
    """Raise GeneratorExit inside coroutine.
    """
    try:
        await self.athrow(GeneratorExit)
    except (GeneratorExit, StopAsyncIteration):
        pass
    else:
        raise RuntimeError("asynchronous generator ignored GeneratorExit")


def install(module):
    """Put the functions above in place of the trampolines of `module`'s ABCs."""
    _install(module, _METHODS, globals(), ('AsyncIterator',))
