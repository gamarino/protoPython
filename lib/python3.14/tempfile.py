# tempfile.py - gettempdir/gettempprefix; mkstemp with unique path; TemporaryFile returns file-like.
# gettempdir uses os.environ (TEMP, TMP) with default '/tmp'. gettempprefix returns 'tmp'.

import os

def gettempdir():
    """Return temp directory: os.environ.get('TEMP') or os.environ.get('TMP') or '/tmp'."""
    return os.environ.get('TEMP', os.environ.get('TMP', '/tmp'))

def gettempprefix():
    """Return common prefix for temp files."""
    return 'tmp'

def mkstemp(suffix='', prefix='tmp', dir=None, text=False):
    """Create a unique temp file. Returns (fd, path); fd is 0 (stub)."""
    import random
    import time
    base = dir if dir else gettempdir()
    sep = '/' if not base.endswith('/') else ''
    unique = prefix + str(int(time.time() * 1000)) + '_' + str(random.getrandbits(24)) + suffix
    path = base + sep + unique
    return (0, path)

class TemporaryFile:
    """File-like that uses a temp path. On close, removes the file if delete=True."""
    def __init__(self, mode='w+b', suffix='', prefix='tmp', dir=None, delete=True):
        self._path = mkstemp(suffix=suffix, prefix=prefix, dir=dir)[1]
        self._file = open(self._path, mode)
        self._delete = delete

    def close(self):
        if self._file:
            self._file.close()
            self._file = None
            if self._delete:
                try:
                    os.remove(self._path)
                except OSError:
                    pass

    def write(self, data):
        return self._file.write(data) if self._file else 0

    def read(self, n=-1):
        return self._file.read(n) if self._file else b''

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
