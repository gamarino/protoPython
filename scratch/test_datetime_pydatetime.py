import datetime
import time

print(f"datetime module file: {datetime.__file__}")
try:
    from _datetime import datetime as dt
    print(f"_datetime.datetime: {dt}")
except ImportError:
    print("_datetime NOT found")

now = datetime.datetime.now()
print(f"Current datetime: {now}")

d1 = datetime.date(2026, 4, 21)
d2 = datetime.date(2026, 4, 20)
delta = d1 - d2
print(f"Delta: {delta} (type: {type(delta)})")
