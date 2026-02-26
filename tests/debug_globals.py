print("access" in globals())
try:
    print(globals()["access"])
except KeyError as e:
    print("Caught KeyError!", e)
