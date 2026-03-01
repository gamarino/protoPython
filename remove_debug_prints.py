import os
import re

lib_dir = r"c:\Users\gamar\PycharmProjects\protoPython\lib\python3.14"

for root, dirs, files in os.walk(lib_dir):
    for file in files:
        if file.endswith(".py"):
            path = os.path.join(root, file)
            try:
                with open(path, "r", encoding="utf-8") as f:
                    content = f.read()
            except UnicodeDecodeError:
                with open(path, "r", encoding="latin-1") as f:
                    content = f.read()
            
            # Replace print lines with pass to preserve indentation structure
            def replacer(match):
                indent = match.group(1)
                return f"{indent}pass\n"
            
            new_content = re.sub(r'^([ \t]*)print\(f?["\']DEBUG.*?\n', replacer, content, flags=re.MULTILINE)
            
            if new_content != content:
                print(f"Cleaned {path}")
                try:
                    with open(path, "w", encoding="utf-8") as f:
                        f.write(new_content)
                except UnicodeEncodeError:
                    with open(path, "w", encoding="latin-1") as f:
                        f.write(new_content)
