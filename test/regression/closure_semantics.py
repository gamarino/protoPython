# Closure semantics, checked against CPython (closure_semantics.expected is
# CPython 3.14's output).  Covers what the cell-based closure representation
# must preserve: late binding, nonlocal, recursion, class bodies, the
# __class__ cell, comprehensions, generators, coroutines, introspection.
import sys
import types


def show(label, value):
    print(label, value)


# --- late binding and nonlocal ---------------------------------------------
def late():
    x = 1
    def get():
        return x
    x = 2
    return get()
show("late binding", late())


def counter():
    n = 0
    def inc():
        nonlocal n
        n += 1
        return n
    def peek():
        return n
    inc(); inc()
    return inc(), peek(), n
show("nonlocal shared", counter())


def loop_closures():
    fs = []
    for i in range(3):
        fs.append(lambda: i)
    return [f() for f in fs]
show("loop late binding", loop_closures())


def loop_defaults():
    return [f() for f in [lambda i=i: i for i in range(3)]]
show("default capture", loop_defaults())


def comp_lambdas():
    return [f() for f in [lambda: x for x in range(3)]]
show("comprehension lambdas", comp_lambdas())


# --- recursion ---------------------------------------------------------------
def rec_outer(k):
    def rec(n):
        return k if n == 0 else rec(n - 1)
    return rec(4)
show("recursive inner", rec_outer(7))


def rec_value():
    def walk(n):
        return [n] + (walk(n - 1) if n else [])
    return walk(3), walk.__name__, walk.__code__.co_freevars
show("recursive value", rec_value())


def rec_through_comprehension():
    def depth(t):
        return 1 + max([depth(c) for c in t], default=0)
    return depth([[[]], []])
show("recursion via comprehension", rec_through_comprehension())


def rec_identity():
    def me():
        return me
    return me() is me, me.__closure__[0].cell_contents is me
show("recursive identity", rec_identity())


def mutual():
    def even(n):
        return True if n == 0 else odd(n - 1)
    def odd(n):
        return False if n == 0 else even(n - 1)
    return even(10), odd(7)
show("mutual recursion", mutual())


def rebound_recursion():
    def f(n):
        return n if n < 1 else f(n - 1)
    g = f
    def f(n):
        return "second"
    return g(3)
show("rebound recursion", rebound_recursion())


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
show("decorated recursion", decorated_recursion())


def recursive_generator():
    def tree(n):
        if n:
            yield n
            yield from tree(n - 1)
    return list(tree(4))
show("recursive generator", recursive_generator())


def defined_in_loop():
    out = []
    for i in range(2):
        def f(n):
            return (i, n) if n == 0 else f(n - 1)
        out.append(f)
    return [g(1) for g in out]
show("recursive def in loop", defined_in_loop())


# --- deletion and unbound cells ---------------------------------------------
def unbound_free():
    def inner():
        return v
    try:
        inner()
    except NameError as e:
        msg = str(e)
    v = 1
    return msg, inner()
show("unbound free", unbound_free())


def unbound_cell():
    def inner():
        return w
    try:
        print(w)
    except UnboundLocalError as e:
        msg = str(e)
    w = 2
    return msg, inner()
show("unbound cell", unbound_cell())


def delete_cell():
    z = 1
    def inner():
        return z
    del z
    try:
        inner()
    except NameError as e:
        return str(e)
show("deleted cell", delete_cell())


def nonlocal_delete():
    q = 5
    def drop():
        nonlocal q
        del q
    drop()
    try:
        return q
    except UnboundLocalError as e:
        return type(e).__name__
show("nonlocal del", nonlocal_delete())


# --- parameters as cells, *args, **kwargs -------------------------------------
def params(a, *args, b=3, **kw):
    def inner():
        return a, args, b, kw
    a = a * 10
    return inner()
show("cell params", params(1, 2, b=4, c=5))


# --- three levels, pass-through ---------------------------------------------
def three():
    x = "x0"
    def middle():
        def inner():
            return x
        return inner
    f = middle()
    x = "x1"
    return f(), middle.__code__.co_freevars, f.__code__.co_freevars
show("pass-through", three())


def three_nonlocal():
    x = 0
    def middle():
        def inner():
            nonlocal x
            x += 5
        inner()
        return x
    return middle(), x
show("deep nonlocal", three_nonlocal())


# --- class bodies -------------------------------------------------------------
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
show("class body", class_body())


def class_body_late():
    v = 1
    class C:
        def get(self):
            return v
    v = 2
    return C().get()
show("class method late binding", class_body_late())


def class_nonlocal():
    total = 0
    class Acc:
        def add(self, k):
            nonlocal total
            total += k
    a = Acc()
    a.add(2); a.add(3)
    return total
show("class method nonlocal", class_nonlocal())


def super_in_function():
    class Base:
        def who(self):
            return "base"
    class Derived(Base):
        def who(self):
            return "derived+" + super().who()
    Alias = Derived
    return Alias().who(), Derived.who.__code__.co_freevars
show("super in function", super_in_function())


def class_cell_rebound():
    class A:
        def f(self):
            return __class__
    B = A
    del A
    return B().f() is B
show("class cell rebound", class_cell_rebound())


class Module:
    def m(self):
        def inner():
            return __class__
        return inner() is Module, super().__class__.__name__
show("module class cell", Module().m())


# --- comprehensions and generators -------------------------------------------
def comp_capture(k):
    return [x * k for x in range(3)], {x: k for x in range(2)}, sum(x + k for x in range(3))
show("comprehension capture", comp_capture(10))


