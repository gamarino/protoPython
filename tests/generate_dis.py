import re

with open("include/protoPython/ExecutionEngine.h", "r") as f:
    text = f.read()

opcodes = {}
for line in text.split('\n'):
    m = re.search(r'constexpr int (OP_[A-Z_]+)\s*=\s*(\d+);', line)
    if m:
        name = m.group(1).replace('OP_', '')
        val = int(m.group(2))
        opcodes[name] = val

dis_code = """
import sys

opmap = {
"""
for name, val in sorted(opcodes.items(), key=lambda x: x[1]):
    dis_code += f"    '{name}': {val},\n"

dis_code += """}

opname = ['<%r>' % (op,) for op in range(256)]
for name, val in opmap.items():
    if val < len(opname):
        opname[val] = name

def get_instructions(co):
    code = list(co.co_code)
    for i in range(0, len(code), 2):
        op = code[i]
        arg = code[i+1]
        yield (i, opname[op], arg)

def dis(x=None):
    if x is None:
        return
    if hasattr(x, '__code__'):
        co = x.__code__
    elif hasattr(x, 'gi_code'):
        co = x.gi_code
    elif hasattr(x, 'co_code'):
        co = x
    else:
        print("Cannot disassemble", type(x))
        return

    code = list(co.co_code)
    for i in range(0, len(code), 2):
        op = code[i]
        arg = code[i+1]
        name = opname[op]
        print(f"{i//2:4} {name:<20} {arg}")

"""

with open("lib/python3.14/dis.py", "w") as f:
    f.write(dis_code)

print("Generated lib/python3.14/dis.py")
