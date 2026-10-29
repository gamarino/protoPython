# object.__subclasshook__ is a classmethod that answers NotImplemented, so
# issubclass/isinstance fall through to the normal checks; an ABC that
# overrides it decides by structure, and super() reaches object's.  Matches
# CPython 3.14.
import abc
import collections.abc as cabc


class Plain:
    pass


assert "__subclasshook__" in object.__dict__
assert object.__subclasshook__(int) is NotImplemented
assert Plain.__subclasshook__(int) is NotImplemented
assert Plain().__subclasshook__(int) is NotImplemented
assert int.__subclasshook__(str) is NotImplemented

for call in (lambda: object.__subclasshook__(), lambda: object.__subclasshook__(int, str)):
    try:
        call()
    except TypeError as e:
        assert "takes exactly one argument" in str(e), str(e)
    else:
        raise AssertionError("expected TypeError")


class Sized(abc.ABC):
    @classmethod
    def __subclasshook__(cls, C):
        if cls is Sized and any("__len__" in B.__dict__ for B in C.__mro__):
            return True
        return super().__subclasshook__(C)


class HasLen:
    def __len__(self):
        return 0


class NoLen:
    pass


assert Sized.__subclasshook__(NoLen) is NotImplemented
assert issubclass(HasLen, Sized)
assert isinstance(HasLen(), Sized)
assert not issubclass(NoLen, Sized)


class Hookless(abc.ABC):
    pass


Hookless.register(NoLen)
assert Hookless.__subclasshook__(NoLen) is NotImplemented
assert issubclass(NoLen, Hookless)
assert not issubclass(HasLen, Hookless)


class Explicit(Sized):
    pass


assert issubclass(Explicit, Sized)
assert not issubclass(HasLen, Explicit)  # the hook only answers for Sized

assert issubclass(HasLen, cabc.Sized)
assert not issubclass(NoLen, cabc.Sized)
assert isinstance([], cabc.Iterable)


class Bad(abc.ABC):
    @classmethod
    def __subclasshook__(cls, C):
        return 1


try:
    issubclass(int, Bad)
except AssertionError:
    pass
else:
    raise AssertionError("a non-bool hook result must be rejected")

print("object_subclasshook: ok")
