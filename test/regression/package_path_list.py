# A regular package's __path__ is a list holding its directory, shared with
# __spec__.submodule_search_locations, and the spec of a source module
# records its file (CPython).  It used to be a bare internal list: len() 0,
# repr [], and specs said origin 'built-in' with no search locations.
import email
import importlib.util
import json
import json.decoder
import os
import pkgutil

for pkg in (json, email):
    p = pkg.__path__
    assert type(p) is list and len(p) == 1, (type(p), len(p))
    assert repr(p) == repr([os.path.dirname(pkg.__file__)]), repr(p)
    assert os.path.isdir(p[0]) and os.path.basename(p[0]) == pkg.__name__
    spec = pkg.__spec__
    assert spec.name == pkg.__name__
    assert spec.submodule_search_locations == p, spec.submodule_search_locations
    assert spec.origin == pkg.__file__, spec.origin
    assert spec.has_location is True and spec.parent == pkg.__name__

mod = json.decoder
assert not hasattr(mod, "__path__")
assert mod.__spec__.origin == mod.__file__ and mod.__spec__.submodule_search_locations is None
assert mod.__spec__.parent == "json"

names = sorted(m.name for m in pkgutil.iter_modules(json.__path__))
assert {"decoder", "encoder", "scanner"} <= set(names), names
assert importlib.util.find_spec("json.decoder") is not None
json.__path__.append("/nonexistent-dir")
assert json.__path__[-1] == "/nonexistent-dir" and len(json.__path__) == 2
json.__path__.pop()
print("package_path_list: ok")
