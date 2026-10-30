# An assignment expression inside a function binds a local of the function,
# wherever it appears (an if/while test, a call argument, a return value).
# It bound a global: closures made by different calls shared one binding.
def outer(v):
    def deco(f):
        if ex := v:
            def inner():
                return ex
        return inner
    return deco


a, b = outer(1)(None), outer(2)(None)
assert (a(), b()) == (1, 2), (a(), b())


def mk(v):
    if x := v:
        return lambda: x


assert (mk(1)(), mk(2)()) == (1, 2)


def loop(items):
    it = iter(items)
    out = []
    while (n := next(it, None)) is not None:
        out.append(n)
    return out, n


assert loop([1, 2]) == ([1, 2], None)


def arg(v):
    print(y := v, end="") if False else None
    return len(z := [v, v]), z


assert arg(3) == (2, [3, 3])
for name in ("ex", "x", "n", "y", "z"):
    assert name not in globals(), name
try:
    def unbound():
        if False:
            (w := 1)
        return w
    unbound()
except UnboundLocalError:
    pass
else:
    raise AssertionError("w is not a local")
print("walrus_function_scope: ok")
