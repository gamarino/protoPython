import dis
def foo():
    class A:
        pass
dis.dis(foo)
