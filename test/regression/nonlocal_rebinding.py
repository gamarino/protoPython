# Regression test: `nonlocal` rebinding reaches the enclosing function.
#
# Three stacked defects made every `nonlocal x; x = ...` a private write:
# the compiler did not count a name the nested scope declares nonlocal (and
# assigns) as captured, so the enclosing function kept it in a fast slot;
# STORE_DEREF tested getAttribute, which walks the chain and never returns
# nullptr, so it bound the name on the running function's own frame; and
# BUILD_FUNCTION copied the enclosing frame's live bindings into the closure
# frame, so reads and writes hit that copy.  asyncio.gather() counts finished
# children with `nonlocal nfinished` and never completed.

def counter():
    n = 0
    def bump():
        nonlocal n
        n += 1
    bump(); bump()
    return n
assert counter() == 2, counter()

def assigned_after_def():
    def bump():
        nonlocal n
        n += 1
    n = 10
    bump()
    return n
assert assigned_after_def() == 11

def parameter(a):
    def bump():
        nonlocal a
        a += 1
    bump()
    return a
assert parameter(10) == 11

def varargs_kwonly(*items, flag=False):
    seen = 0
    def bump():
        nonlocal seen
        seen += 1
    for _ in items:
        bump()
    return seen
assert varargs_kwonly(1, 2, 3) == 3

def siblings():
    n = 0
    def inc():
        nonlocal n
        n += 1
    def get():
        return n
    inc(); inc()
    return get(), n
assert siblings() == (2, 2)

def rebound_after_def():
    x = 1
    def get():
        return x
    x = 2
    return get()
assert rebound_after_def() == 2

def two_levels():
    total = 0
    def mid():
        def leaf():
            nonlocal total
            total += 5
        leaf()
        return total
    return mid(), total
assert two_levels() == (5, 5)

class Box:
    def triple(self, value):
        def inner():
            nonlocal value
            value = value * 3
        inner()
        return value
assert Box().triple(4) == 12

# A method parameter named like a class attribute, captured by an inner
# function, is the parameter (the case the closure-frame copy was added for).
class Stack:
    def make(self, fn):
        return fn()
    def callback(self, callback, *args):
        def wrapper():
            return callback(*args)
        return self.make(wrapper)
assert Stack().callback(lambda: "param") == "param"

# gather()'s shape: the callback is created before the names it rebinds.
def gather_shape(*items):
    def done(item):
        nonlocal finished
        finished += 1
        if outer is None:
            return
        if finished == total:
            outer.append(sorted(children))
    children = []
    total = 0
    finished = 0
    outer = None
    for it in items:
        total += 1
        children.append(it)
    outer = []
    for it in items:
        done(it)
    return outer, finished
assert gather_shape(3, 1, 2) == ([[1, 2, 3]], 3)

print("nonlocal rebinding OK")
