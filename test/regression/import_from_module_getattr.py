# `from module import name` falls back to the module's __getattr__ (PEP 562),
# as `module.name` does.  unittest provides IsolatedAsyncioTestCase only
# that way, so `from unittest import IsolatedAsyncioTestCase` (CPython's
# testmock/testasync.py) raised ImportError.
import sys
import types

m = types.ModuleType("lazy_mod_for_import_from")
m.__getattr__ = lambda name: "lazy:" + name if name.startswith("x") else (_ for _ in ()).throw(AttributeError(name))
sys.modules[m.__name__] = m
from lazy_mod_for_import_from import xyz
assert xyz == "lazy:xyz"
try:
    from lazy_mod_for_import_from import nope
except ImportError as e:
    assert "nope" in str(e)
else:
    raise AssertionError("missing name imported")

from unittest import IsolatedAsyncioTestCase
assert IsolatedAsyncioTestCase.__name__ == "IsolatedAsyncioTestCase"
print("import_from_module_getattr: ok")
