# Regression test: reversed() passes the receiver to __reversed__ and __len__.
#
# py_reversed looked __reversed__ up on the type and called it with no
# arguments, so a native __reversed__ got no receiver: reversed([1, 2]) was
# empty and a Python __reversed__ ran without self. The sequence-protocol
# fallback called a native __len__ the same way.
def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


check("list", list(reversed([1, 2, 3])), [3, 2, 1])
check("next(reversed(list))", next(reversed([1, 2])), 2)
check("empty list", list(reversed([])), [])
check("tuple", list(reversed((1, 2))), [2, 1])
check("range", list(reversed(range(3))), [2, 1, 0])
check("str (sequence protocol, native __len__)", list(reversed("abc")), ["c", "b", "a"])
check("dict", list(reversed({"a": 1, "b": 2})), ["b", "a"])
check("dict keys", list(reversed({"a": 1, "b": 2}.keys())), ["b", "a"])
check("dict values", list(reversed({"a": 1, "b": 2}.values())), [2, 1])
check("dict items", list(reversed({"a": 1, "b": 2}.items())), [("b", 2), ("a", 1)])


class L(list):
    pass


class T(tuple):
    pass


check("list subclass", list(reversed(L([1, 2]))), [2, 1])
check("list.__reversed__ unbound", list(list.__reversed__(L([1, 2]))), [2, 1])
check("tuple subclass", list(reversed(T((1, 2)))), [2, 1])


class R:
    def __init__(self):
        self.items = [1, 2]

    def __reversed__(self):
        return iter(self.items[::-1])


check("Python __reversed__ receives self", list(reversed(R())), [2, 1])


class Q:
    def __len__(self):
        return 2

    def __getitem__(self, i):
        if i >= 2:
            raise IndexError(i)
        return "q%d" % i


check("sequence protocol", list(reversed(Q())), ["q1", "q0"])

print("reversed native OK")
