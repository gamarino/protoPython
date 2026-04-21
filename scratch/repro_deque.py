from collections import deque
d = deque()
d.append(1)
print(f"len: {len(d)}")
assert len(d) == 1
d.append(2)
print(f"len: {len(d)}")
assert len(d) == 2
print("deque test passed")
