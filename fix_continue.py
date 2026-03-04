import re

with open('src/library/ExecutionEngine.cpp', 'r') as f:
    lines = f.readlines()

new_lines = []
for idx, line in enumerate(lines):
    # Only target lines within executeBytecodeRange loop, approximately 1640 to 4330
    if 1640 <= idx <= 4330:
        if 'continue;' in line:
            # Check if 'i = ' or 'next_i =' is on the same line or previous line
            if 'i =' not in line and 'next_i =' not in line:
                # Also check previous line for 'i ='
                prev_line = lines[idx-1] if idx > 0 else ""
                if 'i = ' not in prev_line and 'next_i = ' not in prev_line:
                    line = line.replace('continue;', 'i = next_i; continue;')
    new_lines.append(line)

with open('src/library/ExecutionEngine.cpp', 'w') as f:
    f.writelines(new_lines)
