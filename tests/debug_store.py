import sys, re

filepath = 'src/library/ExecutionEngine.cpp'
with open(filepath, 'r') as f:
    orig = f.read()

# I am replacing the OP_STORE_NAME body in executeBytecodeRange!
old = '''        } else if (op == OP_STORE_NAME) {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) continue;
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                // Delay pop until done
                if (nameObj->isString(ctx)) {
                    // Update frame (CoW support)
                    std::string nStr;
                    nameObj->asString(ctx)->toUTF8String(ctx, nStr);
                    const proto::ProtoObject* newFrame = frame->setAttribute(ctx, nameObj->asString(ctx), val);
                    frame = const_cast<proto::ProtoObject*>(newFrame);
                    stack.pop_back(); // Pop val now that it's stored'''

new = '''        } else if (op == OP_STORE_NAME) {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) continue;
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                if (nameObj->isString(ctx)) {
                    std::string nStr;
                    nameObj->asString(ctx)->toUTF8String(ctx, nStr);
                    
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::cerr << "OP_STORE_NAME executing! name=" << nStr << " val_ptr=" << (void*)val << " frame=" << (void*)frame << "\\n";
                    }
                    
                    const proto::ProtoObject* newFrame = frame->setAttribute(ctx, nameObj->asString(ctx), val);
                    frame = const_cast<proto::ProtoObject*>(newFrame);
                    
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        const proto::ProtoObject* verify = frame->getAttribute(ctx, nameObj->asString(ctx));
                        std::cerr << "OP_STORE_NAME verified setAttribute! Verify=" << (void*)verify << "\\n";
                    }
                    
                    stack.pop_back();'''

content = orig.replace(old, new)

with open(filepath, 'w') as f:
    f.write(content)

