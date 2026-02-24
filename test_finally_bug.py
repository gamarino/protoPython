def test_finally():
    try:
        print("In try")
        return "returned"
    finally:
        print("In finally")

print("Result:", test_finally())
