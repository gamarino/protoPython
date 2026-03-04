import os
print("Imported OS, creating garbage to force GC...")
def spam():
    for j in range(50000):
        x = str(j)
for i in range(20):
    spam()
print("Done, GC should have run.")
