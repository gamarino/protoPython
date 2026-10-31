"""protoPython: the _datetime accelerator module, in pure Python.

protoPython has no native datetime; _datetime runs the _pydatetime source in
its own namespace instead of importing the _pydatetime module.  It must not
depend on that module by name: test.support.import_helper.import_fresh_module
loads the "C" variant of datetime with _pydatetime blocked (as
test_datetime's _Fast suite does), and the classes of each variant must be
distinct, as CPython's C and pure-Python classes are.
"""

import os as _os

_source_path = _os.path.join(_os.path.dirname(__file__), "_pydatetime.py")
with open(_source_path, encoding="utf-8") as _f:
    _code = compile(_f.read(), _source_path, "exec")
exec(_code, globals())
# _pydatetime renames its module "datetime" (so its classes report
# __module__ "datetime"); this module keeps its own name.
__name__ = "_datetime"
del _os, _source_path, _f, _code

__all__ = ["date", "datetime", "time", "timedelta", "timezone", "tzinfo",
           "MINYEAR", "MAXYEAR", "UTC"]
