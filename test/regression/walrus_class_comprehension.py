# PEP 572: `:=` in a comprehension directly in a class body is a SyntaxError;
# in a function it binds the name in the function scope.
def f():
    [(last := i) for i in range(3)]
    return last
assert f() == 2

for src in ("class K:\n    v = [(q := i) for i in range(2)]\n",
            "class K:\n    v = [i for i in range(2) if (q := i)]\n",
            "class K:\n    v = {(q := i): 1 for i in range(2)}\n"):
    try:
        compile(src, "<test>", "exec")
    except SyntaxError as e:
        assert "class body" in str(e), e
    else:
        raise AssertionError("SyntaxError expected for %r" % src)

class Ok:
    v = [i for i in range(3)]
    def m(self):
        return [(w := i) for i in range(2)], w
assert Ok.v == [0, 1, 2] and Ok().m() == ([0, 1], 1)
print("walrus_class_comprehension: ok")
