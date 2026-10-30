"""Test module _testsinglephase

The CPython test helper of the same name (Modules/_testsinglephase.c), which
every standard CPython build provides.  test.test_importlib.util imports it
at module level and raises SkipTest without it, so every test module that
imports test.test_importlib.util (unittest.mock's testpatch among them) was
skipped as a whole.

protoPython has no single-phase extension modules; this module offers the
same attributes and behaviour as the basic variant of the C module, so the
code that only uses it as an ordinary module works.  Tests about loading it
as an extension find no extension file for it (test.test_importlib.util's
EXTENSIONS.file_path stays None) and skip, as they do on a CPython built
without it.
"""

import time as _time


class error(Exception):
    pass


error.__module__ = "_testsinglephase"

int_const = 1969
str_const = "something different"

_initialized_count = 1
_module_initialized = _time.time()
_state_initialized = _time.monotonic()


def sum(i, j):
    """sum(i,j)

Return the sum of i and j."""
    return i + j


def look_up_self():
    import sys
    return sys.modules[__name__]


def initialized_count():
    return _initialized_count


def state_initialized():
    return _state_initialized


def _clear_globals():
    """_clear_globals()

Free all global state and set it to uninitialized."""
    global _initialized_count
    _initialized_count = 0
