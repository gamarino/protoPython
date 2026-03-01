import time

t0 = time.time()
for i in range(10000):
   pass
t1 = time.time()
print("10,000 loop passes took", t1 - t0)

t2 = time.time()
def bar(): pass
for i in range(10000):
   bar()
t3 = time.time()
print("10,000 function calls took", t3 - t2)
