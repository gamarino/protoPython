# datetime is CPython's _pydatetime (through lib/_datetime.py, which
# re-exports it): the native _datetime it used to load had no fromisoformat,
# strptime, min/max or astimezone, could not be pickled and named its
# classes builtins.datetime.
import datetime as d
import pickle

assert d.datetime.__module__ != "builtins", d.datetime.__module__
assert d.datetime.fromisoformat("2024-01-02T03:04:05").hour == 3
assert d.date.fromisoformat("2024-01-02").day == 2
assert d.datetime.strptime("2024-05", "%Y-%m").month == 5
assert d.datetime.min.year == 1 and d.date.max.year == 9999
aware = d.datetime(2024, 1, 1, tzinfo=d.timezone.utc)
assert aware.astimezone(d.timezone(d.timedelta(hours=2))).hour == 2
assert pickle.loads(pickle.dumps(d.datetime(2024, 1, 2))) == d.datetime(2024, 1, 2)
assert repr(d.datetime(2024, 1, 2, 3, 4, 5)) == "datetime.datetime(2024, 1, 2, 3, 4, 5)"
print("datetime_pure_python: ok")
