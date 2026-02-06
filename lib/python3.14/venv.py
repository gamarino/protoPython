# venv - Minimal stub for protoPython.
# Full implementation requires creation of venv directory and activate scripts.

def EnvBuilder(system_site_packages=False, clear=False, symlinks=False):
    """Stub: return a minimal builder placeholder."""
    return _StubEnvBuilder()

class _StubEnvBuilder:
    """Stub: create method no-op. Full impl creates bin/activate and interpreter symlink."""
    def create(self, env_dir):
        pass

def create(env_dir, system_site_packages=False, clear=False, symlinks=False):
    """Stub: no-op. Full impl creates virtual environment at env_dir."""
    pass

def _activate_script_path(env_dir):
    """Return path to activate script if present (minimal detection)."""
    import os
    if hasattr(os, 'path') and hasattr(os.path, 'join'):
        for name in ('activate', 'activate.sh', 'activate.csh'):
            p = os.path.join(env_dir, 'bin', name)
            if hasattr(os.path, 'exists') and os.path.exists(p):
                return p
    return None
