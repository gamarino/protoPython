# os.py - Uses _os for getenv, getcwd, chdir when available.
import sys

name = 'os'

path = []
pathsep = ':'
sep = '/'

_common_env_keys = (
    'PATH', 'HOME', 'USER', 'LOGNAME', 'SHELL', 'PWD', 'LANG',
    'TERM', 'DISPLAY', 'EDITOR', 'TMPDIR', 'TEMP', 'TMP',
    'SINGLE_THREAD', 'PROTO_THREAD_DEBUG', 'PYTHONPATH',
)

def _make_environ():
    d = {}
    try:
        import _os
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
