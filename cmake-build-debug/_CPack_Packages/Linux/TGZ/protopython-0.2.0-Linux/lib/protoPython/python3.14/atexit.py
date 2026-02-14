# atexit: register callbacks to run at interpreter shutdown.

_exithandlers = []

def register(func, *args, **kwargs):
    """Register func(*args, **kwargs) to be called at exit."""
    _exithandlers.append((func, args, kwargs))

def unregister(func):
    """Unregister all entries for func."""
    global _exithandlers
    _exithandlers = [(f, a, k) for (f, a, k) in _exithandlers if f != func]

def _run_exitfuncs():
    """Run all registered exit handlers. Called by runtime at shutdown."""
    global _exithandlers
    handlers = _exithandlers
    _exithandlers = []
    for func, args, kwargs in handlers:
        try:
            func(*args, **kwargs)
        except Exception:
            pass
