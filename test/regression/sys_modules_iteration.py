# Every module in sys.modules is listed when sys.modules is iterated.  The
# interpreter's own stores set only the dict's data, so list(sys.modules)
# missed nearly every module, and test.support.import_helper -- which saves
# and restores entries by iterating sys.modules -- left a blocking None
# behind: after import_fresh_module('functools', blocked=['_functools']),
# _functools could not be imported again and test_functools skipped its 117
# C-module tests.
import sys
import functools
import json.decoder
import _thread

names = set(sys.modules)
for name in ("sys", "builtins", "__main__", "functools", "_functools",
             "json", "json.decoder", "_thread"):
    assert name in sys.modules, name
    assert name in names, "%s missing from list(sys.modules)" % name
assert len(names) == len(list(sys.modules.keys())) == len(sys.modules)

# Built-in modules carry BuiltinImporter specs; source modules a
# SourceFileLoader, also the ones loaded before importlib was imported.
import importlib, importlib.machinery as m
assert "json" not in sys.builtin_module_names
assert "sys" in sys.builtin_module_names
assert sys.__spec__.loader is m.BuiltinImporter, sys.__spec__
assert isinstance(json.__spec__, m.ModuleSpec), json.__spec__
assert isinstance(json.__spec__.loader, m.SourceFileLoader), json.__spec__.loader
assert json.__spec__.origin == json.__file__
assert isinstance(functools.__loader__, m.SourceFileLoader), functools.__loader__

from test.support import import_helper
py = import_helper.import_fresh_module("functools", blocked=["_functools"])
c = import_helper.import_fresh_module("functools", fresh=["_functools"])
assert py is not None and c is not None, (py, c)
assert sys.modules["_functools"] is not None
# The C variant uses the native cmp_to_key, the Python one functools.py's.
assert not hasattr(c.cmp_to_key, "__code__")
assert hasattr(py.cmp_to_key, "__code__")
print("sys_modules_iteration: ok")
