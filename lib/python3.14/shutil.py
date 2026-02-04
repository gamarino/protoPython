"""
Minimal shutil stub for protoPython.
copyfile, rmtree, and other functions are stubs; full implementation requires native file APIs.
"""

def copyfile(src, dst, follow_symlinks=True):
    """Stub: no-op. Full impl requires native file copy."""
    pass

def rmtree(path, ignore_errors=False, onerror=None):
    """Stub: no-op. Full impl requires native directory removal."""
    pass

def copy(src, dst, follow_symlinks=True):
    """Stub: no-op."""
    pass

def move(src, dst, copy_function=copy):
    """Stub: no-op."""
    pass
