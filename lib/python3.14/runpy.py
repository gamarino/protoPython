# runpy - Run Python code from path or module.

def run_path(path_name, init_globals=None, run_name=None):
    """Execute the script at path_name; return the globals dict after execution."""
    if init_globals is None:
        init_globals = {}
    if run_name is None:
        run_name = "<run_path>"
    try:
        with open(path_name) as f:
            source = f.read()
    except Exception:
        return init_globals
    try:
        code = compile(source, path_name, 'exec')
        exec(code, init_globals)
    except Exception:
        raise
    return init_globals

def run_module(mod_name, init_globals=None, run_name=None, alter_sys=False):
    """Run module mod_name as __main__. Stub: returns empty dict; full impl requires import machinery."""
    if init_globals is None:
        init_globals = {}
    if run_name is None:
        run_name = "__main__"
    try:
        mod = __import__(mod_name, level=0)
        init_globals.update(vars(mod))
        if hasattr(mod, '__main__') and callable(getattr(mod, '__main__', None)):
            getattr(mod, '__main__')()
    except Exception:
        pass
    return init_globals
