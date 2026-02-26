
import sys

print("Testing tuple with map and intern:")
t3 = tuple(map(sys.intern, ['a', 'b']))
print("Result t3:", t3)

print("Testing join on tuple:")
joined = ', '.join(t3)
print("Joined:", joined)
