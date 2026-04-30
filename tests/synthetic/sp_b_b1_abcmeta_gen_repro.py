# tests/synthetic/sp_b_b1_abcmeta_gen_repro.py
"""SP-B / B1 reproducer — `'ABCMeta' object has no attribute 'gen'`.

Pre-fix, instances of a class with non-default metaclass had their
type() resolve to the metaclass instead of the class.  `getType` walked
the parent chain looking for `__class__` and accepted the parent's own
`__class__` (the metaclass).  The fix (commit fe896c33) restricts the
honor-`__class__` rule to OWN attributes only.

Two checks:
  1) Direct: a custom metaclass class — type(C()) must be C, not Meta.
  2) Integration: contextlib.contextmanager (which uses ABCMeta).
"""

# 1. Direct unit-style reproducer — non-default metaclass instance.
class _B1_Meta(type):
    pass

class _B1_C(metaclass=_B1_Meta):
    def __init__(self, x):
        self.x = x

_inst = _B1_C(42)
assert type(_inst) is _B1_C, f"type({_inst!r}) is {type(_inst).__name__}, expected _B1_C"
assert _inst.x == 42, f"_inst.x = {_inst.x!r}, expected 42"

# 2. Integration test — contextlib path that surfaced the bug originally.
from contextlib import contextmanager

@contextmanager
def _b1_my_ctx():
    yield 99

with _b1_my_ctx() as v:
    assert v == 99

print("SP_B_B1_OK")
