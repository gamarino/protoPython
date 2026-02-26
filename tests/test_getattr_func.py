def foo(): pass
foo.__isabstractmethod__ = True
print("getting getattr")
res = getattr(foo, "__isabstractmethod__", False)
print(res)
