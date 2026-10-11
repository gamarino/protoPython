# Regression test: list, tuple and dict reprs show every item, and a
# container that contains itself shows `[...]` / `{...}` (CPython's
# Py_ReprEnter), instead of stopping after 20 items and recursing until the
# stack overflowed.  Every expected string is CPython's.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


check("list of 25", repr(list(range(25))), "[" + ", ".join(str(i) for i in range(25)) + "]")
check("tuple of 25", repr(tuple(range(25))), "(" + ", ".join(str(i) for i in range(25)) + ")")
check("dict of 22", repr({i: i for i in range(22)}),
      "{" + ", ".join("%d: %d" % (i, i) for i in range(22)) + "}")
check("str of list", str(list(range(30))), "[" + ", ".join(str(i) for i in range(30)) + "]")
check("nested long", repr([list(range(21))]), "[[" + ", ".join(str(i) for i in range(21)) + "]]")

a = []
a.append(a)
check("self list", repr(a), "[[...]]")
check("self list str", str(a), "[[...]]")
d = {}
d["self"] = d
check("self dict", repr(d), "{'self': {...}}")
x = [1]
y = [x]
x.append(y)
check("mutual lists", repr(x), "[1, [[...]]]")
check("mutual lists from y", repr(y), "[[1, [...]]]")
m = {"l": []}
m["l"].append(m)
check("dict in list in dict", repr(m), "{'l': [{...}]}")
t = ([],)
t[0].append(t)
check("tuple in list in tuple", repr(t), "([(...)],)")
# The same container twice at one level is not recursion.
z = [1]
check("shared, not recursive", repr([z, z]), "[[1], [1]]")
print("repr containers OK")
