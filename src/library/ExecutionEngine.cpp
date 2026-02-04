#include <protoPython/ExecutionEngine.h>
#include <vector>

namespace protoPython {

const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode) {
    if (!ctx || !constants || !bytecode) return PROTO_NONE;
    std::vector<const proto::ProtoObject*> stack;
    unsigned long n = bytecode->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* instr = bytecode->getAt(ctx, static_cast<int>(i));
        if (!instr->isInteger(ctx)) continue;
        int op = static_cast<int>(instr->asLong(ctx));
        if (op == OP_LOAD_CONST) {
            unsigned long idx = (i + 1 < n) ? static_cast<unsigned long>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;
            i++;
            if (idx < constants->getSize(ctx))
                stack.push_back(constants->getAt(ctx, static_cast<int>(idx)));
        } else if (op == OP_RETURN_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* top = stack.back();
            return top;
        }
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

} // namespace protoPython
