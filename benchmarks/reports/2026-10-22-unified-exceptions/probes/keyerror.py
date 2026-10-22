def run(n):
    d = {}; c = 0; i = 0
    while i < n:
        try:
            d[i]
        except KeyError:
            c += 1
        i += 1
    return c
print("CHECK", run(200000))
