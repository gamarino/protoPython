# warnings.py - Minimal warnings. warn() prints message to sys.stderr when available.

def warn(message, category=None, stacklevel=1):
    """Emit a warning: print message to sys.stderr if available; otherwise no-op."""
    try:
        import sys
        stderr = getattr(sys, 'stderr', None)
        if stderr is not None and hasattr(stderr, 'write'):
            msg = str(message)
            if category is not None:
                name = getattr(category, '__name__', str(category))
                msg = '%s: %s' % (name, msg)
            stderr.write(msg + '\n')
    except Exception:
        pass


class UserWarning(Exception):
    """Base class for user warnings."""
    pass
