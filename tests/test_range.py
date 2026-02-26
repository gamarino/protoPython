r = range(5)
print("r is:", r)
print("type(r) is:", type(r))
try:
    for i in r:
        print(i)
except Exception as e:
    print("Exception in loop:", type(e), e)
