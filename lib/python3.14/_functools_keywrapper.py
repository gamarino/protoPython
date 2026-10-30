"""Key wrapper behind protoPython's native _functools.cmp_to_key.

_functools.cmp_to_key is a builtin, as in CPython, so it is not bound as a
method when stored on a class; the wrapper it returns is defined here.
"""


def cmp_to_key(mycmp):
    """Convert a cmp= function into a key= function"""
    class KeyWrapper(object):
        __slots__ = ['obj']

        def __init__(self, obj):
            self.obj = obj

        def __lt__(self, other):
            return mycmp(self.obj, other.obj) < 0

        def __gt__(self, other):
            return mycmp(self.obj, other.obj) > 0

        def __eq__(self, other):
            return mycmp(self.obj, other.obj) == 0

        def __le__(self, other):
            return mycmp(self.obj, other.obj) <= 0

        def __ge__(self, other):
            return mycmp(self.obj, other.obj) >= 0

        __hash__ = None

    KeyWrapper.__module__ = 'functools'
    return KeyWrapper
