import traceback

def test_match():
    print("Testing match statement with dict pattern...")
    data = {"a": 1}
    match data:
        case {"a": 1}:
            print("Match success!")
        case _:
            print("Match failure!")

def test_co_positions():
    print("Testing co_positions...")
    def f():
        pass
    code = f.__code__
    print(f"Code object: {code}")
    try:
        pos = list(code.co_positions())
        print(f"co_positions length: {len(pos)}")
        if len(pos) > 0:
            print(f"First position: {pos[0]}")
            print("co_positions success!")
        else:
            print("co_positions returned empty list (unexpected if bytecode exists)")
    except AttributeError as e:
        print(f"co_positions failed: {e}")
    except Exception as e:
        print(f"co_positions failed with error: {e}")
        traceback.print_exc()

if __name__ == "__main__":
    test_match()
    test_co_positions()
