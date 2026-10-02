# weakref.finalize objects that are still alive at interpreter exit run from
# their atexit handler (finalize._exitfunc), which calls gc.isenabled() and
# gc.disable()/gc.enable() as in CPython.  The gc module named the first
# function is_enabled, so every program that exited with a live finalizer
# printed an AttributeError report.  CTest checks that the finalizer ran and
# that nothing was reported (see CMakeLists.txt).
import gc
import sys
import weakref

assert gc.isenabled()
gc.disable()
assert not gc.isenabled()
gc.enable()
assert gc.isenabled()


class Resource:
    pass


resource = Resource()
weakref.finalize(resource, sys.stderr.write, "weakref_finalize_at_exit: finalizer ran\n")
print("weakref_finalize_at_exit: registered")
