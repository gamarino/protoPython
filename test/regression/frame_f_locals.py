# Frame objects expose f_locals, f_globals, f_code.co_name, f_back and
# f_lineno.  The motivating case is the setuptools "nspkg" .pth line (shipped
# by e.g. sphinxcontrib-jsmath), which site.addpackage() runs with exec() and
# which reads `sys._getframe(1).f_locals['sitedir']` -- the local variable of
# addpackage() one frame above the exec'd code.
import io
import os
import site
import sys
import tempfile

# Module level: f_locals is the module namespace, the same object as
# f_globals and globals().
here = sys._getframe()
assert here.f_locals is globals(), "module-level f_locals is not globals()"
assert here.f_globals is globals()
assert here.f_locals["__name__"] == __name__
assert isinstance(here.f_lineno, int)


# A function whose locals live in fast slots; the inner function keeps the
# runtime from eliding its frame.
def optimized(a, b=2):
    c = a + b
    frame = sys._getframe()
    keep = lambda: c  # noqa: E731
    return frame.f_locals, frame.f_code.co_name, frame.f_lineno, keep


snap, name, lineno, _ = optimized(1)
assert name == "optimized", name
assert snap["a"] == 1 and snap["b"] == 2 and snap["c"] == 3, dict(snap)
assert isinstance(lineno, int) and lineno > 0, lineno


# A function whose locals live on the frame (it calls locals()).
def mapped(sitedir):
    marker = "m"
    locals()
    return sys._getframe().f_locals


snap = mapped("/some/dir")
assert snap["sitedir"] == "/some/dir" and snap["marker"] == "m", dict(snap)
assert snap.get("missing", "default") == "default"
for internal in ("f_back", "f_code", "f_globals", "f_locals"):
    assert internal not in snap, (internal, dict(snap))


# f_back reaches the caller, and its f_locals holds the caller's variables.
def callee():
    return sys._getframe(1)


def caller(token):
    keep = lambda: token  # noqa: E731
    frame = callee()
    return frame.f_code.co_name, frame.f_locals.get("token"), keep


assert caller("tok")[:2] == ("caller", "tok"), caller("tok")


# exec() in a function: the exec'd code runs in its own frame, so
# _getframe(1) is the function that called exec(), as in CPython, and the
# exec'd code still sees the function's variables.
FOUND = []


def run_exec(sitedir):
    exec("import sys; FOUND.append((sitedir, sys._getframe(1).f_locals['sitedir']))")


run_exec("/exec/dir")
assert FOUND == [("/exec/dir", "/exec/dir")], FOUND


# The nspkg .pth pattern through site.addsitedir.
with tempfile.TemporaryDirectory() as sitedir:
    os.mkdir(os.path.join(sitedir, "protopy_nspkg_probe"))
    line = (
        "import sys, types, os;has_mfs = sys.version_info > (3, 5);"
        "p = os.path.join(sys._getframe(1).f_locals['sitedir'], *('protopy_nspkg_probe',));"
        "importlib = has_mfs and __import__('importlib.util');"
        "has_mfs and __import__('importlib.machinery');"
        "m = has_mfs and sys.modules.setdefault('protopy_nspkg_probe', "
        "importlib.util.module_from_spec(importlib.machinery.PathFinder.find_spec("
        "'protopy_nspkg_probe', [os.path.dirname(p)])));"
        "m = m or sys.modules.setdefault('protopy_nspkg_probe', types.ModuleType('protopy_nspkg_probe'));"
        "mp = (m or []) and m.__dict__.setdefault('__path__',[]);"
        "(p not in mp) and mp.append(p)\n"
    )
    with open(os.path.join(sitedir, "protopy_nspkg_probe-nspkg.pth"), "w") as f:
        f.write(line)
    captured = io.StringIO()
    saved_stderr = sys.stderr
    sys.stderr = captured
    try:
        site.addsitedir(sitedir)
    finally:
        sys.stderr = saved_stderr
    assert captured.getvalue() == "", captured.getvalue()
    module = sys.modules["protopy_nspkg_probe"]
    expected = os.path.join(sitedir, "protopy_nspkg_probe")
    assert expected in list(module.__path__), list(module.__path__)

print("frame_f_locals: ok")
