import dis
import types

def dump_code(c):
    print(f"--- Code for {c.co_name} ---")
    dis.dis(c)
    for const in c.co_consts:
        if isinstance(const, types.CodeType):
            dump_code(const)

with open("lib/python3.14/enum.py", "r") as f:
    code = f.read()

c = compile(code, "enum.py", "exec")
dump_code(c)
