with open("src/library/ExecutionEngine.cpp", "r") as f:
    code = f.read()

import re
replacement = """            } else if (tup) {
                if (static_cast<int>(tup->getSize(ctx)) < arg) { i = next_i; continue; }
                for (int j = arg - 1; j >= 0; --j) {
                    stack.push_back(tup->getAt(ctx, j));
                }
            } else {
                if (env) env->raiseTypeError(ctx, "cannot unpack non-iterable object");
                i = next_i; continue;
            }"""

# Find the exact if/else block for tup and replace it
code = re.sub(r'\} else if \(tup\) \{\s*if \(static_cast<int>\(tup->getSize\(ctx\)\) < arg\) \{ i = next_i; continue; \}\s*for \(int j = arg - 1; j >= 0; --j\) \{\s*stack\.push_back\(tup->getAt\(ctx, j\)\);\s*\}\s*\}', replacement, code)

with open("src/library/ExecutionEngine.cpp", "w") as f:
    f.write(code)

