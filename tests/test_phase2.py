
print("--- For Loops ---")
# Simple for loop over list
items = [1, 2, 3]
total = 0
for x in items:
    print(x)
    total += x
print("Total:", total)

print("--- Try-Except ---")
try:
    print("In try")
    # For now, let's just test simple except without type matching if possible
    # but I implemented type matching, so let's try it.
    # Note: ValueError might need to be looked up from builtins
except:
    print("Caught something")
finally:
    print("In finally")

print("--- While-Else ---")
i = 0
while i < 2:
    print(i)
    i += 1
else:
    print("While-else executed")

print("End of test")
