#!/usr/bin/env python3
"""
Isolated test for list_iterator MRO and __dict__ access.
Use to reproduce hang at Iterator.register(list_iterator) in _collections_abc.
Run with: protopy tests/test_list_iterator_mro_dict.py (or python3)
"""
from __future__ import print_function
import sys

def step(name):
    print("[step]", name, flush=True)

step("start")

# Same as _collections_abc: list_iterator = type(iter([]))
step("create iter([])")
it = iter([])
step("type(iter([]))")
list_iterator = type(it)
step("got list_iterator")

# 1. MRO access (can hang if MRO resolution is slow or infinite)
step("get __mro__")
try:
    mro = list_iterator.__mro__
    step("__mro__ = %s" % (type(mro).__name__,))
    for i, B in enumerate(mro):
        step("  mro[%d] = %s" % (i, type(B).__name__))
except Exception as e:
    print("MRO access failed:", e, flush=True)
    sys.exit(1)

# 2. Dict access on each MRO base (mirrors _check_methods which Iterator.__subclasshook__ uses)
step("access __dict__ on each MRO base")
for i, B in enumerate(mro):
    step("  base %d: %s" % (i, getattr(B, "__name__", type(B).__name__)))
    try:
        d = B.__dict__
        step("    __dict__ type: %s" % (type(d).__name__,))
        for method in ("__iter__", "__next__"):
            step("    check %r in __dict__" % (method,))
            present = method in d
            step("    %r in __dict__ = %s" % (method, present))
            if present and d[method] is None:
                step("    (value is None)")
    except Exception as e:
        print("  dict access failed for base %d: %s" % (i, e), flush=True)

# 3. Optional: simulate _check_methods(list_iterator, '__iter__', '__next__')
step("simulate _check_methods")
def _check_methods(C, *methods):
    mro = C.__mro__
    for method in methods:
        for B in mro:
            step("    _check_methods: B=%s, method=%s" % (getattr(B, "__name__", ""), method))
            if method in B.__dict__:
                if B.__dict__[method] is None:
                    step("    -> NotImplemented (None)")
                    return NotImplemented
                break
        else:
            step("    -> NotImplemented (missing %s)" % (method,))
            return NotImplemented
    step("    -> True")
    return True

result = _check_methods(list_iterator, "__iter__", "__next__")
step("_check_methods result: %s" % (result,))

# 4. Optional: actual Iterator.register if abc is available (may hang)
step("try Iterator.register")
try:
    import _collections_abc
    step("imported _collections_abc")
    Iterator = _collections_abc.Iterator
    step("calling Iterator.register(list_iterator)")
    Iterator.register(list_iterator)
    step("Iterator.register(list_iterator) done")
except Exception as e:
    print("Iterator.register failed:", e, flush=True)

step("end")
