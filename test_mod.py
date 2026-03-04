import test_exact
print("globals of test_exact:", test_exact.__dict__ if hasattr(test_exact, "__dict__") else "no dict")
