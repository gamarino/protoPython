# Time conversions outside 1970..3000 never crash, on every platform.
#
# The C runtime's gmtime/localtime return NULL for values they cannot
# represent (on Windows: anything before 1970 or after 3000), and the result
# was dereferenced. Now:
#   - time.gmtime and every UTC conversion use a calendar computation of their
#     own, so all years from 1 to 9999 (and beyond) work on every platform.
#     CPython on Windows raises OSError for negative values there; protoPython
#     answers instead.
#   - time.localtime and the local-time conversions use the C runtime's
#     time-zone rules; where it cannot represent the value (Windows: before
#     1970, after 3000) they raise OSError, as CPython does on Windows.
#   - A value that does not fit the platform's time_t raises OverflowError, as
#     in CPython.
import sys
import time
import datetime

WINDOWS = sys.platform == "win32"

# gmtime: exact UTC fields, any year.
assert tuple(time.gmtime(0)) == (1970, 1, 1, 0, 0, 0, 3, 1, 0), tuple(time.gmtime(0))
assert tuple(time.gmtime(-86400)) == (1969, 12, 31, 0, 0, 0, 2, 365, 0), tuple(time.gmtime(-86400))
assert tuple(time.gmtime(-1.5)) == (1969, 12, 31, 23, 59, 58, 2, 365, 0), tuple(time.gmtime(-1.5))
assert time.gmtime(1.5).tm_sec == 1
assert tuple(time.gmtime(-62135596800)) == (1, 1, 1, 0, 0, 0, 0, 1, 0), tuple(time.gmtime(-62135596800))
assert tuple(time.gmtime(33000000000)) == (3015, 9, 24, 10, 40, 0, 6, 267, 0), tuple(time.gmtime(33000000000))
assert tuple(time.gmtime(951782400)) == (2000, 2, 29, 0, 0, 0, 1, 60, 0), tuple(time.gmtime(951782400))
g = time.gmtime(-86400)
assert (g.tm_year, g.tm_mon, g.tm_mday) == (1969, 12, 31)

for bad in (1e20, -1e20, float("inf")):
    try:
        time.gmtime(bad)
    except (OverflowError, OSError, ValueError):
        pass
    else:
        raise AssertionError("time.gmtime(%r) did not raise" % bad)

# localtime: works where the C runtime can represent the value.
lt = time.localtime(86400 * 365)
assert lt.tm_year == 1971 or (lt.tm_year == 1970 and lt.tm_mon == 12), tuple(lt)
try:
    before = time.localtime(-1)
except OSError:
    assert WINDOWS, "time.localtime(-1) raised OSError on " + sys.platform
else:
    assert not WINDOWS, "time.localtime(-1) answered on Windows"
    assert before.tm_year in (1969, 1970), tuple(before)
for bad in (1e20, float("inf")):
    try:
        time.localtime(bad)
    except (OverflowError, OSError, ValueError):
        pass
    else:
        raise AssertionError("time.localtime(%r) did not raise" % bad)
# ctime and asctime of out-of-range values report an error, never crash.
try:
    time.ctime(-86400 * 365 * 400)
except (OSError, OverflowError, ValueError):
    pass

# datetime: local conversions as time.localtime, UTC ones always.
try:
    d = datetime.date.fromtimestamp(-86400)
except OSError:
    assert WINDOWS, "date.fromtimestamp(-86400) raised OSError on " + sys.platform
else:
    assert not WINDOWS, "date.fromtimestamp(-86400) answered on Windows"
    assert (d.year, d.month) == (1969, 12) and d.day in (30, 31), d
try:
    dt = datetime.datetime.fromtimestamp(-86400)
except OSError:
    assert WINDOWS
else:
    assert not WINDOWS
    assert dt.year == 1969, dt

u = datetime.datetime.utcfromtimestamp(10 ** 10)
assert (u.year, u.month, u.day, u.hour, u.minute, u.second) == (2286, 11, 20, 17, 46, 40), u
u = datetime.datetime.utcfromtimestamp(33000000000)
assert (u.year, u.month, u.day, u.hour, u.minute, u.second) == (3015, 9, 24, 10, 40, 0), u
u = datetime.datetime.utcfromtimestamp(-86400)
assert (u.year, u.month, u.day, u.hour) == (1969, 12, 31, 0), u
u = datetime.datetime.utcfromtimestamp(1.25)
assert (u.second, u.microsecond) == (1, 250000), u

# Weekdays and the ISO calendar of dates the C runtime cannot represent.
assert datetime.date(1, 1, 1).weekday() == 0
assert datetime.date(1900, 1, 1).weekday() == 0
assert datetime.date(3001, 1, 1).weekday() == 3
assert datetime.date(1969, 7, 20).isoweekday() == 7
assert tuple(datetime.date(1899, 12, 31).isocalendar()) == (1899, 52, 7)
assert tuple(datetime.date(2027, 1, 1).isocalendar()) == (2026, 53, 5)
tt = datetime.date(1960, 3, 1).timetuple()
assert tuple(tt)[:8] == (1960, 3, 1, 0, 0, 0, 1, 61), tuple(tt)

print("time_out_of_range: ok")
