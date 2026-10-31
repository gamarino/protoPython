# Exactly one platform module exists, as in CPython: posix on POSIX systems,
# nt on Windows.  The stdlib reads a successful `import nt` as Windows.
import os
import sys

names = set(sys.builtin_module_names)
if os.name == "posix":
    assert "posix" in names and "nt" not in names, sorted(names)
    try:
        import nt  # noqa: F401
    except ImportError:
        pass
    else:
        raise AssertionError("nt imported on a POSIX system")
    import ntpath
    # Without nt, ntpath computes an absolute path lexically.
    assert ntpath.abspath("C:\\x\\..\\y") == "C:\\y", ntpath.abspath("C:\\x\\..\\y")
else:
    assert "nt" in names and "posix" not in names, sorted(names)
    import nt  # noqa: F401
print("platform_module_only: ok")
