try:
    raise TypeError
except TypeError as exc:
    print("Caught:", type(exc))

def _f(): pass
print("type(_f):", type(_f))
print("type(type.__dict__):", type(type.__dict__))

