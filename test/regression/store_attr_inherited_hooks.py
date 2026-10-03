# Regression test: `obj.name = value` honours a `__setattr__` override and a
# data descriptor that the object's class INHERITS, not only the ones the
# class defines itself.
#
# OP_STORE_ATTR's fast path wrote the attribute directly unless the
# instance's own class owned `__setattr__` or an attribute named `name`; a
# hook defined on a base class was bypassed. Every expected value below is
# what CPython prints.


class Times10:
    def __setattr__(self, name, value):
        object.__setattr__(self, name, value * 10)


class FromBase(Times10):
    def __init__(self):
        self.x = 1


class FromGrandBase(FromBase):
    def __init__(self):
        self.y = 2


assert FromBase().x == 10, "inherited __setattr__ was bypassed"
assert FromGrandBase().y == 20, "__setattr__ two levels up was bypassed"


class WithProperty:
    @property
    def p(self):
        return self._p

    @p.setter
    def p(self, v):
        self._p = v + 100


class InheritsProperty(WithProperty):
    def __init__(self):
        self.p = 1


ip = InheritsProperty()
assert ip.p == 101, "inherited property setter was bypassed: %r" % (ip.p,)
assert "p" not in vars(ip), "the property's name was stored on the instance"


class Doubling:
    def __set_name__(self, owner, name):
        self.store = "_" + name

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return getattr(obj, self.store)

    def __set__(self, obj, value):
        setattr(obj, self.store, value * 2)


class Base:
    d = Doubling()


class Derived(Base):
    def __init__(self, v):
        self.d = v


assert Derived(5).d == 10, "inherited data descriptor was bypassed"


class ReadOnly:
    @property
    def r(self):
        return 1


class DerivedReadOnly(ReadOnly):
    pass


try:
    DerivedReadOnly().r = 2
    raise AssertionError("assigning an inherited read-only property did not raise")
except AttributeError:
    pass


# A plain class attribute on a base is a default, not a descriptor: the
# instance gets its own value and the class keeps its own.
class Defaults:
    count = 0


class UsesDefaults(Defaults):
    def __init__(self, c):
        self.count = c


u = UsesDefaults(7)
assert u.count == 7 and UsesDefaults.count == 0 and Defaults.count == 0


# A class stored as a class attribute is not a descriptor, even when it
# defines __set__ itself.
class HasSetMethod:
    def __set__(self, obj, value):
        raise AssertionError("a class attribute that is a class acted as a descriptor")


class Holder:
    kind = HasSetMethod


class SubHolder(Holder):
    def __init__(self):
        self.kind = 3


assert SubHolder().kind == 3

print("store_attr_inherited_hooks: ok")
