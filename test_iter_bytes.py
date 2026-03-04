try:
    print(iter(b''))
except Exception as e:
    print("Caught:", type(e), e)
