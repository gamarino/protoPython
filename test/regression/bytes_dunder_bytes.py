# bytes(x) calls type(x).__bytes__ bound to x: the method bound by its
# __get__ was called with x again, so every def __bytes__(self) failed with
# "takes 1 positional argument but 2 were given" (bytes(array) among them).
import array


class B:
    def __bytes__(self):
        return b"zz"


class Sub(B):
    pass


assert bytes(B()) == b"zz"
assert bytes(Sub()) == b"zz"
assert bytes(array.array("B", b"abc")) == b"abc"
assert bytes(memoryview(b"xy")) == b"xy"
assert memoryview(array.array("B", b"abc")).tobytes() == b"abc"


class Descr:
    def __get__(self, obj, owner):
        return lambda: b"via descriptor"


class C:
    __bytes__ = Descr()


assert bytes(C()) == b"via descriptor"
print("bytes_dunder_bytes: ok")
