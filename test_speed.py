import time
print("START")

t0 = time.time()
print("t0 =", t0)

src = "def foo(): pass\n" * 1000

t1 = time.time()
print("building string took", t1 - t0)

# We don't have compile(), but we can exec()
exec(src)

t2 = time.time()
print("exec 1000 definitions took", t2 - t1)
