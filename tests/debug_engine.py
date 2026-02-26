import sys, re

filepath = 'src/library/ExecutionEngine.cpp'
with open(filepath, 'r') as f:
    content = f.read()

# Add a print to OP_STORE_NAME
old_store = '''        } else if (op == OP_STORE_NAME) {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) continue;'''
new_store = '''        } else if (op == OP_STORE_NAME) {
            if (std::getenv("PROTO_ENV_DIAG")) {
                std::cerr << "OP_STORE_NAME: names=" << (names ? names->getSize(ctx) : -1) << " frame=" << (frame != nullptr) << " arg=" << arg << "\\n";
            }
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) {
                    if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "OP_STORE_NAME: empty stack!\\n";
                    continue;
                }'''
content = content.replace(old_store, new_store)

with open(filepath, 'w') as f:
    f.write(content)
