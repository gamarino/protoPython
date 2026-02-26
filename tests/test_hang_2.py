try:
    print(type(iter([])).__mro__)
except Exception as e:
    print("Caught Exception:", type(e), e)
print("Done")
