seen = set()
field_names = ['failed', 'attempted']
for name in field_names:
    print(f"checking {name!r} in seen")
    if name in seen:
        print(f"duplicate: {name!r}")
    seen.add(name)
    print(f"seen is now {seen}")
