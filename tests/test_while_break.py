print("Testing while-else with break")
i = 0
found = False
while i < 5:
    print(i)
    if i == 2:
        print("Breaking")
        found = True
        break
    i += 1
else:
    print("Else SHOULD NOT BE PRINTED")

if found:
    print("Break worked correctly")

print("Testing while-else without break")
i = 0
while i < 2:
    print(i)
    i += 1
else:
    print("Else executed correctly")
