import time

print("Testing type class creation speed")
start = time.time()
for i in range(100):
    class X:
        pass
end = time.time()
print(f"Created 100 classes in {end - start:.2f}s")
