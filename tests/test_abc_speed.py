import time
from abc import ABCMeta

print("Testing ABCMeta class creation speed")
start = time.time()
for i in range(100):
    class X(metaclass=ABCMeta):
        pass
end = time.time()
print(f"Created 100 ABCs in {end - start:.2f}s")
