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
    """Recursively remove directory. Uses os.listdir, os.remove, os.rmdir."""
    import os
    try:
        entries = os.listdir(path)
        for name in entries:
            full = os.path.join(path, name)
            if os.path.isdir(full):
                rmtree(full, ignore_errors, onerror)
            else:
                try:
                    os.remove(full)
                except OSError as e:
                    if not ignore_errors:
                        if onerror:
                            onerror(os.remove, full, e)
                        else:
                            raise
        os.rmdir(path)
    except OSError as e:
        if not ignore_errors:
            if onerror:
                onerror(os.rmdir, path, e)
            else:
                raise

def copy(src, dst, follow_symlinks=True):
    """Copy src to dst. For files, delegates to copyfile."""
    copyfile(src, dst, follow_symlinks)

def move(src, dst, copy_function=copy):
    """Move src to dst. Copy then unlink (rename when same filesystem would require native support)."""
    copy_function(src, dst)
    try:
        import os
        if os.path.isdir(src):
            rmtree(src)
        else:
            os.remove(src)
    except (OSError, IOError):
        pass
