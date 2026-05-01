"""SP-C / Phase 4 — SP-B/B3 closes; @dataclass __init__ assigns fields.

This is the canonical B3 reproducer.  It verifies that with the
MappingProxy semantics fix (own-only cls.__dict__), dataclasses'
_set_new_attribute correctly assigns the synthesized __init__,
and instance fields are set as expected.

Note on scope:  SP-B/B3's symptom was `'Point' object has no
attribute 'x'` — i.e. the synthesized __init__ was not being
assigned to the class at all (because `_set_new_attribute` saw a
truthy result for `'__init__' in cls.__dict__` from inherited
__init__ leaking through the MappingProxy, and bailed out without
attaching its own __init__).  SP-C's MappingProxy semantics fix
makes `'__init__' in cls.__dict__` False on a fresh dataclass, so
`_set_new_attribute` proceeds and the synthesized __init__ binds.
This reproducer therefore uses an all-positional Point(1, 2) — the
canonical B3 minimal reproducer.

A residual sub-bug — synthesized-__init__ default values not
applied when the corresponding positional argument is omitted —
remains open and is tracked separately; it is not part of B3.
"""
from dataclasses import dataclass

@dataclass
class Point:
    x: int
    y: int

p = Point(1, 2)
assert p.x == 1, f"p.x = {p.x!r}, expected 1"
assert p.y == 2, f"p.y = {p.y!r}, expected 2"

# A second instance must also have its synthesized __init__ run
# correctly (the bug pre-SP-C surfaced on every construction, so any
# call exercises it).
p2 = Point(10, 20)
assert p2.x == 10, f"p2.x = {p2.x!r}, expected 10"
assert p2.y == 20, f"p2.y = {p2.y!r}, expected 20"

print("SP_C_PHASE4_OK")
