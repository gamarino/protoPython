with open("src/library/ExecutionEngine.cpp") as f:
    text = f.read()

import re
import dis
opcodes = {v: k for k, v in dis.opmap.items()}
print("OP 156:", opcodes.get(156))
print("OP 166:", opcodes.get(166))
