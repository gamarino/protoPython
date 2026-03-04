import sys
import test_exact
mod = sys.modules["test_exact"]
print("id mod:", id(mod), "id test_exact:", id(test_exact))
print("CodeType in globals:", "CodeType" in globals().keys())
