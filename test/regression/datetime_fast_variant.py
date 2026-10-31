# The "C" datetime variant loads with _pydatetime blocked, as test_datetime's
# _Fast suite imports it; protoPython's _datetime is pure Python but must not
# need the _pydatetime module.
from test.support.import_helper import import_fresh_module
import sys
fast = import_fresh_module("datetime", fresh=["datetime", "_datetime"], blocked=["_pydatetime"])
pure = import_fresh_module("datetime", fresh=["datetime", "_pydatetime"], blocked=["_datetime"])
assert fast is not None and pure is not None
assert fast.date is not pure.date
assert fast.date.__module__ == "datetime" and pure.date.__module__ == "datetime"
assert fast.timedelta(days=1) + fast.timedelta(hours=1) == fast.timedelta(hours=25)
import _datetime
assert _datetime.__name__ == "_datetime"
print("datetime_fast_variant: ok")
