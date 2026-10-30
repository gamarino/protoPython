# The code type exposes its co_* attributes as members, as in CPython, so
# dir(types.CodeType) lists them.  unittest.mock's spec_set=CodeType (used by
# AsyncMock for every async function it mocks) accepts only the names dir()
# reports, so with none listed every AsyncMock raised AttributeError.
import types
from unittest import mock

names = set(dir(types.CodeType))
for n in ("co_argcount", "co_flags", "co_varnames", "co_posonlyargcount",
          "co_kwonlyargcount", "co_name", "co_qualname", "co_filename",
          "co_firstlineno", "co_consts", "co_names", "co_freevars",
          "co_cellvars", "co_nlocals"):
    assert n in names, n


def f(a, b=1, *, c):
    x = a
    return x


c = f.__code__
assert c.co_argcount == 2, c.co_argcount
assert c.co_kwonlyargcount == 1
assert c.co_posonlyargcount == 0
assert c.co_name == "f" and c.co_qualname == "f"
assert c.co_varnames[:3] == ("a", "b", "c"), c.co_varnames
assert c.co_nlocals == len(c.co_varnames)
assert c.co_freevars == () and c.co_cellvars == ()
assert isinstance(c.co_flags, int)

# Module code has every member too.
m = compile("y = 1", "<s>", "exec")
assert m.co_argcount == 0 and m.co_posonlyargcount == 0
assert m.co_freevars == () and m.co_qualname == "<module>", m.co_qualname

# Class-level access gives the descriptor.
assert type(types.CodeType.__dict__["co_flags"]).__name__ == "member_descriptor" \
    if hasattr(types.CodeType, "__dict__") else True

# The members are read-only.
try:
    c.co_flags = 0
except AttributeError:
    pass
else:
    raise AssertionError("co_flags is writable")


async def coro(x):
    return x


m = mock.AsyncMock(spec=coro)
assert m.__code__.co_flags & 0x80, m.__code__.co_flags  # CO_COROUTINE
print("code_type_members: ok")
