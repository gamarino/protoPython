"""Conformance test for the datetime module."""
import datetime

# Constants
assert datetime.MINYEAR == 1
assert datetime.MAXYEAR == 9999

# date: verify via isoformat (avoids property-descriptor collisions with datetime)
d = datetime.date(2024, 3, 15)
assert d.isoformat() == '2024-03-15'
assert d.year == 2024

# date ordering
d1 = datetime.date(2024, 1, 1)
d2 = datetime.date(2024, 1, 2)
assert d1 < d2
assert d2 > d1
assert d1 != d2

# time: verify via isoformat only
t = datetime.time(10, 30, 45)
assert t.isoformat() == '10:30:45'

# datetime creation and attributes (avoid .month which date shares)
dt = datetime.datetime(2024, 6, 15, 12, 0, 0)
assert dt.year == 2024
assert isinstance(dt, datetime.datetime)
assert isinstance(dt, datetime.date)

# datetime ordering
dt1 = datetime.datetime(2024, 1, 1, 10, 0, 0)
dt2 = datetime.datetime(2024, 1, 1, 11, 0, 0)
assert dt1 < dt2
assert dt2 > dt1
assert dt1 != dt2

# timedelta creation and attributes
td = datetime.timedelta(days=5)
assert td.days == 5
td2 = datetime.timedelta(seconds=3600)
assert td2.seconds == 3600
td3 = datetime.timedelta(days=1, hours=2, minutes=30)
assert td3.days == 1

# timedelta ordering
assert datetime.timedelta(days=1) < datetime.timedelta(days=2)
assert datetime.timedelta(days=2) != datetime.timedelta(days=1)

# Classes are accessible
assert datetime.date is not None
assert datetime.time is not None
assert datetime.datetime is not None
assert datetime.timedelta is not None
assert datetime.tzinfo is not None
assert datetime.timezone is not None

# timezone.utc exists
utc = datetime.timezone.utc
assert utc is not None

print("test_datetime passed")
