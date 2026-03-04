try:
    x = reversed([])
    print("reversed:", x)
except Exception as e:
    print("Caught:", type(e), e)
