#include <protoPython/ExecutionEngine.h>
#include <vector>

namespace protoPython {

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) + b->asLong(ctx));
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string sa, sb;
        a->asString(ctx)->toUTF8String(ctx, sa);
        b->asString(ctx)->toUTF8String(ctx, sb);
        return ctx->fromUTF8String((sa + sb).c_str());
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binarySubtract(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) - b->asLong(ctx));
    return PROTO_NONE;
}

static const proto::ProtoObject* invokeCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args) {
    const proto::ProtoObject* callAttr = callable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) return PROTO_NONE;
    return callAttr->asMethod(ctx)(ctx, callable, nullptr, args, nullptr);
}

const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject* frame) {
    if (!ctx || !constants || !bytecode) return PROTO_NONE;
    std::vector<const proto::ProtoObject*> stack;
    unsigned long n = bytecode->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* instr = bytecode->getAt(ctx, static_cast<int>(i));
        if (!instr->isInteger(ctx)) continue;
        int op = static_cast<int>(instr->asLong(ctx));
        int arg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
            ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;

        if (op == OP_LOAD_CONST) {
            i++;
            if (static_cast<unsigned long>(arg) < constants->getSize(ctx))
                stack.push_back(constants->getAt(ctx, arg));
        } else if (op == OP_RETURN_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            return stack.back();
        } else if (op == OP_LOAD_NAME && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                const proto::ProtoObject* val = frame->getAttribute(ctx, nameObj->asString(ctx));
                if (val && val != PROTO_NONE) stack.push_back(val);
            }
        } else if (op == OP_STORE_NAME && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            if (nameObj->isString(ctx))
                frame->setAttribute(ctx, nameObj->asString(ctx), val);
        } else if (op == OP_BINARY_ADD) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryAdd(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_SUBTRACT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binarySubtract(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_CALL_FUNCTION) {
            i++;
            if (stack.size() < static_cast<size_t>(arg) + 1) continue;
            std::vector<const proto::ProtoObject*> argVec(arg);
            for (int j = arg - 1; j >= 0; --j) {
                argVec[j] = stack.back();
                stack.pop_back();
            }
            const proto::ProtoObject* callable = stack.back();
            stack.pop_back();
            const proto::ProtoList* args = ctx->newList();
            for (int j = 0; j < arg; ++j)
                args = args->appendLast(ctx, argVec[j]);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, args);
            if (result) stack.push_back(result);
        }
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

} // namespace protoPython
