try:
    def f(): pass
    print("Function __code__:", f.__code__)
    print("FunctionType __code__:", type(f).__code__)
except Exception as e:
    print("CAUGHT:", type(e))
    print("MESSAGE:", str(e))
