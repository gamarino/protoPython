# os.py - Uses _os for getenv, getcwd, chdir, listdir, remove, rmdir when available.
import sys

name = 'os'

pathsep = ':'
sep = '/'

# Import os.path so that os.path.join, os.path.isdir, etc. are available
try:
    import os.path
    path = os.path
except ImportError:
    path = []

_common_env_keys = (
    'PATH', 'HOME', 'USER', 'LOGNAME', 'SHELL', 'PWD', 'LANG',
    'TERM', 'DISPLAY', 'EDITOR', 'TMPDIR', 'TEMP', 'TMP',
    'SINGLE_THREAD', 'PROTO_THREAD_DEBUG', 'PYTHONPATH',
    'USERNAME', 'HOSTNAME', 'OLDPWD', 'LC_ALL', 'LC_CTYPE',
)

def _make_environ():
    d = {}
    try:
        import _os
        if hasattr(_os, 'environ_keys'):
            for k in _os.environ_keys():
                v = _os.getenv(k)
                if v is not None:
                    d[k] = v
        else:
            for k in _common_env_keys:
                v = _os.getenv(k)
                if v is not None:
                    d[k] = v
    except ImportError:
        pass
    return d

environ = _make_environ()

def getpid():
    try:
        import _thread
        return _thread.getpid()
    except ImportError:
        return 0

def getcwd():
    try:
        import _os
        cwd = _os.getcwd()
        if cwd is not None:
            return cwd
    except ImportError:
        pass
    return "."

def chdir(path):
    try:
        import _os
        _os.chdir(path)
    except ImportError:
        pass

def listdir(path='.'):
    try:
        import _os
        if hasattr(_os, 'listdir'):
            return _os.listdir(path)
    except ImportError:
        pass
    return []

def remove(path):
    try:
        import _os
        if hasattr(_os, 'remove'):
            _os.remove(path)
    except ImportError:
        pass

def rmdir(path):
    try:
        import _os
        if hasattr(_os, 'rmdir'):
            _os.rmdir(path)
    except ImportError:
        pass
