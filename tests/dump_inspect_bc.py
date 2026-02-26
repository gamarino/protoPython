source = open("lib/python3.14/inspect.py", "r").read()
code = compile(source, "inspect.py", "exec")
ops = code.co_code
for i in range(0, len(ops), 2):
    print(f"PC {i:4d}: OP {ops[i]:3d} ARG {ops[i+1]:3d}")
