def run(n):
    c = 0; i = 0; z = 0
    while i < n:
        try:
            c += 1 // z
        except ZeroDivisionError:
            c += 1
        i += 1
    return c
print("CHECK", run(200000))
