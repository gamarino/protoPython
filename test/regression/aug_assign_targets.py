# Regression test: augmented assignment to a subscript or an attribute stores
# the result back, evaluating the target expressions once.
#
# compileAugAssign left [container, key, result] on the stack and emitted
# STORE_SUBSCR, which expects [result, container, key]: the result went into
# the wrong object, so `d[k] += 1` and `l[i] += 10` left the container
# unchanged (a list element with `+=` only looked right when __iadd__ mutated
# in place). The attribute form evaluated the object expression a second time
# for the store and stored under the unmangled name of a private attribute.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


d = {"a": 1}
d["a"] += 1
check("dict item +=", d, {"a": 2})

l = [1, 2]
l[0] += 10
l[-1] *= 3
check("list item += and *=", l, [11, 6])

counts = {}
for ch in "abracadabra":
    counts[ch] = counts.get(ch, 0)
    counts[ch] += 1
check("counting", counts, {"a": 5, "b": 2, "r": 2, "c": 1, "d": 1})

nested = {"x": [0, 0]}
nested["x"][1] -= 4
check("nested subscript", nested, {"x": [0, -4]})

calls = []


def key():
    calls.append("key")
    return "k"


m = {"k": 5}
m[key()] //= 2
check("subscript key evaluated once", (m, calls), ({"k": 2}, ["key"]))


class Box:
    def __init__(self):
        self.n = 1
        self.__hidden = 10

    def bump(self):
        self.__hidden += 5
        return self.__hidden


made = []
b = Box()


def box():
    made.append(1)
    return b


box().n += 41
check("attribute +=", b.n, 42)
check("attribute object evaluated once", len(made), 1)
check("private attribute +=", b.bump(), 15)

print("aug assign targets OK")
