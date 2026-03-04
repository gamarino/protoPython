# Phase 1.2.1: Module identity — after import M and M.x = value,
# sys.modules["M"] (or second import) must see M.x == value.
# We use a tiny local module (conformity_dummy) if available; else skip module-identity.

import sys

# Prefer testing with a dedicated module so we don't mutate stdlib.
try:
    import conformity_dummy as M
    modname = "conformity_dummy"
except ImportError:
    # Fallback: use sys module (set attr and re-fetch from sys.modules)
    M = sys
    modname = "sys"

M.conformity_marker = 12345

M2 = sys.modules.get(modname)
assert M2 is not None
got = getattr(M2, "conformity_marker", None)
assert got == 12345, "module identity: expected 12345 got %r" % got

print("OK module identity")
