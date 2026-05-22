class T:
    def run(self):
        class C(object):
            def __new__(cls, arg):
                if isinstance(arg, str): return [1, 2, 3]
                elif isinstance(arg, int): return object.__new__(D)
                else: return object.__new__(cls)
        class D(C):
            def __init__(self, arg):
                self.foo = arg
        print("C('1'):", C("1"))
        d = C(1)
        print("C(1):", d)
        print("type(d):", type(d))
        print("isinstance(d, D):", isinstance(d, D))

T().run()
