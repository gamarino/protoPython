# Test Phase 1: Expressions and While Loops
print("--- Unary Operators ---")
a = 10
print(-a)      # Expected: -10
print(+a)      # Expected: 10
print(~a)      # Expected: -11
print(not True) # Expected: False
print(not 0)    # Expected: True

print("--- Augmented Assignments ---")
x = 5
x += 3
print(x) # Expected: 8
x -= 2
print(x) # Expected: 6
x *= 10
print(x) # Expected: 60
x /= 5
print(x) # Expected: 12.0

print("--- While Loops ---")
i = 0
while i < 5:
    print(i)
    i += 1
else:
    print("Loop finished")

print("--- While Loops with Break ---")
j = 0
while j < 10:
    if j == 3:
        break
    print(j)
    j += 1
else:
    print("This should not be printed")

print("End of test")
