import dis

opcodes = [157, 0, 119, 0, 120, 24, 121, 2, 158, 2, 158, 1, 157, 1, 157, 2, 118, 2, 172, 0, 133, 0]

for i in range(0, len(opcodes), 2):
    op = opcodes[i]
    arg = opcodes[i+1]
    name = dis.opname[op] if op < len(dis.opname) else f"UNKNOWN({op})"
    print(f"{i//2}: {name} {arg}")
