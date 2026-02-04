# tempfile.py - Minimal stub. gettempdir, gettempprefix, mkstemp/TemporaryFile stubs.

import os

def gettempdir():
    """Stub: return a safe default temp directory."""
    return os.environ.get('TEMP', os.environ.get('TMP', '/tmp'))

def gettempprefix():
    """Stub: return common prefix for temp files."""
    return 'tmp'

def mkstemp(*args, **kwargs):
    """Stub: not implemented."""
    raise NotImplementedError('tempfile.mkstemp is a stub')

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
