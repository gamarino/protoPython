def return_two():
    return 1, 2

def call_return_two():
    a, b = return_two()
    return a

print(f"return_two(): {return_two()}")
print(f"call_return_two(): {call_return_two()}")

def test_tuple_unpack():
    res = return_two()
    print(f"res type: {type(res)}")
    print(f"res: {res}")
    
test_tuple_unpack()
