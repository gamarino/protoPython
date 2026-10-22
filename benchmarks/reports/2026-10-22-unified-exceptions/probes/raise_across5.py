def f(d):
    if d == 0:
        raise ValueError("deep")
    return f(d - 1)
def run(n):
    c = 0; i = 0
    while i < n:
        try:
            f(5)
        except ValueError:
            c += 1
        i += 1
    return c
print("CHECK", run(100000))
