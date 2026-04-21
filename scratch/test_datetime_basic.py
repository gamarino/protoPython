import datetime
print("Testing datetime.now()...")
try:
    now = datetime.datetime.now()
    print(f"now: {now}")
    print(f"year: {now.year}")
    print(f"month: {now.month}")
    print(f"day: {now.day}")
except Exception as e:
    print(f"datetime.now failure: {e}")

print("\nTesting datetime subtraction...")
d1 = datetime.datetime(2026, 4, 21, 10, 0, 0)
d2 = datetime.datetime(2026, 4, 21, 9, 0, 0)
diff = d1 - d2
print(f"d1 - d2: {diff}")
print(f"diff.days: {diff.days}")
print(f"diff.seconds: {diff.seconds}")
