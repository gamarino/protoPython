# Regression test: PEP 695 corners left over from the first round.
#
# - `async def f[T]` ignored its type parameters (no __type_params__);
# - `type X = v` (and `async def`) inside a function bound a global, because
#   the local-name scan did not know those statements;
# - `class C(metaclass=M, **{"metaclass": N})` silently took the last value
#   instead of raising TypeError;
# - plain functions and classes had no `__type_params__` (CPython: `()`).


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


import inspect


class Echo:
    """Awaitable that yields its value once: drives a coroutine one step."""

    def __init__(self, value):
        self.value = value

    def __await__(self):
        return (yield self.value)


async def af[T](x: T) -> T:
    return await Echo(x)


(T_af,) = af.__type_params__
check("async type parameter", T_af.__name__, "T")
check("async generic is a coroutine function", inspect.iscoroutinefunction(af), True)
coro = af(3)
check("calling it makes a coroutine", type(coro).__name__, "coroutine")
check("the coroutine runs", coro.send(None), 3)
coro.close()


class Box:
    async def get[U](self, x: U) -> U:
        return await Echo(x)


check("async generic method", [p.__name__ for p in Box.get.__type_params__], ["U"])
coro = Box().get(7)
check("async generic method runs", coro.send(None), 7)
coro.close()


def make_async():
    async def local_async_probe[V](v: V):
        return v
    return local_async_probe


check("local async generic", [p.__name__ for p in make_async().__type_params__], ["V"])
try:
    local_async_probe
    leaked = True
except NameError:
    leaked = False
check("local async def is not a global", leaked, False)


def local_alias():
    type LocalAliasProbe = list[int]
    return LocalAliasProbe


check("local alias value", local_alias().__value__, list[int])
try:
    LocalAliasProbe
    leaked = True
except NameError:
    leaked = False
check("local alias is not a global", leaked, False)


def local_generic_alias():
    type LocalPairProbe[X] = tuple[X, X]
    return LocalPairProbe


(X_p,) = local_generic_alias().__type_params__
check("local generic alias", local_generic_alias().__name__, "LocalPairProbe")
check("local generic alias parameter", X_p.__name__, "X")
try:
    LocalPairProbe
    leaked = True
except NameError:
    leaked = False
check("local generic alias is not a global", leaked, False)


class M(type):
    pass


class N(type):
    pass


def class_error(make):
    try:
        make()
    except TypeError as exc:
        return str(exc)
    return None


def dup_keyword_and_unpack():
    class C(metaclass=M, **{"metaclass": N}):
        pass


def dup_two_unpacks():
    class C(**{"metaclass": M}, **{"metaclass": N}):
        pass


def dup_unpack_then_keyword():
    class C(**{"metaclass": M}, metaclass=N):
        pass


suffix = "got multiple values for keyword argument 'metaclass'"
for label, make in [("keyword then **", dup_keyword_and_unpack),
                    ("** then **", dup_two_unpacks),
                    ("** then keyword", dup_unpack_then_keyword)]:
    err = class_error(make)
    check("duplicate metaclass: " + label, err is not None and err.endswith(suffix), True)


class E(metaclass=M, **{}):
    pass


check("metaclass with empty **", type(E) is M, True)


class Kw:
    def __init_subclass__(cls, **kw):
        cls.kw = kw


class F(Kw, a=1, **{"b": 2}):
    pass


check("keywords and ** merge", F.kw, {"a": 1, "b": 2})


def plain():
    pass


async def plain_async():
    pass


class Plain:
    pass


check("function __type_params__", plain.__type_params__, ())
check("lambda __type_params__", (lambda: 0).__type_params__, ())
check("async function __type_params__", plain_async.__type_params__, ())
check("class __type_params__", Plain.__type_params__, ())
check("builtin class __type_params__", int.__type_params__, ())
check("class default is not in __dict__", "__type_params__" in Plain.__dict__, False)


def settable():
    pass


settable.__type_params__ = (T_af,)
check("assigned __type_params__", settable.__type_params__, (T_af,))
check("default unaffected by assignment", plain.__type_params__, ())

print("pep695 extras OK")
