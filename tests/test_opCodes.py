import dis
with open("lib/python3.14/argparse.py") as f:
    code = compile(f.read(), "argparse.py", "exec")
for instr in dis.get_instructions(code):
    if instr.offset == 198:
        print(instr.opcode, instr.opname, instr.argrepr, instr.starts_line)
