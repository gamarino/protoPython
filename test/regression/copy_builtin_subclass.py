# copy.copy() and copy.deepcopy() of instances of tuple, str, bytes, int and
# float subclasses keep their value. object.__reduce_ex__ passes the result of
# __getnewargs__() to cls.__new__, and the built-in types did not define it,
# so a tuple subclass instance was rebuilt as ().
import copy


class T(tuple):
    pass


class S(str):
    pass


class B(bytes):
    pass


class I(int):
    pass


class F(float):
    pass


cases = [
    (T, (1, 2)),
    (S, "text"),
    (B, b"data"),
    (I, 5),
    (I, 2 ** 80),
    (F, 1.5),
]
for cls, value in cases:
    original = cls(value)
    for copier in (copy.copy, copy.deepcopy):
        duplicate = copier(original)
        assert type(duplicate) is cls, (copier.__name__, cls.__name__, type(duplicate))
        assert duplicate == value, (copier.__name__, cls.__name__, duplicate)


# Attributes set on the instance are copied along with the value.
t = T((1, 2))
t.label = "pair"
c = copy.copy(t)
assert c == (1, 2) and c.label == "pair"

# __getnewargs__ returns a tuple holding the plain built-in value.
assert (1, 2).__getnewargs__() == ((1, 2),)
assert T((1, 2)).__getnewargs__() == ((1, 2),)
assert type(T((1, 2)).__getnewargs__()[0]) is tuple
assert "ab".__getnewargs__() == ("ab",)
assert (7).__getnewargs__() == (7,)
assert (2.5).__getnewargs__() == (2.5,)
assert b"ab".__getnewargs__() == (b"ab",)
assert tuple.__getnewargs__((3,)) == ((3,),)

print("copy_builtin_subclass: ok")
