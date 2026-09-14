# Regression test: type's own getset descriptors resolve on classes, and their
# repr works.
#
# For `type` itself or a metaclass M(type), attribute lookup found type's
# __qualname__ / __doc__ getset descriptor in the class's own MRO and called
# __get__(None, cls), which returns the descriptor: `type.__qualname__` was a
# getset_descriptor instead of 'type'. CPython gives a data descriptor defined
# on the metatype precedence. repr() of a getset descriptor also raised
# "'str' object is not callable" because its __repr__ was a placeholder string.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


check("type.__qualname__", type.__qualname__, "type")


class Meta(type):
    """Meta doc."""


check("metaclass __doc__", Meta.__doc__, "Meta doc.")
check("metaclass __qualname__", Meta.__qualname__, "Meta")


class Plain:
    """Plain doc."""


check("class __doc__", Plain.__doc__, "Plain doc.")
check("class __qualname__", Plain.__qualname__, "Plain")
check("f-string of type.__qualname__", f"{type.__qualname__}", "type")
check("getset repr", repr(type.__dict__["__qualname__"]).startswith("<attribute '__qualname__' of"), True)

print("type getset descriptors OK")
