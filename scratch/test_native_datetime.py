import _datetime
print(f"Native _datetime: {_datetime}")
print(f"timedelta: {_datetime.timedelta}")
td = _datetime.timedelta(days=1, seconds=3600)
print(f"td: {td}")
print(f"td.days: {td.days}")
print(f"td.seconds: {td.seconds}")
print(f"td.total_seconds(): {td.total_seconds()}")

d = _datetime.date(2026, 4, 21)
print(f"d: {d}")
print(f"d.year: {d.year}")
