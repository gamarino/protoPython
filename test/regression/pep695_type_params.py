# Regression test: PEP 695 type parameters and the `type` statement.
#
# The parser accepted `class A[T]`, `def f[T]` and `type X = ...` but the
# compiler dropped the type parameters: no __type_params__, T unbound inside
# the class body and annotations, no Generic base, and `type X = v` just
# stored v.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


import typing


class A[T]:
    """Docstring survives."""

    def get(self) -> T:
        return None


(T_A,) = A.__type_params__
check("class type param name", T_A.__name__, "T")
check("class type param kind", type(T_A).__name__, "TypeVar")
check("class Generic base", typing.Generic in A.__mro__, True)
check("class __parameters__", A.__parameters__, (T_A,))
check("class annotation sees T", A.get.__annotations__["return"] is T_A, True)
check("class qualname", A.__qualname__, "A")
check("class docstring", A.__doc__, "Docstring survives.")
check("T not leaked to module", "T" in globals(), False)


class B[K, V](dict):
    pass


check("B type params", [p.__name__ for p in B.__type_params__], ["K", "V"])
check("B mro", B.__mro__, (B, dict, typing.Generic, object))
check("B instance", B({1: 2}), {1: 2})


def f[U](x: U) -> U:
    return x


(U_f,) = f.__type_params__
check("function type params", U_f.__name__, "U")
check("function annotations", f.__annotations__, {"x": U_f, "return": U_f})
check("function call", f(3), 3)
check("function qualname", f.__qualname__, "f")


def bounded[T: int, S: (int, str), *Ts, **P](a: T) -> S:
    return a


T_b, S_b, Ts_b, P_b = bounded.__type_params__
check("bound", T_b.__bound__, int)
check("constraints", S_b.__constraints__, (int, str))
check("TypeVarTuple", type(Ts_b).__name__, "TypeVarTuple")
check("ParamSpec", type(P_b).__name__, "ParamSpec")


def deco(fn):
    fn.decorated = True
    return fn


@deco
def g[T](x: T) -> T:
    return x


check("decorated generic function", (g.decorated, [p.__name__ for p in g.__type_params__]), (True, ["T"]))


class Outer:
    def method[T](self, x: T, y=3) -> T:
        return x

    class Inner[T]:
        pass


check("method type params", [p.__name__ for p in Outer.method.__type_params__], ["T"])
check("method qualname", Outer.method.__qualname__, "Outer.method")
check("method default", Outer().method(1), 1)
check("nested class qualname", Outer.Inner.__qualname__, "Outer.Inner")


def factory():
    class Local[T]:
        def ident(self, x: T) -> T:
            return x
    return Local


Local = factory()
check("local generic class", Local().ident(5), 5)
check("local generic qualname", Local.__qualname__, "factory.<locals>.Local")


type Alias = list[int]
check("alias name", Alias.__name__, "Alias")
check("alias value", Alias.__value__, list[int])
check("alias type", type(Alias).__name__, "TypeAliasType")

type Forward = list[Later]


class Later:
    pass


check("alias value is lazy", Forward.__value__, list[Later])

type Pair[X] = tuple[X, X]
(X_p,) = Pair.__type_params__
check("generic alias params", X_p.__name__, "X")
check("generic alias value", Pair.__value__, tuple[X_p, X_p])

print("pep695 type params OK")
