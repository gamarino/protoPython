class Demo:
    pass

def test_hasattr_swallow():
    print("Testing raise after hasattr...")
    d = Demo()
    try:
        1 / 0
    except ZeroDivisionError as e:
        # Before fix, hasattr internally caught AttributeError or StopIteration
        # and called clearPendingException(), which wiped the active exception
        # from the except block context!
        hasattr(d, "nonexistent")
        
        # This bare raise would then fail with "reraise outside of except block"
        raise

try:
    test_hasattr_swallow()
except ZeroDivisionError:
    print("SUCCESS: exception correctly reraised!")
except Exception as e:
    print(f"FAILED: caught wrong exception: {e}")
