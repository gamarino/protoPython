import sys

print("before")
for i in range(5000):
    print("ñandú línea")
n = 0
for line in sys.stdin:
    n += 1
print("lines read:", n)
print("after")
raise ValueError("boom")
