# Python try / except / finally / with as C++ try / catch (each protected range
# a nested dispatch, its handler inside the catch), against CPython: yields at
# every level, gen.throw and close, the handled exception after a resume,
# chaining, suppression, finally overriding, break/continue through finally.
import sys
out = []
def p(*a): out.append(" ".join(str(x) for x in a))

def g1():
    try:
        yield 1
        yield 2
    except ValueError as e:
        p("g1 caught", e)
        yield 3
    finally:
        p("g1 finally")
it = g1(); p(next(it)); p(it.throw(ValueError("boom")))
try:
    next(it)
except StopIteration:
    p("g1 done")

def g2():
    try:
        raise KeyError("k")
    except KeyError:
        yield "in except"
        p("exc_info after resume", sys.exc_info()[0].__name__)
        try:
            raise
        except KeyError as e:
            p("bare raise after resume", repr(e))
    p("exc_info after handler", sys.exc_info()[0])
for v in g2(): p(v)

def g3():
    try:
        yield "a"
    finally:
        p("g3 close runs finally")
it = g3(); next(it); it.close(); p("closed")

class CM:
    def __init__(self, suppress): self.suppress = suppress
    def __enter__(self): p("enter"); return self
    def __exit__(self, t, v, tb): p("exit", t.__name__ if t else None); return self.suppress
def g4():
    with CM(True):
        yield "in with"
        raise RuntimeError("suppressed")
    yield "after with"
p(list(g4()))

try:
    try:
        raise ValueError("first")
    except ValueError:
        raise TypeError("second")
except TypeError as e:
    p("context", type(e.__context__).__name__, e.__cause__)
try:
    try:
        raise ValueError("a")
    except ValueError as v:
        raise TypeError("b") from v
except TypeError as e:
    p("cause", type(e.__cause__).__name__, e.__suppress_context__)

def f_finally_return():
    try:
        return "body"
    finally:
        return "finally"
p(f_finally_return())

def f_loop():
    r = []
    for i in range(5):
        try:
            if i == 1: continue
            if i == 3: break
            r.append(i)
        finally:
            r.append("f%d" % i)
    return r
p(f_loop())

def deep(n):
    if n == 0: raise LookupError("deep")
    try:
        deep(n - 1)
    finally:
        out.append("unwind %d" % n)
try:
    deep(3)
except LookupError as e:
    p("deep caught", e)

def nested():
    try:
        try:
            try:
                1 / 0
            except ZeroDivisionError:
                p("inner")
                raise
        except ArithmeticError as e:
            p("middle", type(e).__name__)
            raise ValueError("outer") from None
    except ValueError as e:
        p("outer", e, e.__suppress_context__)
nested()

def handler_raises():
    try:
        raise OSError("x")
    except OSError:
        try:
            raise KeyError("y")
        except KeyError:
            p("handled y inside x")
        p("still in x", sys.exc_info()[0].__name__)
handler_raises()
p("after all", sys.exc_info())

def gen_yield_from():
    def sub():
        try:
            yield 1
        except ValueError:
            yield "sub caught"
    r = yield from sub()
    yield "done"
it = gen_yield_from(); p(next(it)); p(it.throw(ValueError())); p(next(it))

def ret_in_except():
    try:
        raise ValueError()
    except ValueError:
        return "returned from except"
    finally:
        p("finally after except return")
p(ret_in_except())
print("\n".join(out))
