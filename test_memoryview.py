print("Testing memoryview")
try:
    print(memoryview)
except Exception as e:
    print("Caught", type(e), e)
print("Done")
