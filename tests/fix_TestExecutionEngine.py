import sys, re

filepath = 'test/library/TestExecutionEngine.cpp'
with open(filepath, 'r') as f:
    content = f.read()

# 1. Add listToTuple and vector include at the top
header = '''
#include <vector>
static const proto::ProtoTuple* listToTuple(proto::ProtoContext* ctx, const proto::ProtoList* list) {
    if (!list) return nullptr;
    std::vector<const proto::ProtoObject*> elems;
    unsigned long size = list->getSize(ctx);
    for (unsigned long i = 0; i < size; ++i) {
        elems.push_back(list->getAt(ctx, i));
    }
    return ctx->newTuple(elems);
}
'''
if 'listToTuple' not in content:
    content = content.replace('#include <thread>', '#include <thread>\n' + header)

# 2. Add dummy 0 arguments to all 1-slot opcodes
pattern = r'(->appendLast\([^,]+,\s*[a-zA-Z0-9_>.-]+fromInteger\(protoPython::OP_[A-Z0-9_]+\)\))(?!\s*->appendLast\([^,]+,\s*[a-zA-Z0-9_>.-]+fromInteger\([0-9]+\)\))'
def replacer(match):
    return match.group(1) + '->appendLast(&ctx, ctx.fromInteger(0))'
content = re.sub(pattern, replacer, content)

# 3. Replace executeBytecodeRange and executeMinimalBytecode calls to use listToTuple
content = re.sub(r'executeBytecodeRange\(\s*&ctx,\s*constants,\s*bytecode,\s*names,\s*frame,\s*0,\s*([^\)]+)\)',
                 r'executeBytecodeRange(&ctx, listToTuple(&ctx, constants), listToTuple(&ctx, bytecode), listToTuple(&ctx, names), frame, 0, \1)', content)
                 
content = re.sub(r'executeBytecodeRange\(\s*&ctx,\s*constants,\s*bytecode,\s*nullptr,\s*frame,\s*0,\s*([^\)]+)\)',
                 r'executeBytecodeRange(&ctx, listToTuple(&ctx, constants), listToTuple(&ctx, bytecode), nullptr, frame, 0, \1)', content)

content = re.sub(r'executeMinimalBytecode\s*\(\s*&ctx,\s*constants,\s*bytecode,\s*names,\s*frame\s*\)',
                 r'executeMinimalBytecode(&ctx, listToTuple(&ctx, constants), listToTuple(&ctx, bytecode), listToTuple(&ctx, names), frame)', content)

content = re.sub(r'executeMinimalBytecode\s*\(\s*&ctx,\s*constants,\s*bytecode,\s*nullptr,\s*frame\s*\)',
                 r'executeMinimalBytecode(&ctx, listToTuple(&ctx, constants), listToTuple(&ctx, bytecode), nullptr, frame)', content)

# 4. Fix JumpAbsolute target
content = content.replace('->appendLast(&ctx, ctx.fromInteger(protoPython::OP_JUMP_ABSOLUTE))->appendLast(&ctx, ctx.fromInteger(1))',
                          '->appendLast(&ctx, ctx.fromInteger(protoPython::OP_JUMP_ABSOLUTE))->appendLast(&ctx, ctx.fromInteger(2))')

# 5. Fix StoreSubscr stack
old_store = '''        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_LIST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_DUP_TOP_TWO))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_POP_TOP))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_SUBSCR))->appendLast(&ctx, ctx.fromInteger(0))'''

new_store = '''        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_LIST))->appendLast(&ctx, ctx.fromInteger(2)) // [L]
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2)) // 0 (index / key, TOS2)
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(3)) // 99.0 (value, TOS)
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_SUBSCR))->appendLast(&ctx, ctx.fromInteger(0))'''
content = content.replace(old_store, new_store)

# 6. Change CompiledAssignment check
old_assert = '''    ASSERT_NE(aVal, nullptr);
    EXPECT_TRUE(aVal->isInteger(&ctx));
    EXPECT_EQ(aVal->asLong(&ctx), 3);'''
new_assert = '''    ASSERT_NE(aVal, nullptr);
    if (!aVal->isInteger(&ctx)) {
        if (aVal == PROTO_NONE) {
            std::cerr << "Engine Error: aVal is PROTO_NONE!\\n";
        } else {
            std::cerr << "Engine Error: aVal is NOT an integer! It is: " << aVal->repr(&ctx) << "\\n";
        }
    }
    EXPECT_TRUE(aVal->isInteger(&ctx));
    EXPECT_EQ(aVal->isInteger(&ctx) ? aVal->asLong(&ctx) : 0, 3);'''
content = content.replace(old_assert, new_assert)

with open(filepath, 'w') as f:
    f.write(content)
