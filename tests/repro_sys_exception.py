import sys
print("sys.exception exists:", hasattr(sys, 'exception'))
print("sys.exc_info exists:", hasattr(sys, 'exc_info'))

ei = sys.exc_info()
print("Initial exc_info:", ei)
print("Initial exc_info type:", type(ei))

try:
    raise ValueError("test error")
except ValueError as e:
    ei_caught = sys.exc_info()
    print("Caught exception via sys.exception():", sys.exception())
    print("Caught exception via sys.exc_info():", ei_caught)
    print("Caught exc_info type:", type(ei_caught))
    print("Exception matches:", sys.exception() is e)
    if isinstance(ei_caught, tuple):
        print("Exc_info value matches:", ei_caught[1] is e)
    else:
        print("Exc_info is NOT a tuple!")
