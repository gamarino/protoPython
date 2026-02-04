"""
shutil: copyfile and copy implemented; rmtree, move remain stubs.
"""

def copyfile(src, dst, follow_symlinks=True):
    """Copy file src to dst. Uses open/read/write."""
    with open(src, 'rb') as f:
        data = f.read()
    with open(dst, 'wb') as f:
        f.write(data)

def rmtree(path, ignore_errors=False, onerror=None):
    """Stub: no-op. Full impl requires native directory removal."""
    pass

def copy(src, dst, follow_symlinks=True):
    """Copy src to dst. For files, delegates to copyfile."""
    copyfile(src, dst, follow_symlinks)

def move(src, dst, copy_function=copy):
    """Stub: no-op. Full impl requires rename or copy+remove."""
    pass
