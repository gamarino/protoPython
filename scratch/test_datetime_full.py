import _datetime

print(f"MINYEAR: {_datetime.MINYEAR}")
print(f"MAXYEAR: {_datetime.MAXYEAR}")

td = _datetime.timedelta(days=1, hours=1)
print(f"td: {td}")
print(f"td.total_seconds(): {td.total_seconds()}")

d = _datetime.date(year=2024, month=2, day=29)
print(f"d: {d}")

dt = _datetime.datetime(2024, 2, 29, 12, 30, 45)
print(f"dt: {dt}")

t = _datetime.time(hour=23, minute=59, second=59)
print(f"t: {t}")
