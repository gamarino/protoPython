import re

def patch_opcode_metadata():
    with open('include/protoPython/ExecutionEngine.h', 'r') as f:
        content = f.read()

    opcodes = {}
    for line in content.splitlines():
        if line.startswith('constexpr int OP_'):
            match = re.match(r'constexpr int OP_([A-Z0-9_]+) = (\d+);', line)
            if match:
                opcodes[match.group(1)] = int(match.group(2))

    with open('lib/python3.14/_opcode_metadata.py', 'r') as f:
        meta = f.read()

    # We want to replace the opmap dict.
    # We will just append defining a new opmap at the end of the file.
    
    new_opmap = "opmap = {\n"
    for name, val in opcodes.items():
        new_opmap += f"    '{name}': {val},\n"
    new_opmap += "}\n"

    # also update _specialized_opmap to empty? It might clash
    new_opmap += "_specialized_opmap = {}\n"

    with open('lib/python3.14/_opcode_metadata.py', 'a') as f:
        f.write("\n")
        f.write(new_opmap)

if __name__ == '__main__':
    patch_opcode_metadata()
    print("opmap patched successfully!")
