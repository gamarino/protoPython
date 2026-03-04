# Phase 1.2.2: Wrapper vs content — getImportModule wrapper's "val" (or module content)
# must point to current module root after execution that mutates the module.

import sys

try:
    import conformity_dummy
    M = sys.modules["conformity_dummy"]
except ImportError:
    M = sys.modules["sys"]
M.wrapper_test_attr = "current_root"

M_again = sys.modules.get(M.__name__, M)
assert getattr(M_again, "wrapper_test_attr", None) == "current_root"

print("OK wrapper content")
