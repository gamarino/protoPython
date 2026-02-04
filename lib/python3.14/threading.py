# threading.py - Uses _thread (ProtoSpace threads) when available.

try:
    import _thread
    _has_thread = True
except ImportError:
    _has_thread = False


def get_ident():
    """Return the current thread's id (OS thread id when _thread provides it)."""
    if _has_thread:
        return _thread.get_ident()
    return 0


def getpid():
    """Return the current process id (when _thread provides it)."""
    if _has_thread:
        return _thread.getpid()
    return 0


def current_thread():
    """Return a minimal object representing the current thread (ident, name)."""
    class _CurrentThread:
        ident = property(lambda self: get_ident())
        name = "MainThread"
    return _CurrentThread()


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
    """Lock using _thread.allocate_lock when available."""
    def __init__(self):
        if _has_thread:
            self._lock = _thread.allocate_lock()
        else:
            self._lock = None

    def acquire(self, blocking=True, timeout=-1):
        if self._lock is None:
            return True
        return _thread._lock_acquire(self._lock, blocking)

    def release(self):
        if self._lock is not None:
            _thread._lock_release(self._lock)
