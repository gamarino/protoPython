import _os
print("--- test_os_simple START ---")
print("Testing _os.environ_keys()...")
keys = _os.environ_keys()
print("Keys count (len):", len(keys))
print("Testing _os.environ lookup...")
e = _os.environ
val = e['PATH']
print("PATH length:", len(val))
print("Iterating over keys...")
for key in keys:
    print("Key:", key)
print("Iteration complete.")
