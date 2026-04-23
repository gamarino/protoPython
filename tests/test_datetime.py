import datetime

print(f"date instance: {datetime.date(2024, 2, 29)}")
print(f"timedelta instance: {datetime.timedelta(days=1, seconds=3600)}")
print(f"datetime instance: {datetime.datetime(2024, 2, 29, 12, 30)}")
print(f"time instance: {datetime.time(10, 30, 45)}")

d1 = datetime.date(2024, 3, 15)
print("Asserting d1 fields...")
assert d1.year == 2024
assert d1.month == 3
assert d1.day == 15
print("Asserting d1.isoformat()...")
assert d1.isoformat() == "2024-03-15"

d2 = datetime.date(2024, 3, 16)
print("Asserting d1 < d2...")
assert d1 < d2
assert d1 <= d2
assert d2 > d1
assert d2 >= d1
assert not (d2 < d1)
assert not (d2 <= d1)


dt = datetime.datetime(2024, 3, 15, 12, 0)
print("Asserting isinstance(dt, datetime.date)...")
assert isinstance(dt, datetime.date)
assert dt.year == 2024
assert dt.hour == 12

td1 = datetime.timedelta(days=1)
td2 = datetime.timedelta(hours=24)
print("Asserting td1 == td2...")
assert td1 == td2
print("Asserting (td1 - td2).total_seconds()...")
assert (td1 - td2).total_seconds() == 0

t1 = datetime.time(10, 30, 45)
print("Asserting t1.isoformat()...")
assert t1.isoformat() == "10:30:45"

# timezone.utc exists
print("Asserting datetime.timezone...")
print(f"datetime.timezone: {datetime.timezone}")
assert datetime.timezone is not None
utc = datetime.timezone.utc
print(f"datetime.timezone.utc: {utc}")
assert utc is not None


# Arithmetic
print("Asserting date arithmetic...")
d3 = d1 + datetime.timedelta(days=1)
assert d3.day == 16
d4 = d3 - datetime.timedelta(days=1)
assert d4.day == 15
td_diff = d2 - d1
assert td_diff.days == 1

print("Asserting datetime arithmetic...")
dt1 = datetime.datetime(2024, 3, 15, 12, 0)
dt2 = dt1 + datetime.timedelta(hours=1)
assert dt2.hour == 13
td_dt_diff = dt2 - dt1
assert td_dt_diff.total_seconds() == 3600

print("Asserting time comparisons...")
t2 = datetime.time(11, 0, 0)
assert t1 < t2
assert t2 > t1
assert t1 != t2

print("Asserting fromtimestamp...")
# Use a known timestamp
ts = 1710504000.0 # 2024-03-15 12:00:00 UTC
dt_from = datetime.datetime.fromtimestamp(ts)
# Note: fromtimestamp uses local time, so we just check it doesn't crash
assert dt_from.year == 2024

print("test_datetime passed")

