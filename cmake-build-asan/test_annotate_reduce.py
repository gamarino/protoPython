import functools

def f(): pass

print("reduce:", functools.reduce)
try:
    print("getting annotate on reduce")
    getattr(functools.reduce, "__annotate__")
except Exception as e:
    print("reduce exception", type(e))
