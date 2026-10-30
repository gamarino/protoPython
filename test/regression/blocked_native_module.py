# None in sys.modules blocks an import, also of a module protoPython
# implements natively (CPython: "import of X halted; None in sys.modules").
# test.support.import_fresh_module blocks _datetime this way to test
# _pydatetime; the native module was imported regardless, so test_datetime's
# _Pure half ran against the native classes.
import sys
from test.support.import_helper import import_fresh_module

import _datetime  # native

saved = sys.modules.pop("_datetime")
sys.modules["_datetime"] = None
try:
    try:
        import _datetime
    except ImportError:
        pass
    else:
        raise AssertionError("blocked native module imported")
    try:
        from _datetime import datetime
    except ImportError:
        pass
    else:
        raise AssertionError("blocked native module imported by from-import")
finally:
    sys.modules["_datetime"] = saved

pure = import_fresh_module("datetime", fresh=["datetime", "_pydatetime"], blocked=["_datetime"])
assert pure.datetime.__module__ in ("datetime", "_pydatetime"), pure.datetime.__module__
assert pure.datetime.fromisoformat("2020-01-02T03:04:05").hour == 3
print("blocked_native_module: ok")
