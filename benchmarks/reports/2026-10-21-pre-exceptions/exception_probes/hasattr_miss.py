class Box: pass
def run(n):
    b = Box(); c = 0; i = 0
    while i < n:
        if not hasattr(b, "missing"): c += 1
        i += 1
    return c
print("CHECK", run(200000))
