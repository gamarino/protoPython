# Regression test: fast locals start unbound.
#
# The call path sizes the CO_OPTIMIZED slot array through ProtoContext, which
# fills every slot with PROTO_NONE, so a local read before its first
# assignment gave None and `del` of a never-assigned local succeeded.  Both
# must raise UnboundLocalError, as in CPython.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def unbound_message(fn, *args):
    try:
        fn(*args)
    except UnboundLocalError as exc:
        return str(exc)
    return None


def msg(name):
    return "cannot access local variable '%s' where it is not associated with a value" % name


def read_before_assign():
    y
    y = 2


def del_unbound():
    del x
    x = 1


def read_after_del():
    z = 1
    del z
    return z


def empty_loop():
    for i in range(0):
        pass
    return i


def conditional(flag):
    if flag:
        v = 1
    return v


def augmented():
    n += 1
    n = 0


check("read before assignment", unbound_message(read_before_assign), msg("y"))
check("del of an unbound local", unbound_message(del_unbound), msg("x"))
check("read after del", unbound_message(read_after_del), msg("z"))
check("loop variable of an empty loop", unbound_message(empty_loop), msg("i"))
check("branch not taken", unbound_message(conditional, False), msg("v"))
check("branch taken", conditional(True), 1)
check("augmented assignment before binding", unbound_message(augmented), msg("n"))
check("UnboundLocalError is a NameError", issubclass(UnboundLocalError, NameError), True)


def recovers():
    try:
        u
    except UnboundLocalError:
        u = 3
    return u


check("handler can bind the local", recovers(), 3)


def none_is_a_value():
    v = None
    w = v
    del v
    return w


check("None is a bound value", none_is_a_value(), None)


def params(a, b=2, *args, c, d=4, **kw):
    e = a + b
    return a, b, args, c, d, kw, e


check("parameters stay bound", params(1, c=3), (1, 2, (), 3, 4, {}, 3))
check("varargs and kwargs bound", params(1, 5, 6, c=7, k=8), (1, 5, (6,), 7, 4, {"k": 8}, 6))
check("lambda *args/**kwargs", (lambda *a, **k: (a, k))(1, x=2), ((1,), {"x": 2}))
check("lambda positional", (lambda p, q=1: p + q)(2), 3)


def closure_late_binding():
    def inner():
        return late
    late = 5
    return inner()


check("closure over a later binding", closure_late_binding(), 5)


def comprehension():
    data = [1, 2]
    return [d * 2 for d in data], {k for k in data}, list(g for g in data)


check("comprehensions", comprehension(), ([2, 4], {1, 2}, [1, 2]))


def gen():
    t = 0
    for i in range(3):
        t += i
        yield t


check("generator locals", list(gen()), [0, 1, 3])


def gen_unbound():
    yield 1
    w
    w = 2
    yield w


g = gen_unbound()
check("generator first step", next(g), 1)
try:
    next(g)
except UnboundLocalError as exc:
    gen_msg = str(exc)
else:
    gen_msg = None
check("generator read before assignment", gen_msg, msg("w"))


def dir_sees_bound_only():
    r = dir()
    q = 1
    return r


check("dir() skips unbound locals", dir_sees_bound_only(), [])


def many(n):
    total = 0
    for k in range(n):
        step = k * 2
        total += step
    return total


check("loop locals", many(4), 12)

print("unbound fast locals OK")
