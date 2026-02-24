import sys, re

filepath = 'test/library/TestExecutionEngine.cpp'
with open(filepath, 'r') as f:
    content = f.read()

# Fix executeBytecodeRange
content = re.sub(r'executeBytecodeRange\s*\(\s*&ctx,\s*([_a-zA-Z0-9]+),\s*([_a-zA-Z0-9]+),\s*([_a-zA-Z0-9]+|nullptr),\s*([_a-zA-Z0-9]+),\s*0,\s*([^\)]+)\)',
                 r'executeBytecodeRange(&ctx, listToTuple(&ctx, \1), listToTuple(&ctx, \2), listToTuple(&ctx, \3), \4, 0, \5)', content)

# Fix executeMinimalBytecode
content = re.sub(r'executeMinimalBytecode\s*\(\s*&ctx,\s*([_a-zA-Z0-9]+),\s*([_a-zA-Z0-9]+),\s*([_a-zA-Z0-9]+|nullptr),\s*([_a-zA-Z0-9]+)\s*\)',
                 r'executeMinimalBytecode(&ctx, listToTuple(&ctx, \1), listToTuple(&ctx, \2), listToTuple(&ctx, \3), \4)', content)

# Fix aVal->repr(&ctx) which doesn't exist. PythonEnvironment has reprObject.
# Replace it with aVal->isString(&ctx)
content = content.replace('aVal->repr(&ctx)', 'aVal->isString(&ctx)')

# Also, there's a compilation error with listToTuple(&ctx, nullptr) because listToTuple expects const proto::ProtoList*.
# Actually, the regex replace could generate `listToTuple(&ctx, nullptr)`.
content = content.replace('listToTuple(&ctx, nullptr)', 'nullptr')

with open(filepath, 'w') as f:
    f.write(content)
