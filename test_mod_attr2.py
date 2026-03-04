import test_exact
print("has __name__:", hasattr(test_exact, "__name__"))
try:
    print("test_exact.__name__:", test_exact.__name__)
except Exception as e:
    print("Error:", type(e))
