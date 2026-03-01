import sys
sys.stdout.write("Test started\n")
sys.stdout.flush()

class A:
    pass

a = A()
a.test = lambda: 1

sys.stdout.write("Test ending\n")
sys.stdout.flush()
