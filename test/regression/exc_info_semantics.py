# The exception being handled (sys.exc_info(), sys.exception(), a bare raise,
# __context__) against CPython: nested handlers, callees, generators resumed
# inside and outside their except clause, finally, gen.throw, __exit__,
# __getattr__ and another thread.
import sys

def cur():
    e = sys.exc_info()[1]
    return None if e is None else (type(e).__name__, str(e))

print("top", cur(), sys.exception())
try:
    raise ValueError("a")
except ValueError:
    print("in handler", cur())
    def inner(): return cur()
    print("callee sees", inner())
    try:
        raise KeyError("b")
    except KeyError:
        print("nested", cur())
    print("after nested", cur())
print("after", cur())

def g():
    try:
        raise TypeError("g")
    except TypeError:
        yield cur()
        yield cur()
    yield cur()

it = g()
print("gen1", next(it))
print("caller between", cur())
try:
    raise IndexError("outer")
except IndexError:
    print("gen2 under handler", next(it))
    print("gen3 under handler", next(it))

try:
    try:
        raise ValueError("f")
    finally:
        print("finally", cur())
except ValueError:
    pass

def reraiser():
    raise

try:
    raise OSError("re")
except OSError:
    try:
        reraiser()
    except OSError as e:
        print("bare raise from callee", e)

try:
    reraiser()
except RuntimeError as e:
    print("bare raise outside", e)

try:
    try:
        raise ValueError("ctx")
    except ValueError:
        def f(): raise KeyError("new")
        f()
except KeyError as e:
    print("context", repr(e.__context__))

class G:
    def __getattr__(self, n):
        return cur()
try:
    raise ValueError("ga")
except ValueError:
    print("getattr sees", G().zz)
print("getattr outside", G().zz)

def gen_ctx():
    try:
        yield 1
    except ValueError:
        yield cur()
gg = gen_ctx(); next(gg)
print("throw", gg.throw(ValueError("thrown")))

with open(__file__) as fh:
    pass
class CM:
    def __enter__(self): return self
    def __exit__(self, *a):
        print("exit sees", cur())
        return True
with CM():
    raise ZeroDivisionError("cm")
print("after with", cur())
import threading
def worker():
    print("thread", cur())
try:
    raise ValueError("main")
except ValueError:
    t = threading.Thread(target=worker); t.start(); t.join()
print("end", sys.exc_info())