def gen_closure():
    base = 100
    def g():
        for i in range(3):
            yield base + i
    it = g()
    first = next(it)
    nonlocal_base_change = None
    return first, list(it)
show("generator closure", gen_closure())


def gen_nonlocal():
    state = []
    def g():
        nonlocal state
        state = state + [1]
        yield len(state)
        state = state + [2]
        yield len(state)
    return list(g()), state
show("generator nonlocal", gen_nonlocal())


def coroutine_closure():
    seen = []
    async def co(v):
        seen.append(v)
        return v * 2
    c = co(21)
    try:
        c.send(None)
    except StopIteration as e:
        return e.value, seen
show("coroutine closure", coroutine_closure())


# --- introspection ------------------------------------------------------------
def intro():
    a = 1
    b = 2
    def inner():
        return a + b
    return (inner.__code__.co_freevars, intro.__code__.co_cellvars,
            [type(c).__name__ for c in inner.__closure__],
            [c.cell_contents for c in inner.__closure__])
show("introspection", intro())


def cell_write():
    v = "old"
    def get():
        return v
    get.__closure__[0].cell_contents = "new"
    return get(), v
show("cell write", cell_write())


def frame_locals():
    a = 1
    def inner():
        b = 2
        return a + b, sorted(sys._getframe().f_locals.items())
    return inner()
show("f_locals with free vars", frame_locals())


def locals_with_cells():
    c = 3
    def inner():
        return c
    return sorted((k, v) for k, v in locals().items() if k != 'inner')
show("locals with cells", locals_with_cells())


def eval_with_cells():
    c = 4
    def inner():
        return c
    return eval("c + 1"), inner()
show("eval with cells", eval_with_cells())


def cell_type():
    def f():
        return f
    return type(f.__closure__[0]) is types.CellType
show("cell type", cell_type())



# --- more recursion shapes ------------------------------------------------
def recursive_deleted():
    def r():
        return r
    keep = r
    del r
    try:
        keep()
    except NameError as e:
        return str(e)
show("recursive name deleted", recursive_deleted())


def recursive_with_sibling():
    def rec(n):
        return 0 if n == 0 else 1 + rec(n - 1)
    def user():
        return rec(3)
    return user(), rec.__code__.co_freevars, user.__code__.co_freevars
show("recursive with sibling", recursive_with_sibling())


def recursive_in_branches(flag):
    if flag:
        def pick(n):
            return "a" if n == 0 else pick(n - 1)
    else:
        try:
            def pick(n):
                return "b" if n == 0 else pick(n - 1)
        finally:
            pass
    return pick(2)
show("recursive in branches", (recursive_in_branches(True), recursive_in_branches(False)))


def recursive_outlives_outer():
    def count(n):
        return n if n <= 0 else count(n - 1) + 1
    return count
c1 = recursive_outlives_outer()
c2 = recursive_outlives_outer()
show("recursive outlives outer", (c1(5), c2(3), c1 is not c2))


async def _async_rec_driver():
    async def arec(n):
        return n if n == 0 else await arec(n - 1) + 1
    return await arec(4)
def run_coroutine(co):
    try:
        co.send(None)
    except StopIteration as e:
        return e.value
show("async recursive", run_coroutine(_async_rec_driver()))


# --- class bodies, namespaces and __classcell__ ------------------------------
def class_namespace_shadow():
    v = "outer"
    class C:
        w = v
        def get(self):
            return v
    return C.w, C().get(), "__classcell__" in vars(C), "v" in vars(C)
show("class namespace", class_namespace_shadow())


def class_cell_hidden():
    class D:
        def m(self):
            return super().__repr__ is not None
    return D().m(), "__classcell__" in vars(D), "__classcell__" in dir(D)
show("classcell hidden", class_cell_hidden())


def nested_class_closures():
    base = 10
    class Outer:
        class Inner:
            def val(self):
                return base + 1
        def val(self):
            return base + 2
    return Outer.Inner().val(), Outer().val()
show("nested class closures", nested_class_closures())


def class_in_loop():
    made = []
    for i in range(3):
        class K:
            def who(self):
                return (i, type(self).__name__, __class__ is K)
        made.append(K)
    return [k().who() for k in made]
show("class in loop", class_in_loop())


# --- comprehensions and generator expressions --------------------------------
def nested_comprehensions(k):
    return [[x * y * k for y in range(2)] for x in range(3)]
show("nested comprehensions", nested_comprehensions(2))


def genexp_late(k):
    g = (x + k for x in range(3))
    k = 100
    return list(g)
show("genexp late binding", genexp_late(1))


def comprehension_cell():
    fs = [lambda: x * y for x in range(2) for y in range(2)]
    return [f() for f in fs]
show("comprehension cell", comprehension_cell())


# --- lambdas, defaults, exec ---------------------------------------------------
def lambda_chain():
    a = 1
    f = lambda b: lambda c: a + b + c
    a = 5
    return f(10)(100)
show("lambda chain", lambda_chain())


def exec_sees_cells():
    v = 41
    def use():
        return v
    ns = {}
    exec("r = v + 1", globals(), ns) if False else None
    out = {}
    exec("w = 1", {}, out)
    return eval("v + 1"), out["w"], use()
show("exec and eval", exec_sees_cells())


def nonlocal_through_class():
    hits = 0
    class Counter:
        def hit(self):
            nonlocal hits
            hits += 1
            return hits
    c = Counter()
    c.hit(); c.hit()
    return hits, c.hit()
show("nonlocal through class", nonlocal_through_class())

print("closure semantics done")
