import os
print("Imported OS, creating garbage to force GC...")
for i in range(50):
    for j in range(2000):
        x = str(j)
print("Done, GC should have run.")
