import sys

def test():
    print("Testing GenericAlias...")
    try:
        # list[int] triggers __class_getitem__
        ga = list[int]
        print(f"list[int] = {ga}")
        print(f"type(list[int]) = {type(ga)}")
    except Exception as e:
        print(f"GenericAlias error: {e}")

    print("\nTesting UnionType...")
    try:
        # int | str triggers __or__
        u = int | str
        print(f"int | str = {u}")
        print(f"type(int | str) = {type(u)}")
        
        # Check if it works with three types
        u3 = int | str | float
        print(f"int | str | float = {u3}")
    except Exception as e:
        print(f"UnionType error: {e}")

if __name__ == "__main__":
    test()
