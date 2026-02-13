try:
    print("Testing NameError catch...")
    print(non_existent_variable)
except NameError:
    print("Success: Caught NameError")
except Exception as e:
    print(f"Error: Caught {type(e).__name__} instead of NameError")

try:
    print("\nTesting TypeError catch...")
    "string" + 1
except TypeError:
    print("Success: Caught TypeError")
except Exception as e:
    print(f"Error: Caught {type(e).__name__} instead of TypeError")

print("\nException catch tests completed.")
