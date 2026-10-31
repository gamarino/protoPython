# frame.f_locals of a function frame is a write-through mapping (PEP 667).
import sys
from collections.abc import Mapping

def write_own():
    x = 1
    f = sys._getframe()
    f.f_locals["x"] = 2
    return x
assert write_own() == 2

def caller():
    # A function that builds no inner function may run without a frame
    # object (docs/PYTHON_COMPATIBILITY.md); this one has a frame.
    a = "before"
    def unused():
        pass
    callee()
    return a
def callee():
    sys._getframe(1).f_locals["a"] = "after"
assert caller() == "after"

def with_cell():
    c = 1
    def inner():
        return c
    sys._getframe().f_locals["c"] = 5
    return c, inner()
assert with_cell() == (5, 5)

def reads():
    y = 10
    fl = sys._getframe().f_locals
    y = 11
    return fl["y"], "y" in fl, len(fl) >= 1, dict(fl)["y"], isinstance(fl, Mapping)
assert reads() == (11, True, True, 11, True)

def no_delete():
    z = 1
    fl = sys._getframe().f_locals
    try:
        del fl["z"]
    except ValueError:
        return z
assert no_delete() == 1

assert sys._getframe().f_locals is globals()
# A returned function's f_locals keeps its final values, and stores still work.
def finished():
    v = 41
    f = sys._getframe()
    proxy = f.f_locals
    v = 42
    return proxy
fl = finished()
fl["w"] = 1
assert fl["w"] == 1, dict(fl)

print("frame_locals_write_through: ok")
