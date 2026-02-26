import sys, re

filepath = 'test/library/TestExecutionEngine.cpp'
with open(filepath, 'r') as f:
    content = f.read()

# I am changing CompiledAssignment to print the disassembled bytecode!
old_assert = '''    ASSERT_NE(codeObj, nullptr);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(ctx.newObject(true));
    protoPython::runCodeObject(&ctx, codeObj, frame);'''
new_assert = '''    ASSERT_NE(codeObj, nullptr);
    
    // Disassemble to see what Compiler actually emitted!
    const proto::ProtoTuple* compCode = codeObj->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "co_code"))->asTuple(&ctx);
    const proto::ProtoTuple* compNames = codeObj->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "co_names"))->asTuple(&ctx);
    const proto::ProtoTuple* compConsts = codeObj->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "co_consts"))->asTuple(&ctx);
    std::cerr << "Compiled code size: " << (compCode ? compCode->getSize(&ctx) : 0) << "\\n";
    if (compCode) {
        for (unsigned long i=0; i<compCode->getSize(&ctx); i+=2) {
            std::cerr << "OP: " << compCode->getAt(&ctx, i)->asLong(&ctx) << " ARG: " << compCode->getAt(&ctx, i+1)->asLong(&ctx) << "\\n";
        }
    }
    
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(ctx.newObject(true));
    proto::ProtoObject* oldFrame = frame;
    protoPython::runCodeObject(&ctx, codeObj, frame);
    std::cerr << "Frame pointer changed? " << (frame != oldFrame) << "\\n";'''
content = content.replace(old_assert, new_assert)

with open(filepath, 'w') as f:
    f.write(content)
