from _collections_abc import Iterator
x = type(iter([]))
print("Calling issubclass")
try:
    print(issubclass(x, Iterator))
except Exception as e:
    print("Caught", type(e), e)
print("Done")
