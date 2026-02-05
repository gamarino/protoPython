# subprocess.py - Minimal stub for protoPython.
# Full implementation requires process creation API (fork/exec). See STUBS.md.
# run returns CompletedProcess(0, ...) for import compatibility.

def run(*args, capture_output=False, **kwargs):
    """Stub: returns CompletedProcess(0, b'', b''). Full impl requires process creation."""
    if args and args[0] is not None:
        _ = list(args[0]) if hasattr(args[0], '__iter__') and not isinstance(args[0], str) else args[0]
    return CompletedProcess(0, b'' if capture_output else None, b'' if capture_output else None)


class CompletedProcess:
    """Stub: minimal placeholder for subprocess result."""

    def __init__(self, returncode, stdout, stderr):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


class Popen:
    """Stub: no-op for import compatibility. Full impl requires process creation."""

    def __init__(self, *args, **kwargs):
        pass
