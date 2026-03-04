import sys
import test_exact
mod = sys.modules["test_exact"]
print("sys.modules[test_exact] ptr:", hex(id(mod)))
