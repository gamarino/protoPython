# Phase 1.2.3: Re-import — no dangling references to old roots after re-import/reload.

import sys

try:
    import conformity_dummy as M
    name = "conformity_dummy"
except ImportError:
    M = sys
    name = "sys"

M.reimport_marker = 999
M2 = sys.modules.get(name)
assert M2 is not None
assert getattr(M2, "reimport_marker", None) == 999
assert M is M2

print("OK reimport")
