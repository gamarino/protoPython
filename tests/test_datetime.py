import datetime

print(f"date instance: {datetime.date(2024, 2, 29)}")
print(f"timedelta instance: {datetime.timedelta(days=1, seconds=3600)}")
print(f"datetime instance: {datetime.datetime(2024, 2, 29, 12, 30)}")
print(f"time instance: {datetime.time(10, 30, 45)}")

d1 = datetime.date(2024, 3, 15)
assert d1.year == 2024
assert d1.month == 3
assert d1.day == 15
assert d1.isoformat() == "2024-03-15"

d2 = datetime.date(2024, 3, 16)
assert d1 < d2
assert not (d2 < d1)

dt = datetime.datetime(2024, 3, 15, 12, 0)
assert isinstance(dt, datetime.date)
assert dt.year == 2024
assert dt.hour == 12

td1 = datetime.timedelta(days=1)
td2 = datetime.timedelta(hours=24)
assert td1 == td2
assert (td1 - td2).total_seconds() == 0

t1 = datetime.time(10, 30, 45)
assert t1.isoformat() == "10:30:45"

# timezone.utc exists
assert datetime.timezone is not None
utc = datetime.timezone.utc
assert utc is not None

print("test_datetime passed")
