"""*Ts is Unpack[Ts]: Generic[T, *Ts] collects Ts and substitutes variadic arguments."""
import typing
from typing import Generic, ParamSpec, TypeVar, TypeVarTuple

T = TypeVar("T")
Ts = TypeVarTuple("Ts")
P = ParamSpec("P")


class G(Generic[T, *Ts]):
    pass


(unpacked,) = list(Ts)
assert unpacked.__origin__ is typing.Unpack and unpacked.__args__ == (Ts,)
assert G.__parameters__ == (T, Ts), G.__parameters__
assert G[int, str, float].__args__ == (int, str, float)
assert G[int].__args__ == (int,)

try:
    Ts.__typing_subst__(int)
except TypeError:
    pass
else:
    raise AssertionError("a bare TypeVarTuple cannot be substituted")


class C(Generic[P]):
    pass


assert C[[int, str]].__args__ == ((int, str),)
assert typing.List[T][int] == typing.List[int]

print("typevartuple generic OK")
