s = "abc"
t = type(s)
print(f"s: {s}")
print(f"type(s): {t}")
print(f"str: {str}")
print(f"Match: {t is str}")

class MyStr(str):
    pass

ms = MyStr("def")
print(f"ms: {ms}")
print(f"type(ms): {type(ms)}")
print(f"Match MyStr: {type(ms) is MyStr}")
