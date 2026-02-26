import dis, argparse
with open("lib/python3.14/argparse.py") as f:
    code = compile(f.read(), "argparse.py", "exec")

def find_inner_code(c, name):
    for const in c.co_consts:
        if hasattr(const, "co_name") and const.co_name == name:
            return const
        if hasattr(const, "co_consts"):
            res = find_inner_code(const, name)
            if res: return res
    return None

init_code = find_inner_code(code, "__init__")
if init_code:
    print("Found __init__ code")
    bc = init_code.co_code
    for i in range(180, 210, 2):
        if i < len(bc):
            print(f"PC {i}: {bc[i]}")
else:
    print("Not found")
