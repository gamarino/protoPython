"""Pure-Python fallback for the _datetime C extension module.

Re-exports everything from _pydatetime so that code that imports _datetime
directly still works when the C accelerator is unavailable.
"""

from _pydatetime import *
from _pydatetime import (
    date, datetime, time, timedelta, timezone, tzinfo,
    MINYEAR, MAXYEAR,
)

__all__ = ["date", "datetime", "time", "timedelta", "timezone", "tzinfo",
           "MINYEAR", "MAXYEAR", "UTC"]
