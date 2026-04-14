import argparse
print("PROTOPY_SUCCESS: argparse imported")

source_text = open("../lib/python3.14/argparse.py").read()
print(f"PROTOPY_SUCCESS: source length {len(source_text)}")
compiled_code = compile(source_text, "argparse.py", "exec")
print(f"PROTOPY_SUCCESS: compiled_code type: {type(compiled_code)}")

def find_name(c, name):
    if not hasattr(c, "co_consts"): return None
    for const in c.co_consts:
        if hasattr(const, "co_name") and const.co_name == name:
            return const
        res = find_name(const, name)
        if res: return res
    return None

init_code = find_name(compiled_code, "__init__")
if init_code:
    print(f"PROTOPY_SUCCESS: Found __init__ with {len(init_code.co_code)} bytes")
else:
    print("PROTOPY_FAILURE: __init__ not found")
