# tempfile.py - Stub with gettempdir/gettempprefix from os; mkstemp/TemporaryFile stubs.
# gettempdir uses os.environ (TEMP, TMP) with default '/tmp'. gettempprefix returns 'tmp'.

import os

def gettempdir():
    """Return temp directory: os.environ.get('TEMP') or os.environ.get('TMP') or '/tmp'."""
    return os.environ.get('TEMP', os.environ.get('TMP', '/tmp'))

def gettempprefix():
    """Stub: return common prefix for temp files."""
    return 'tmp'

def mkstemp(*args, suffix='', prefix='tmp', dir=None, **kwargs):
    """Stub: returns (0, path) with a placeholder path."""
    import os
    base = dir if dir else os.gettempdir()
    return (0, base + '/' + prefix + 'XXXXXX' + suffix)

class TemporaryFile:
    """Stub: minimal file-like placeholder."""
    def __init__(self, *args, **kwargs):
        pass
    def close(self):
        pass
    def write(self, data):
        pass
    def read(self, n=-1):
        return b''
