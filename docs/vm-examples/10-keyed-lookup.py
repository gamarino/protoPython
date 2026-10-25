def lookup(d, obj):
    d["k"] = obj.attr
    return d["k"], obj.method(1)
