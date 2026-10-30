# function.__annotations__ comes from __annotate__ when the function has no
# annotations dict of its own (PEP 649).  functools.wraps copies
# __annotate__, not __annotations__, so a wrapper reported {} and
# functools.singledispatch's register() found no annotation to dispatch on.
import functools


def f(arg: int, *, k: str = "") -> bool:
    pass


assert f.__annotations__ == {"arg": int, "k": str, "return": bool}


def deco(fn):
    @functools.wraps(fn)
    def wrapper(*a, **kw):
        return fn(*a, **kw)
    return wrapper


assert deco(f).__annotations__ == f.__annotations__


def g():
    pass


assert g.__annotate__ is None and g.__annotations__ == {}
g.__annotations__["x"] = 1
assert g.__annotations__ == {"x": 1}


def h():
    pass


h.__annotate__ = lambda fmt: {"y": float}
assert h.__annotations__ == {"y": float}


@functools.singledispatch
def fun(arg):
    return "default"


@fun.register
@deco
def _(arg: int):
    return "int"


assert fun(1) == "int" and fun("s") == "default"
print("function_annotations_from_annotate: ok")
