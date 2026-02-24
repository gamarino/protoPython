l = [("__name__", None), ("__module__", "abc")]
print("starting loop")
for n, v in l:
    print("about to process", n)
    is_abs = getattr(v, "__isabstractmethod__", False)
    print("post-getattr", n)
    if is_abs:
        print("found abstract", n)
print("done loop")
