# The path-based import machinery: sys.path_hooks, sys.path_importer_cache,
# PEP 420 namespace packages, a setuptools *-nspkg.pth namespace package and
# pkgutil.iter_modules.  protoPython's `import` statement resolves modules
# natively; importlib's PathFinder/FileFinder serve the programs that use the
# machinery directly (pkgutil, importlib.util.spec_from_file_location, the
# nspkg .pth line), as in CPython.
import io
import os
import site
import sys
import tempfile

root = tempfile.mkdtemp()


def write(path, text=""):
    full = os.path.join(root, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w") as f:
        f.write(text)
    return full


# sys.path_hooks holds the zipimporter hook and FileFinder's hook, and the
# FileFinder hook builds finders that sys.path_importer_cache keeps.
import importlib.machinery
import zipimport

hooks = list(sys.path_hooks)
assert zipimport.zipimporter in hooks, hooks
file_hooks = [h for h in hooks if getattr(h, "__name__", "") == "path_hook_for_FileFinder"]
assert len(file_hooks) == 1, hooks
finder = file_hooks[0](root)
assert isinstance(finder, importlib.machinery.FileFinder), finder
assert finder.path == root, finder.path
if sys.implementation.name == "protopython":
    # Only source files: no .pyc loader (protoPython cannot run CPython
    # bytecode) and no extension loader (the import statement loads those).
    suffixes = sorted(suffix for suffix, _ in finder._loaders)
    assert suffixes == [".py"], suffixes

# PEP 420: a namespace package whose portions live in two sys.path entries.
write("left/protopy_ns_probe/alpha.py", "VALUE = 'alpha'\n")
write("right/protopy_ns_probe/beta.py", "VALUE = 'beta'\n")
left = os.path.join(root, "left")
right = os.path.join(root, "right")
sys.path[0:0] = [left, right]
import protopy_ns_probe.alpha
import protopy_ns_probe.beta
assert protopy_ns_probe.alpha.VALUE == "alpha"
assert protopy_ns_probe.beta.VALUE == "beta"
portions = list(protopy_ns_probe.__path__)
assert portions == [os.path.join(left, "protopy_ns_probe"),
                    os.path.join(right, "protopy_ns_probe")], portions
assert protopy_ns_probe.__spec__.origin is None, protopy_ns_probe.__spec__.origin
assert list(protopy_ns_probe.__spec__.submodule_search_locations) == portions
assert getattr(protopy_ns_probe, "__file__", None) is None

# A regular package or module later on sys.path wins over a namespace portion.
write("left/protopy_ns_shadow/data.txt", "")
write("right/protopy_ns_shadow.py", "KIND = 'module'\n")
import protopy_ns_shadow
assert protopy_ns_shadow.KIND == "module"

# importlib's PathFinder finds the same namespace package through the hooks,
# and caches the finders it used.
spec = importlib.machinery.PathFinder.find_spec("protopy_ns_probe", [left, right])
assert spec is not None and spec.origin is None, spec
assert list(spec.submodule_search_locations) == portions, spec.submodule_search_locations
assert left in sys.path_importer_cache, sorted(sys.path_importer_cache)
assert isinstance(sys.path_importer_cache[left], importlib.machinery.FileFinder)

# A source file loaded through a spec, as plugin loaders do.
import importlib.util
plugin_spec = importlib.util.spec_from_file_location(
    "protopy_plugin_probe", write("plugins/plugin.py", "ANSWER = 42\n"))
plugin = importlib.util.module_from_spec(plugin_spec)
plugin_spec.loader.exec_module(plugin)
assert plugin.ANSWER == 42

# The setuptools nspkg .pth line makes a real namespace package: PathFinder
# returns a namespace spec, module_from_spec builds the module, and its
# submodules import.
sitedir = os.path.join(root, "site")
write("site/protopy_nspkg_ns/inner/__init__.py", "MARK = 'inner'\n")
line = (
    "import sys, types, os;has_mfs = sys.version_info > (3, 5);"
    "p = os.path.join(sys._getframe(1).f_locals['sitedir'], *('protopy_nspkg_ns',));"
    "importlib = has_mfs and __import__('importlib.util');"
    "has_mfs and __import__('importlib.machinery');"
    "m = has_mfs and sys.modules.setdefault('protopy_nspkg_ns', "
    "importlib.util.module_from_spec(importlib.machinery.PathFinder.find_spec("
    "'protopy_nspkg_ns', [os.path.dirname(p)])));"
    "m = m or sys.modules.setdefault('protopy_nspkg_ns', types.ModuleType('protopy_nspkg_ns'));"
    "mp = (m or []) and m.__dict__.setdefault('__path__',[]);"
    "(p not in mp) and mp.append(p)\n"
)
write("site/protopy_nspkg_ns-1.0-nspkg.pth", line)
captured = io.StringIO()
saved_stderr = sys.stderr
sys.stderr = captured
try:
    site.addsitedir(sitedir)
finally:
    sys.stderr = saved_stderr
assert captured.getvalue() == "", captured.getvalue()
nspkg = sys.modules["protopy_nspkg_ns"]
assert nspkg.__spec__ is not None and nspkg.__spec__.origin is None, nspkg.__spec__
assert type(nspkg.__spec__.loader).__name__ == "NamespaceLoader", nspkg.__spec__.loader
assert list(nspkg.__path__) == [os.path.join(sitedir, "protopy_nspkg_ns")], list(nspkg.__path__)
import protopy_nspkg_ns.inner
assert protopy_nspkg_ns.inner.MARK == "inner"

# A submodule found only through the parent's __path__ (a directory that is
# not on sys.path), as pkgutil.extend_path and nspkg packages arrange.
write("elsewhere/protopy_nspkg_ns/extra.py", "MARK = 'extra'\n")
nspkg.__path__.append(os.path.join(root, "elsewhere", "protopy_nspkg_ns"))
import protopy_nspkg_ns.extra
assert protopy_nspkg_ns.extra.MARK == "extra"

# pkgutil.iter_modules over a package directory and over a namespace path.
import pkgutil
write("pkg/protopy_iter_probe/__init__.py")
write("pkg/protopy_iter_probe/first.py")
write("pkg/protopy_iter_probe/sub/__init__.py")
write("pkg/protopy_iter_probe/notes.txt")
found = sorted((name, ispkg) for _, name, ispkg in
               pkgutil.iter_modules([os.path.join(root, "pkg", "protopy_iter_probe")]))
assert found == [("first", False), ("sub", True)], found
names = sorted(name for _, name, _ in pkgutil.iter_modules(protopy_ns_probe.__path__))
assert names == ["alpha", "beta"], names

print("import_path_hooks: ok")
