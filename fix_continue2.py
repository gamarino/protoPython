import re

with open('src/library/ExecutionEngine.cpp.bak', 'r') as f:
    lines = f.readlines()

new_lines = []
for idx, line in enumerate(lines):
    if 1640 <= idx <= 4330:
        if 'continue;' in line:
            if 'i =' not in line and 'next_i =' not in line:
                prev_line = lines[idx-1] if idx > 0 else ""
                if 'i = ' not in prev_line and 'next_i = ' not in prev_line:
                    # if it has an if without braces, make it braced
                    if re.search(r'\bif\s*\([^)]+\)\s*continue;', line):
                        line = re.sub(r'(\bif\s*\([^)]+\))\s*continue;', r'\1 { i = next_i; continue; }', line)
                    else:
                        line = line.replace('continue;', '{ i = next_i; continue; }')
    new_lines.append(line)

with open('src/library/ExecutionEngine.cpp', 'w') as f:
    f.writelines(new_lines)
