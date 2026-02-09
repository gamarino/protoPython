print("os.py START", flush=True)
import sys

name = 'os'
pathsep = ':'
sep = '/'

print("os.py about to import os.path", flush=True)
try:
    import os.path as path
except ImportError:
    path = None

def _make_environ():
    d = {}
    try:
        import _os
        if hasattr(_os, 'environ_keys'):
            for k in _os.environ_keys():
                v = _os.getenv(k)
                if v is not None:
                    d[k] = v
    except ImportError:
        pass
    return d

class _Environ:
    def __init__(self, initial):
        self._data = initial
    def __getitem__(self, key):
        return self._data[key]
    def __setitem__(self, key, value):
        self._data[key] = value
        try:
            import _os
            print("DEBUG: calling _os.putenv", flush=True)
            _os.putenv(key, value)
        except Exception as e:
            print("DEBUG: os.environ.__setitem__ failed: %s" % e, flush=True)
            pass
    def __delitem__(self, key):
        print("DEBUG: calling os.environ.__delitem__ for", key, flush=True)
        del self._data[key]
        try:
            import _os
            _os.unsetenv(key)
        except Exception as e:
            print("DEBUG: os.environ.__delitem__ failed: %s" % e, flush=True)
            pass
    def __contains__(self, key):
        return key in self._data
    def __repr__(self):
        return repr(self._data)
    def __len__(self):
        return len(self._data)
    def keys(self): return self._data.keys()
    def values(self): return self._data.values()
    def items(self): return self._data.items()
    def get(self, key, default=None): return self._data.get(key, default)

environ = _Environ(_make_environ())

def getenv(key, default=None):
    try:
        import _os
        v = _os.getenv(key)
        return v if v is not None else default
    except: return default

def putenv(key, value):
    try:
        import _os
        _os.putenv(key, value)
        environ._data[key] = value
    except: pass

def unsetenv(key):
    try:
        import _os
        _os.unsetenv(key)
        if key in environ._data:
            del environ._data[key]
    except: pass

def getcwd():
    try:
        import _os
        return _os.getcwd()
    except: return "."

def chdir(path):
    try:
        import _os
        _os.chdir(path)
    except: pass

def listdir(path='.'):
    try:
        import _os
        return _os.listdir(path)
    except: return []

def remove(path):
    try:
        import _os
        _os.remove(path)
    except: pass

def rmdir(path):
    try:
        import _os
        _os.rmdir(path)
    except: pass

def waitpid(pid, options):
    try:
        import _os
        return _os.waitpid(pid, options)
    except: return (0, 0)

def kill(pid, sig):
    try:
        import _os
        _os.kill(pid, sig)
    except: pass

def pipe():
    try:
        import _os
        return _os.pipe()
    except: return (0, 0)

def getpid():
    try:
        import _thread
        return _thread.getpid()
    except: return 0
