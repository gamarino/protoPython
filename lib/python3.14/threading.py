# threading.py - Python threading support for protoPython
import _thread
import time

_has_thread = True

def get_ident():
    return _thread.get_ident()

def getpid():
    return _thread.getpid()

class Lock:
    def __init__(self):
        self._lock = _thread.allocate_lock()
    def acquire(self, blocking=True, timeout=-1):
        return self._lock.acquire(blocking, timeout)
    def release(self):
        self._lock.release()

class RLock:
    def __init__(self):
        self._lock = _thread.allocate_rlock()
    def acquire(self, blocking=True, timeout=-1):
        return _thread.rlock_acquire(self._lock, blocking, timeout)
    def release(self):
        # In a real implementation we would release here.
        # For protoPython, we'll just call the native release.
        _thread.rlock_release(self._lock)

class Condition:
    def __init__(self, lock=None):
        if lock is None:
            lock = RLock()
        self._lock = lock
        self.acquire = lock.acquire
        self.release = lock.release

    def wait(self, timeout=None):
        self.release()
        time.sleep(0.01)
        self.acquire()

    def notify(self, n=1): pass
    def notify_all(self): pass

class Event:
    def __init__(self):
        self._cond = Condition()
        self._flag = False
    def is_set(self):
        return self._flag
    def set(self):
        self._cond.acquire()
        self._flag = True
        self._cond.release()
    def clear(self):
        self._cond.acquire()
        self._flag = False
        self._cond.release()
    def wait(self, timeout=None):
        while not self._flag:
            self._cond.wait(timeout)
            if timeout is not None: break
        return self._flag

class Semaphore:
    def __init__(self, value=1):
        self._cond = Condition()
        self._value = value
    def acquire(self, blocking=True, timeout=None):
        self._cond.acquire()
        while self._value <= 0:
            self._cond.wait(timeout)
        self._value -= 1
        self._cond.release()
        return True
    def release(self):
        self._cond.acquire()
        self._value += 1
        self._cond.release()

class Thread:
    def __init__(self, target, args=(), kwargs={}):
        self._target = target
        self._args = args
        self._kwargs = kwargs
        self._tid = None

    def start(self):
        def _run():
            self._target(*self._args, **self._kwargs)
        self._tid = _thread.start_new_thread(_run, ())

    def join(self):
        while self.is_alive():
            time.sleep(0.01)

    def is_alive(self):
        if self._tid is None: return False
        return _thread.is_alive(self._tid)

def current_thread():
    return None
