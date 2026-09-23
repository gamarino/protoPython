# A metaclass belongs to the class object, not to its instances.
#
# protoPython builds a class as a protoCore object whose parent chain is the
# class's C3 MRO. Earlier rounds also appended the metaclass and its ancestors
# to that chain as a lookup fallback; protoCore 2.0.0's `newChild` captures the
# receiver's current chain, so every instance then inherited the metaclass tail
# as well: `isinstance(inst, SomeMeta)` answered True and metaclass-only
# attributes resolved from instances. Both diverge from CPython.


class CustomMeta(type):
    meta_only_attr = "META"

    def meta_only_method(cls):
        return "meta"


class WithMeta(metaclass=CustomMeta):
    cls_attr = "CLS"

    def inst_method(self):
        return "inst"


inst = WithMeta()

# An instance is not an instance of its class's metaclass.
assert isinstance(inst, CustomMeta) is False
assert isinstance(inst, WithMeta) is True
assert isinstance(inst, type) is False
# ... but the class is, and the metaclass is still a type.
assert isinstance(WithMeta, CustomMeta) is True
assert isinstance(WithMeta, type) is True
assert isinstance(CustomMeta, type) is True

# Metaclass attributes are reachable from the class, not from its instances.
assert WithMeta.meta_only_attr == "META"
assert WithMeta.meta_only_method() == "meta"
assert hasattr(WithMeta, "meta_only_attr") is True
assert hasattr(inst, "meta_only_attr") is False
assert hasattr(inst, "meta_only_method") is False
try:
    inst.meta_only_attr
except AttributeError:
    pass
else:
    raise AssertionError("instance saw a metaclass-only attribute")
try:
    inst.meta_only_method
except AttributeError:
    pass
else:
    raise AssertionError("instance saw a metaclass-only method")

# Class attributes and instance methods keep working.
assert inst.cls_attr == "CLS"
assert inst.inst_method() == "inst"
assert type(inst) is WithMeta
assert type(WithMeta) is CustomMeta

# A class is not a subclass of its metaclass, and __mro__ excludes it.
assert issubclass(WithMeta, CustomMeta) is False
assert issubclass(WithMeta, object) is True
assert issubclass(CustomMeta, type) is True
assert WithMeta.__mro__ == (WithMeta, object)
assert CustomMeta.__mro__ == (CustomMeta, type, object)


# Inheritance from a class that has a metaclass: the derived class inherits the
# metaclass (so its own instances must not see it either), and the MRO chain
# still resolves base-class attributes.
class Derived(WithMeta):
    pass


d = Derived()
assert type(Derived) is CustomMeta
assert isinstance(Derived, CustomMeta) is True
assert isinstance(d, CustomMeta) is False
assert isinstance(d, WithMeta) is True
assert d.cls_attr == "CLS"
assert d.inst_method() == "inst"
assert hasattr(d, "meta_only_attr") is False
assert Derived.__mro__ == (Derived, WithMeta, object)


# Exception matching is unaffected, and an exception instance does not become
# an instance of its class's metaclass.
class ErrMeta(type):
    pass


class MetaErr(Exception, metaclass=ErrMeta):
    pass


class SubErr(MetaErr):
    pass


try:
    raise SubErr("boom")
except MetaErr as exc:
    assert isinstance(exc, SubErr)
    assert isinstance(exc, Exception)
    assert isinstance(exc, ErrMeta) is False
else:
    raise AssertionError("SubErr was not caught by MetaErr")


# Built-in subclasses keep their layout behaviour with a custom metaclass.
class DictMeta(type):
    pass


class MetaDict(dict, metaclass=DictMeta):
    pass


md = MetaDict(b=2)
assert isinstance(md, dict) is True
assert isinstance(md, MetaDict) is True
assert isinstance(md, DictMeta) is False
assert md["b"] == 2


class MyStr(str):
    pass


class MyList(list):
    pass


assert isinstance(MyStr("hi"), str) is True
assert MyStr("hi").upper() == "HI"
assert isinstance(MyList([1, 2]), list) is True
assert len(MyList([1, 2])) == 2


# ABCMeta is the metaclass the standard library exercises this path with.
import abc


class Shape(abc.ABC):
    @abc.abstractmethod
    def area(self):
        ...


class Square(Shape):
    def area(self):
        return 4


sq = Square()
assert isinstance(sq, Shape) is True
assert isinstance(Shape, abc.ABCMeta) is True
assert isinstance(sq, abc.ABCMeta) is False
# `register` is an ABCMeta method: available on the class, not on instances.
assert hasattr(Shape, "register") is True
assert hasattr(sq, "register") is False

print("metaclass_not_inherited: ok")
