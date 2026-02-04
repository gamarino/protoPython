# subprocess.py - Minimal stub. run, Popen so imports and simple references work.

def run(*args, **kwargs):
    """Stub: returns CompletedProcess(returncode=0, stdout=b'', stderr=b'')."""
    return CompletedProcess(0, b'', b'')

class CompletedProcess:
    """Stub: minimal placeholder for subprocess result."""
    def __init__(self, returncode, stdout, stderr):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

class Popen:
    """Stub: minimal placeholder."""
    def __init__(self, *args, **kwargs):
        pass  # Stub: no-op for import compatibility
