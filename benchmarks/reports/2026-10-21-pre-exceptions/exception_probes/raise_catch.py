def run(n):
    c = 0; i = 0
    while i < n:
        try:
            raise ValueError("x")
        except ValueError:
            c += 1
        i += 1
    return c
print("CHECK", run(200000))
