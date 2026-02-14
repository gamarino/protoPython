class Manager:
    def __init__(self, name, suppress=False):
        self.name = name
        self.suppress = suppress
        self.exited = False

    def __enter__(self):
        print("Enter " + self.name)
        return self

    def __exit__(self, type, val, tb):
        self.exited = True
        print("Exit " + self.name + " with " + str(type))
        return self.suppress

def test_basic():
    print("--- Basic ---")
    m = Manager("Basic")
    with m as mgr:
        print("Inside " + mgr.name)
    print("M exited: " + str(m.exited))

def test_exception():
    print("--- Exception ---")
    m = Manager("Exception")
    try:
        with m:
            print("Raising...")
            raise ValueError("test")
    except ValueError:
        print("Caught ValueError")
    print("M exited: " + str(m.exited))

def test_suppress():
    print("--- Suppress ---")
    m = Manager("Suppress", suppress=True)
    with m:
        print("Raising to be suppressed...")
        raise ValueError("suppressed")
    print("After with (suppressed)")
    print("M exited: " + str(m.exited))

def test_multiple():
    print("--- Multiple ---")
    m1 = Manager("M1")
    m2 = Manager("M2")
    with m1 as a, m2 as b:
        print("Inside multiple " + a.name + " " + b.name)
    print("M1 exited: " + str(m1.exited))
    print("M2 exited: " + str(m2.exited))

test_basic()
test_exception()
test_suppress()
test_multiple()
