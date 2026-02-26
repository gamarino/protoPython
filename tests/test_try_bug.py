try:
    print("Raising...")
    raise ValueError("test")
except ValueError:
    print("Caught")
else:
    print("Else")
print("Done")

print("---")

try:
    print("Normal...")
except ValueError:
    print("Caught")
else:
    print("Else")
print("Done")
