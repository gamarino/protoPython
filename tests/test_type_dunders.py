print("Type of int:", type(int))
print("Repr of int:", repr(int))
print("Str of int:", str(int))
try:
    print("Has __str__:", hasattr(int, '__str__'))
except Exception as e:
    print("Error checking hasattr:", e)

class A: pass
print("Type of A:", type(A))
print("Str of A:", str(A))
