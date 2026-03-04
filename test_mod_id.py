import sys
import test_exact

mod = sys.modules["test_exact"]
print("sys.modules[test_exact] codeType:", hasattr(mod, "CodeType"))
print("sys.modules dict:", hasattr(mod, "__dict__"))
print(dir(mod))
