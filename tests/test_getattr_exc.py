class Dummy: pass
d = Dummy()
val = getattr(d, "missing", False)
print("val:", val)
import sys
print("sys.exc_info() =", sys.exc_info() if hasattr(sys, 'exc_info') else 'no exc_info')
