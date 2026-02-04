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

static const proto::ProtoObject* binaryMultiply(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* r = a->multiply(ctx, b);
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryTrueDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (b->isInteger(ctx) && b->asLong(ctx) == 0) return PROTO_NONE;
    if (b->isDouble(ctx) && b->asDouble(ctx) == 0.0) return PROTO_NONE;
    const proto::ProtoObject* r = a->divide(ctx, b);
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* compareOp(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b, int op) {
    bool result = false;
    int c = a->compare(ctx, b);
    if (op == 0) result = (c == 0);
    else if (op == 1) result = (c != 0);
    else if (op == 2) result = (c < 0);
    else if (op == 3) result = (c <= 0);
    else if (op == 4) result = (c > 0);
    else if (op == 5) result = (c >= 0);
    return result ? PROTO_TRUE : PROTO_FALSE;
}

static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;
    if (obj->isInteger(ctx)) return obj->asLong(ctx) != 0;
    if (obj->isDouble(ctx)) return obj->asDouble(ctx) != 0.0;
    if (obj->isString(ctx)) return obj->asString(ctx)->getSize(ctx) > 0;
    return true;
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
        } else if (op == OP_BINARY_MULTIPLY) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_TRUE_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_COMPARE_OP) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = compareOp(ctx, a, b, arg);
            if (r) stack.push_back(r);
        } else if (op == OP_POP_JUMP_IF_FALSE) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_JUMP_ABSOLUTE) {
            i++;
            if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_LOAD_ATTR && names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                const proto::ProtoObject* val = obj->getAttribute(ctx, nameObj->asString(ctx));
                if (val) stack.push_back(val);
            }
        } else if (op == OP_STORE_ATTR && names && stack.size() >= 2 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                mutableObj->setAttribute(ctx, nameObj->asString(ctx), val);
            }
        } else if (op == OP_BUILD_LIST) {
            i++;
            if (stack.size() >= static_cast<size_t>(arg)) {
                std::vector<const proto::ProtoObject*> elems(arg);
                for (int j = arg - 1; j >= 0; --j) {
                    elems[j] = stack.back();
                    stack.pop_back();
                }
                const proto::ProtoList* lst = ctx->newList();
                for (int j = 0; j < arg; ++j)
                    lst = lst->appendLast(ctx, elems[j]);
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                listObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), lst->asObject(ctx));
                stack.push_back(listObj);
            }
        } else if (op == OP_BINARY_SUBSCR) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* key = stack.back();
            stack.pop_back();
            const proto::ProtoObject* container = stack.back();
            stack.pop_back();
            const proto::ProtoObject* getitem = container->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__getitem__"));
            if (getitem && getitem->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, key);
                const proto::ProtoObject* result = getitem->asMethod(ctx)(ctx, container, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* data = container->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data && data->asList(ctx) && key->isInteger(ctx)) {
                    long long idx = key->asLong(ctx);
                    const proto::ProtoList* list = data->asList(ctx);
                    if (idx >= 0 && static_cast<unsigned long>(idx) < list->getSize(ctx)) {
                        stack.push_back(list->getAt(ctx, static_cast<int>(idx)));
                    }
                } else if (data && data->asSparseList(ctx)) {
                    unsigned long h = key->getHash(ctx);
                    const proto::ProtoObject* val = data->asSparseList(ctx)->getAt(ctx, h);
                    if (val) stack.push_back(val);
                }
            }
        } else if (op == OP_BUILD_MAP) {
            i++;
            if (stack.size() >= static_cast<size_t>(arg * 2)) {
                proto::ProtoObject* mapObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                const proto::ProtoSparseList* data = ctx->newSparseList();
                const proto::ProtoList* keys = ctx->newList();
                for (int j = 0; j < arg; ++j) {
                    const proto::ProtoObject* value = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* key = stack.back();
                    stack.pop_back();
                    unsigned long h = key->getHash(ctx);
                    data = data->setAt(ctx, h, value);
                    keys = keys->appendLast(ctx, key);
                }
                mapObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx));
                mapObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"), keys->asObject(ctx));
                stack.push_back(mapObj);
            }
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
