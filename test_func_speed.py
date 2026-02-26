import time

print("Testing function creation speed")
start = time.time()
for i in range(100):
    def f(): pass
end = time.time()
print(f"Created 100 functions in {end - start:.2f}s")

start = time.time()
for i in range(1000):
    a = 1 + 1
end = time.time()
print(f"Executed 1000 additions in {end - start:.2f}s")
