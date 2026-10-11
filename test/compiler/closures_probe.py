# Closures and argument binding in a module protopyc compiles
# (test/compiler/protopyc_closures.sh): every printed value is CPython's
# (closures.expected).  Closures hold cells: late binding, nonlocal, del,
# recursion, class bodies, generators; calls bind positional and keyword
# arguments, defaults (evaluated at the definition), keyword-only and
# positional-only parameters, *args and **kwargs, with CPython's TypeErrors.


def late():
    x = 1
    def get():
        return x
    x = 2
    return get()


def counter():
    n = 0
    def inc():
        nonlocal n
        n += 1
        return n
    def peek():
        return n
    inc()
    inc()
    return inc(), peek(), n


def loop_closures():
    fs = []
    for i in range(3):
        fs.append(lambda: i)
    return [f() for f in fs]


def default_capture():
    fs = []
    for i in range(3):
        fs.append(lambda i=i: i)
    return [f() for f in fs]


def rec_outer(k):
    def rec(n):
        return k if n == 0 else rec(n - 1)
    return rec(4)


def rec_value():
    def walk(n):
        return [n] + (walk(n - 1) if n else [])
    return walk(3)


def rec_identity():
    def me():
        return me
    return me() is me


def mutual():
    def even(n):
        return True if n == 0 else odd(n - 1)
    def odd(n):
        return False if n == 0 else even(n - 1)
    return even(10), odd(7)


def rebound_recursion():
    def f(n):
        return n if n < 1 else f(n - 1)
    g = f
    def f(n):
        return "second"
    return g(3)


def decorated_recursion():
    calls = []
    def deco(fn):
        def wrapper(n):
            calls.append(n)
            return fn(n)
        return wrapper
    @deco
    def fact(n):
        return 1 if n <= 1 else n * fact(n - 1)
    return fact(4), calls


def recursive_generator():
    def tree(n):
        if n:
            yield n
            yield from tree(n - 1)
    return list(tree(4))


def unbound_free():
    def inner():
        return v
    try:
        inner()
    except NameError as e:
        msg = str(e)
    v = 1
    return msg, inner()


def delete_cell():
    z = 1
    def inner():
        return z
    del z
    try:
        inner()
    except NameError as e:
        return str(e)


def cell_params(a, b=3):
    def inner():
        return a, b
    a = a * 10
    return inner()


def pass_through():
    x = "x0"
    def middle():
        def inner():
            return x
        return inner
    f = middle()
    x = "x1"
    return f()


def deep_nonlocal():
    x = 0
    def middle():
        def inner():
            nonlocal x
            x += 5
        inner()
        return x
    return middle(), x


def class_body():
    a = "outer-a"
    b = "outer-b"
    class C:
        a = "class-a"
        x = a
        y = b
        def m(self):
            return a, b
    return C.x, C.y, C().m()


def class_method_late():
    v = 1
    class C:
        def get(self):
            return v
    v = 2
    return C().get()


def class_method_nonlocal():
    total = 0
    class Acc:
        def add(self, k):
            nonlocal total
            total += k
    acc = Acc()
    acc.add(2)
    acc.add(3)
    return total


def super_in_function():
    class Base:
        def who(self):
            return "base"
    class Derived(Base):
        def who(self):
            return "derived+" + super().who()
    alias = Derived
    return alias().who()


def init_subclass_super():
    made = []
    class Registry:
        def __init_subclass__(cls):
            made.append(cls().tag())
        def tag(self):
            return "registry"
    class Local(Registry):
        def tag(self):
            return "local+" + super().tag()
    return made


def generator_closure():
    base = 100
    def g():
        for i in range(3):
            yield base + i
    it = g()
    first = next(it)
    base = 200
    return first, list(it)


def generator_nonlocal():
    state = []
    def g():
        nonlocal state
        state = state + [1]
        yield len(state)
        state = state + [2]
        yield len(state)
    return list(g()), state


def lambda_chain():
    a = 1
    f = lambda b: lambda c: a + b + c
    a = 5
    return f(10)(100)


# --- argument binding --------------------------------------------------------
def kw(a, b=2, *, c=3, d):
    return a, b, c, d


def star(a, *args, **kwargs):
    return a, args, sorted(kwargs.items())


def posonly(a, b, /, c):
    return a, b, c


def defaults_once(x=[]):
    x.append(1)
    return len(x)


def binding():
    out = []
    out.append(kw(1, d=4))
    out.append(kw(1, 5, d=4, c=6))
    out.append(kw(d=0, a=9))
    out.append(star(1, 2, 3, z=1, y=2))
    out.append(star(a=7))
    out.append(posonly(1, 2, c=3))
    out.append((defaults_once(), defaults_once()))
    out.append((lambda p, q=2, *r, s=4, **t: (p, q, r, s, sorted(t.items())))(1, 3, 5, s=6, u=7))
    for call in (lambda: kw(), lambda: kw(1, 2, 3, d=4), lambda: kw(1, d=4, e=5),
                 lambda: kw(1, a=2, d=4), lambda: kw(1), lambda: posonly(a=1, b=2, c=3),
                 lambda: star()):
        try:
            call()
            out.append("no error")
        except TypeError as e:
            out.append(str(e))
    return out


def run():
    print("late", late())
    print("counter", counter())
    print("loop closures", loop_closures())
    print("default capture", default_capture())
    print("rec outer", rec_outer(7))
    print("rec value", rec_value())
    print("rec identity", rec_identity())
    print("mutual", mutual())
    print("rebound", rebound_recursion())
    print("decorated", decorated_recursion())
    print("recursive generator", recursive_generator())
    print("unbound free", unbound_free())
    print("delete cell", delete_cell())
    print("cell params", cell_params(1), cell_params(2, b=7))
    print("pass through", pass_through())
    print("deep nonlocal", deep_nonlocal())
    print("class body", class_body())
    print("class method late", class_method_late())
    print("class method nonlocal", class_method_nonlocal())
    print("super in function", super_in_function())
    print("init_subclass super", init_subclass_super())
    print("generator closure", generator_closure())
    print("generator nonlocal", generator_nonlocal())
    print("lambda chain", lambda_chain())
    for k, r in enumerate(binding()):
        print("binding", k, r)
