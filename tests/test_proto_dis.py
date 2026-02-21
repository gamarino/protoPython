import dis

with open("lib/python3.14/argparse.py") as f:
    code = compile(f.read(), "argparse.py", "exec")

if hasattr(code, "co_code"):
    bc = code.co_code
    print("Length of bytecode:", len(bc))
    print("Bytecode at 196:", bc[196], "at 198:", bc[198])
    for i in range(190, 210, 2):
        print(f"[{i}] OP {bc[i]} ARG {bc[i+1]}")
else:
    print("No co_code. dir:", dir(code))
