r = range(5)
try:
    s = str(r)
    print("str(r):", s)
except Exception as e:
    print("Exception in str(r):", type(e), e)
    
try:
    print("r directly:", r)
except Exception as e:
    print("Exception in print(r):", type(e), e)
