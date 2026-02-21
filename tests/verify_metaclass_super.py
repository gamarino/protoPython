class M(type):
    def __new__(mcls, name, bases, namespace, **kwargs):
        print("M.__new__ called")
        return super(M, mcls).__new__(mcls, name, bases, namespace, **kwargs)

print("Defining A...")
try:
    class A(metaclass=M):
        pass
    print("SUCCESS: A created")
except Exception as e:
    import traceback
    traceback.print_exc()
    print(f"FAILURE: {e}")
