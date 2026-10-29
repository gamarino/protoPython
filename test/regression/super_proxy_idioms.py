# The idioms a super() proxy serves, after its cells stopped being bound to
# the proxy (make_super_proxy).  Every read and write reaches py_super_getattr
# or py_super_setattr with the proxy as an argument; none may fail or answer
# for the wrong receiver.
import enum


class Base:
    v = "base-v"

    def __init__(self, x):
        self.x = x

    def m(self):
        return "base-m"

    def who(self):
        return type(self).__name__


class Derived(Base):
    def __init__(self, x):
        super().__init__(x * 2)

    def m(self):
        return "derived>" + super().m()

    def explicit(self):
        return super(Derived, self).m()

    def attr(self):
        return super().v

    def held(self):
        s = super()
        return s.m, s

    def via_getattr(self):
        return getattr(super(), "m")()

    def via_getattribute(self):
        s = super()
        return super.__getattribute__(s, "m")()

    def set_through(self):
        super().__setattr__("y", 7)
        return self.y

    def missing(self):
        try:
            super().nope
        except AttributeError:
            return "AttributeError"
        return "no error"


d = Derived(3)
assert d.x == 6, d.x
assert d.m() == "derived>base-m"
assert d.explicit() == "base-m"
assert d.attr() == "base-v"
bound, proxy = d.held()
assert bound() == "base-m"
assert type(proxy).__name__ == "super", type(proxy).__name__
assert proxy.who() == "Derived"
assert proxy.__class__ is super
assert d.via_getattr() == "base-m"
assert d.via_getattribute() == "base-m"
assert d.set_through() == 7
assert d.missing() == "AttributeError"


class Meta(type):
    def __setattr__(cls, name, value):
        super().__setattr__(name, value)


class WithMeta(metaclass=Meta):
    pass


WithMeta.k = 5
assert WithMeta.k == 5


class Color(enum.IntEnum):
    RED = 1
    GREEN = 2


assert Color.GREEN.value == 2 and Color(1) is Color.RED

# A proxy outlives the frame that made it, and many proxies coexist.
proxies = [super(Derived, Derived(i)) for i in range(50)]
assert [p.m() for p in proxies] == ["base-m"] * 50
assert [p.who() for p in proxies] == ["Derived"] * 50
print("super_proxy_idioms ok")
