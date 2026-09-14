# Regression test: TypeVar, ParamSpec, TypeVarTuple and NewType record the
# module that created them, the type variables pickle by reference, and a
# user generic alias reprs with the class's module-qualified name.
#
# The _typing stub set TypeVar.__module__ = None and never set it for
# ParamSpec / TypeVarTuple (they reported "_typing"); none of them had
# __reduce__, so pickle copied them.  sys._getframemodulename was missing and
# sys._getframe cannot see callers of leaf functions, so typing._caller()
# returned None (NewType.__module__ was None).  isinstance(x, (type, ...))
# ignored `type` inside a tuple, so annotationlib.type_repr fell back to
# repr() and Box[int] printed "<class 'Box'>[<class 'int'>]".
import pickle
import sys
from typing import Generic, NewType, ParamSpec, TypeVar, TypeVarTuple


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


check("sys._getframemodulename()", sys._getframemodulename(), __name__)


def one_level_down():
    return sys._getframemodulename(0), sys._getframemodulename(1)


check("sys._getframemodulename(0/1) in a function", one_level_down(), (__name__, __name__))
check("sys._getframemodulename past the outermost scope", sys._getframemodulename(10000), None)

T = TypeVar("T")
P = ParamSpec("P")
Ts = TypeVarTuple("Ts")
UserId = NewType("UserId", int)
for obj in (T, P, Ts, UserId):
    check("%r.__module__" % (obj,), obj.__module__, __name__)
for obj in (T, P, Ts):
    check("pickle round-trip of %r" % (obj,), pickle.loads(pickle.dumps(obj)) is obj, True)


class Box(Generic[T]):
    pass


check("isinstance(class, (int, type))", isinstance(Box, (int, type)), True)
check("isinstance(int, (type,))", isinstance(int, (type,)), True)
check("isinstance(1, ((str,), int))", isinstance(1, ((str,), int)), True)
check("isinstance(1, ())", isinstance(1, ()), False)
check("repr(Box[int])", repr(Box[int]), "%s.Box[int]" % __name__)

print("typing module attrs OK")
