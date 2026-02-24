v = "abc"
print("getattr str")
is_abs = getattr(v, "__isabstractmethod__", False)
print("done", is_abs)
