# object.__setattr__(obj, name, value) is object's own store: it does not
# call the __setattr__ that obj's class defines.  It used to dispatch to
# that override, so every class that makes itself immutable with a raising
# __setattr__ and fills itself in with object.__setattr__ failed in its own
# __init__: frozen dataclasses, uuid.UUID (`import uuid` failed, and with it
# test_os), ...
import dataclasses
import uuid


class Frozen:
    __slots__ = ("a", "__weakref__")

    def __init__(self, a):
        object.__setattr__(self, "a", a)

    def __setattr__(self, name, value):
        raise TypeError("immutable")


class Plain:
    def __init__(self, a):
        object.__setattr__(self, "a", a)

    def __setattr__(self, name, value):
        raise TypeError("immutable")


class Logged:
    def __init__(self):
        self.log = []          # through __setattr__ below

    def __setattr__(self, name, value):
        object.__setattr__(self, "seen", name)
        super().__setattr__(name, value)


assert Frozen(1).a == 1 and Plain(2).a == 2
for o in (Frozen(1), Plain(2)):
    try:
        o.a = 5
    except TypeError:
        pass
    else:
        raise AssertionError("the override did not run")
lg = Logged()
assert lg.log == [] and lg.seen == "log"
lg.x = 3
assert lg.x == 3 and lg.seen == "x"


@dataclasses.dataclass(frozen=True)
class P:
    x: int
    y: int = 2


p = P(1)
assert (p.x, p.y) == (1, 2) and repr(p) == "P(x=1, y=2)"
try:
    p.x = 3
except dataclasses.FrozenInstanceError:
    pass
else:
    raise AssertionError("frozen dataclass accepted a write")

u = uuid.UUID("12345678123456781234567812345678")
assert str(u) == "12345678-1234-5678-1234-567812345678"
assert uuid.NAMESPACE_DNS.version == 1
# UUID.bytes is int.to_bytes(16): a 128-bit value (asLong() overflowed).
assert u.bytes == bytes.fromhex("12345678123456781234567812345678")
assert (2**127 + 5).to_bytes(16, "big")[-1] == 5
assert (-(2**100)).to_bytes(16, "big", signed=True)[0] == 0xff
for value in (2**64, -1):
    try:
        value.to_bytes(8, "big")
    except OverflowError:
        pass
    else:
        raise AssertionError("to_bytes accepted %r" % value)
print("object_setattr_generic: ok")
