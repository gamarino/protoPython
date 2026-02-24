import sys

filepath = 'src/library/ExecutionEngine.cpp'
with open(filepath, 'r') as f:
    content = f.read()

# Add a print at the top of executeBytecodeRange loop
old = '''        int op = bytecode->getAt(ctx, static_cast<int>(i))->asLong(ctx);
        // Every opcode in protoPython now consumes 2 slots'''
new = '''        int op = bytecode->getAt(ctx, static_cast<int>(i))->asLong(ctx);
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::cerr << "EXEC OP: " << op << " at i=" << i << "\n";
        }
        // Every opcode in protoPython now consumes 2 slots'''

content = content.replace(old, new)
with open(filepath, 'w') as f:
    f.write(content)
