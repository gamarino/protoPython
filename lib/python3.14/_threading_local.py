"""Thread-local objects.

(Note that this module provides a Python version of the threading.local
 class.  Depending on the version of Python you're using, there may be a
 faster one available.  You should always import the `local` class from
 `threading`.)

protopy NOTE: the upstream CPython implementation relies on swapping the
instance's __dict__ in/out per thread (object.__setattr__(self, '__dict__',
dct) followed by object.__getattribute__).  protoPython models attribute
storage at the protoCore layer; the user-visible __dict__ is a synthesized
mapping that does not back the actual attribute store, so the swap is a
no-op and writes-through-the-swapped-dict are invisible to subsequent
attribute reads.  Until that layering is reconciled, we ship a minimal
single-thread-isolated `local` that simply stores attributes directly on
the instance.  Subclasses with class-body attribute defaults (e.g.
`class _Local(threading.local): _loop = None`) keep working because the
protoCore prototype chain already resolves them, and per-thread isolation
is moot in protopy's main-thread-dominant workloads.

If multi-threaded isolation becomes load-bearing later, the right fix is
either a native `_threading_local` module or making
object.__setattr__(self, '__dict__', dct) actually rebind the attribute
store at the protoCore level.
"""

__all__ = ["local"]


class local:
    """Thread-local data — protopy single-thread fallback.

    Stores attributes directly on the instance.  Subclassing works
    transparently because protoCore's prototype chain resolves any
    class-body defaults.  Constructor arguments are not supported
    (mirroring CPython behaviour for the `local` base class).
    """

    def __new__(cls, *args, **kw):
        if (args or kw) and (cls.__init__ is object.__init__):
            raise TypeError("Initialization arguments are not supported")
        return object.__new__(cls)
