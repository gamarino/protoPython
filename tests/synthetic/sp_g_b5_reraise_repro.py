"""SP-G / B5-reraise reproducer — bare reraise inside `with` across contexts.

Pre-fix: `import base64; import test.support` failed with

    RuntimeError: reraise outside of except block

even though both modules import cleanly in isolation, and even though
all of `test.support`'s explicit dependencies (annotationlib,
contextlib, functools, inspect, logging, ...) imported successfully on
their own.  The failure surfaced inside `importlib._bootstrap.
_find_and_load`'s `with _ModuleLockManager(name):` block: when an
internal import operation propagated an exception through the with
block's `__exit__`, the synthetic re-raise emitted at the end of the
exception path (OP_RAISE_VARARGS arg=0) found `getActiveException()`
returning `nullptr` even though `pushActiveException` had just
written the exception to the per-thread `_active_excs` list.

Root cause: the active-exception stack was stored exclusively on the
py_thread object's `_active_excs` ProtoList attribute.  Reading that
attribute back goes through protoCore's mutable-shard attribute cache,
which can serve stale (null) results when the read happens in a
different ProtoContext than the write — exactly the same desync that
caused SP0-P2.5 (silent-halt) and was fixed there with the
`s_pendingExc` TLS mirror.  `_active_excs` had no such mirror.

Fix: add `s_activeExcs` — a thread_local `std::vector<const
ProtoObject*>` mirror, parallel to `s_pendingExc`.  Reads now use the
mirror exclusively; the attribute storage write is preserved so the
GC-reachability story (py_thread is rooted via
`s_globalThreadRootsDict`, which keeps the listed exceptions alive
through concurrent collection) is unchanged.
"""

import base64
import test.support  # noqa: F401  — the pair was the original repro.

# Just reaching this print proves OP_RAISE_VARARGS arg=0 inside the
# with-block's exception path correctly observed the active exception.
print("sp_g_b5_reraise_repro: OK")
