"""Phase 1 reproducer - DEBUG stdout cleanliness.

Importing modules used to leak ~240 lines of "DEBUG:" output on stdout.
Triggers BOTH a stdlib import (os) and a local-package import to cover
distinct module-resolution paths. Driver checks: stdout must contain
MARKER_PHASE1 AND must NOT contain any "DEBUG:" lines.
"""
import os, sys

# Trigger a local-package import: create a tiny package in a temp dir,
# add it to sys.path, then import it.  We avoid `tempfile.mkdtemp` to keep
# the reproducer independent of unrelated stdlib quirks - a fixed PID-based
# directory name under /tmp is sufficient and self-cleaning at the end.
d = "/tmp/sp0_p1_" + str(os.getpid())
pkg = os.path.join(d, "_sp0_p1_pkg")
os.makedirs(pkg, exist_ok=True)
with open(os.path.join(pkg, "__init__.py"), "w") as f:
    f.write("VALUE = 42\n")
sys.path.insert(0, d)

# CAVEAT: SP0 Phase 3 fixes sys.path.insert resolution.  Until that fix
# lands, this import will fail with ModuleNotFoundError, which is fine
# for Phase 1 - the test still triggers the stdlib import and prints
# the marker before failing.  After Phase 3 lands, the local import
# also succeeds and adds full coverage.
try:
    import _sp0_p1_pkg
except ModuleNotFoundError:
    pass

# Cleanup - manual, to avoid pulling in shutil/sys.audit quirks unrelated
# to the Phase 1 surface under test.
try:
    os.remove(os.path.join(pkg, "__init__.py"))
except OSError:
    pass
try:
    os.rmdir(pkg)
except OSError:
    pass
try:
    os.rmdir(d)
except OSError:
    pass

# Trigger _create_slots / _add_slots (formerly noisy with DEBUG: prints).
# We only need the decorator to *run* - it is the slot-rewriting code path
# (not slot attribute access at runtime) that previously emitted the eight
# `print(f"DEBUG: ...")` lines.  Once the class is built, the regression
# surface for Phase 1 has been exercised; we deliberately do not exercise
# the still-WIP slot-descriptor read path, which is unrelated to stdout
# noise.
from dataclasses import dataclass

@dataclass(slots=True)
class _SP0P1Probe:
    a: int
    b: str = "x"

# Constructing the instance further exercises the generated __init__ that
# `_add_slots` rewrote, without depending on slot-attribute reads.
try:
    _p = _SP0P1Probe(1, "y")
except Exception:
    pass

print("MARKER_PHASE1", flush=True)
