def test_is_none(x=None):
    print(f"x directly: {x}")
    print(f"x is None: {x is None}")
    print(f"x == None: {x == None}")
    if x is None:
        print("Block 1 executed (x is None)")
    if x == None:
        print("Block 2 executed (x == None)")
        
test_is_none()
test_is_none(None)
