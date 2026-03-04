print("testing type.__dict__")
try:
    print(type.__dict__)
    print(type(type.__dict__))
except Exception as e:
    print("Caught:", type(e))
print("Done")
