import os

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    modified = False
    new_lines = []
    for line in content.split('\n'):
        if 'fprintf(stderr, "DEBUG:' in line and not 'if (std::getenv' in line and not line.strip().startswith('//'):
            line = line.replace('fprintf(stderr', '// fprintf(stderr')
            modified = True
        new_lines.append(line)
        
    if modified:
        with open(filepath, 'w') as f:
            f.write('\n'.join(new_lines))

for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith('.cpp') or file.endswith('.h'):
            process_file(os.path.join(root, file))
