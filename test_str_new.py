class MyStr(str):
    pass

try:
    print("Testing str.__new__")
    obj = str.__new__(MyStr, "hello")
    print("obj:", obj)
    print("type(obj):", type(obj))
except Exception as e:
    print("Exception:", e)
