import sys

print(f"Initial recursion limit: {sys.getrecursionlimit()}")

def recurse(n):
    if n <= 0:
        return "bottom"
    return recurse(n-1)

try:
    print("Testing limit 10...")
    sys.setrecursionlimit(10)
    print(f"Current recursion limit: {sys.getrecursionlimit()}")
    recurse(20)
    print("Error: recurse(20) should have failed with recursion limit 10")
except RecursionError:
    print("Success: Caught RecursionError for depth 20 with limit 10")
except Exception as e:
    print(f"Error: Caught unexpected exception: {type(e).__name__}: {e}")

try:
    print("\nTesting limit 100...")
    sys.setrecursionlimit(100)
    print(f"Current recursion limit: {sys.getrecursionlimit()}")
    res = recurse(50)
    print(f"Success: recurse(50) returned {res}")
    
    print("Testing overflow with limit 100...")
    recurse(150)
    print("Error: recurse(150) should have failed with recursion limit 100")
except RecursionError:
    print("Success: Caught RecursionError for depth 150 with limit 100")

print("\nTesting recursion in generators...")
def gen_recurse(n):
    if n > 0:
        yield from gen_recurse(n-1)
    yield n

try:
    sys.setrecursionlimit(10)
    print(f"Current recursion limit: {sys.getrecursionlimit()}")
    print("Running gen_recurse(20)...")
    for x in gen_recurse(20):
        pass
    print("Error: gen_recurse(20) should have failed with recursion limit 10")
except RecursionError:
    print("Success: Caught RecursionError in generator recursion")

print("\nAll recursion tests completed.")
