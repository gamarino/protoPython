# threading.py - Uses _thread (ProtoSpace threads) when available.

try:
    import _thread
    _has_thread = True
except ImportError:
    _has_thread = False


class Thread:
    """Thread that runs target(*args) in a new ProtoSpace thread when _thread is available."""
    def __init__(self, target=None, args=()):
        self._target = target
        self._args = args if args is not None else ()
        self._handle = None

    def start(self):
        if not _has_thread or self._target is None:
            return
        self._handle = _thread.start_new_thread(self._target, self._args)

    def join(self, timeout=None):
        if not _has_thread or self._handle is None:
            return
        _thread.join_thread(self._handle)
        self._handle = None


class Lock:
    """Minimal Lock (stub: no blocking)."""
    def acquire(self, blocking=True, timeout=-1):
        return True
    def release(self):
        pass
