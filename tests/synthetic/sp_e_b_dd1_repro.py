"""SP-E / B-DD1 reproducer — defense-in-depth for OP_LOAD_NAME closure chain.

Defense-in-depth fence: ProtoObject::getAttribute returns nullptr for
not-found and the actual stored value (which may legitimately be
PROTO_NONE for `x = None` at module or class scope) for found.
ProtoSparseList::getAt (used in the fast path) uses PROTO_NONE as its
absent-sentinel.  The two APIs disagree, so any future "filter PROTO_NONE
to mirror the fast-path" attempt would break the class-body case below.

This reproducer fences the present-vs-absent semantics so that any
regression in the closure-chain branch (e.g., naive PROTO_NONE filter)
would be caught immediately.
"""

# Module scope: name = None must remain reachable
module_value = None
assert module_value is None, "module-scope x = None should be reachable"

# Class body: y = None must be reachable from print(y)-style lookups
# inside the class body itself.
class WithNoneAttr:
    y = None
    # Reading y from within the class body exercises the LOAD_NAME
    # closure-chain branch with PROTO_NONE as the legitimate value.
    z = (y, "marker")

assert WithNoneAttr.y is None
assert WithNoneAttr.z == (None, "marker")

# Closure with x = None — uses LOAD_DEREF (different opcode), but
# verify it still works as a regression check on the surrounding flow.
def outer_none():
    x = None
    def inner():
        return x
    return inner()

assert outer_none() is None, "x = None in closure should work"

# Closure with real value — sanity.
def outer_value():
    y = 42
    def inner():
        return y
    return inner()

assert outer_value() == 42

# NameError on absent name — the not-found path still works.
try:
    _ = nonexistent_global_name_for_sp_e_b_dd1  # noqa: F821
    raise AssertionError("expected NameError on absent global")
except NameError:
    pass

print("SP_E_B_DD1_OK")
