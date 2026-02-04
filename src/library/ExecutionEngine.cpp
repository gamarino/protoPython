#include <protoPython/ExecutionEngine.h>
#include <cmath>
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

static const proto::ProtoObject* binaryModulo(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        long long bb = b->asLong(ctx);
        if (bb == 0) return PROTO_NONE;
        return ctx->fromInteger(a->asLong(ctx) % bb);
    }
    if (a->isDouble(ctx) || b->isDouble(ctx)) {
        double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
        double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
        if (bb == 0.0) return PROTO_NONE;
        return ctx->fromDouble(std::fmod(aa, bb));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryPower(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        long long base = a->asLong(ctx);
        long long exp = b->asLong(ctx);
        if (exp < 0) {
            double r = std::pow(static_cast<double>(base), static_cast<double>(exp));
            return ctx->fromDouble(r);
        }
        long long result = 1;
        for (long long i = 0; i < exp; ++i) result *= base;
        return ctx->fromInteger(result);
    }
    double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
    double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
    return ctx->fromDouble(std::pow(aa, bb));
}

static const proto::ProtoObject* binaryFloorDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (b->isInteger(ctx) && b->asLong(ctx) == 0) return PROTO_NONE;
    if (b->isDouble(ctx) && b->asDouble(ctx) == 0.0) return PROTO_NONE;
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        long long aa = a->asLong(ctx);
        long long bb = b->asLong(ctx);
        return ctx->fromInteger(aa / bb);
    }
    double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
    double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
    return ctx->fromInteger(static_cast<long long>(std::floor(aa / bb)));
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
        } else if (op == OP_INPLACE_ADD) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* iadd = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iadd__"));
            if (iadd && iadd->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = iadd->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryAdd(ctx, a, b);
                if (r) stack.push_back(r);
            }
        } else if (op == OP_BINARY_SUBTRACT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binarySubtract(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_INPLACE_SUBTRACT) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* isub = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__isub__"));
            if (isub && isub->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = isub->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binarySubtract(ctx, a, b);
                if (r) stack.push_back(r);
            }
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
        } else if (op == OP_BINARY_MODULO) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryModulo(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_POWER) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryPower(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_FLOOR_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_LSHIFT) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = static_cast<long long>(static_cast<unsigned long long>(av) << bv);
                stack.push_back(ctx->fromInteger(av));
            }
        } else if (op == OP_BINARY_RSHIFT) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = av >> bv;
                stack.push_back(ctx->fromInteger(av));
            }
        } else if (op == OP_BINARY_AND) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                stack.push_back(ctx->fromInteger(av & bv));
            } else {
                const proto::ProtoObject* andM = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__rand__"));
                    if (randM && randM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = randM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_BINARY_OR) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                stack.push_back(ctx->fromInteger(av | bv));
            } else {
                const proto::ProtoObject* orM = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__or__"));
                if (orM && orM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = orM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* rorM = b->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__ror__"));
                    if (rorM && rorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_UNARY_NEGATIVE) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx))
                stack.push_back(ctx->fromInteger(-a->asLong(ctx)));
            else if (a->isDouble(ctx))
                stack.push_back(ctx->fromDouble(-a->asDouble(ctx)));
        } else if (op == OP_UNARY_NOT) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            stack.push_back(isTruthy(ctx, a) ? PROTO_FALSE : PROTO_TRUE);
        } else if (op == OP_UNARY_INVERT) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx)) {
                long long n = a->asLong(ctx);
                stack.push_back(ctx->fromInteger(static_cast<long long>(~static_cast<unsigned long long>(n))));
            } else {
                const proto::ProtoObject* inv = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__invert__"));
                if (inv && inv->asMethod(ctx)) {
                    const proto::ProtoList* noArgs = ctx->newList();
                    const proto::ProtoObject* result = inv->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                    if (result) stack.push_back(result);
                }
            }
        } else if (op == OP_UNARY_POSITIVE) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* pos = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__pos__"));
            if (pos && pos->asMethod(ctx)) {
                const proto::ProtoList* noArgs = ctx->newList();
                const proto::ProtoObject* result = pos->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                if (result) stack.push_back(result);
            } else {
                stack.push_back(a);
            }
        } else if (op == OP_NOP) {
            i++;
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
        } else if (op == OP_STORE_SUBSCR) {
            i++;
            if (stack.size() < 3) continue;
            const proto::ProtoObject* value = stack.back();
            stack.pop_back();
            const proto::ProtoObject* key = stack.back();
            stack.pop_back();
            proto::ProtoObject* container = const_cast<proto::ProtoObject*>(stack.back());
            stack.pop_back();
            const proto::ProtoObject* setitem = container->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__setitem__"));
            if (setitem && setitem->asMethod(ctx)) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
                setitem->asMethod(ctx)(ctx, container, nullptr, args, nullptr);
            }
            /* When container has no __setitem__, subscript assignment is a no-op (e.g. minimal bytecode with BUILD_MAP/BUILD_LIST objects). */
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
        } else if (op == OP_BUILD_TUPLE) {
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
                const proto::ProtoTuple* tup = ctx->newTupleFromList(lst);
                if (tup) stack.push_back(tup->asObject(ctx));
            }
        } else if (op == OP_GET_ITER) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* iterable = stack.back();
            stack.pop_back();
            const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
            if (!iterM || !iterM->asMethod(ctx)) continue;
            const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
            if (it) stack.push_back(it);
        } else if (op == OP_FOR_ITER) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* iterator = stack.back();
            const proto::ProtoObject* nextM = iterator->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
            if (!nextM || !nextM->asMethod(ctx)) continue;
            const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, iterator, nullptr, ctx->newList(), nullptr);
            if (val && val != PROTO_NONE) {
                stack.push_back(val);
            } else {
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg * 2) < n)
                    i = static_cast<unsigned long>(arg * 2) - 1;
            }
        } else if (op == OP_UNPACK_SEQUENCE) {
            i++;
            if (stack.empty() || arg <= 0) continue;
            const proto::ProtoObject* seq = stack.back();
            stack.pop_back();
            const proto::ProtoList* list = nullptr;
            const proto::ProtoTuple* tup = nullptr;
            if (seq->asList(ctx)) list = seq->asList(ctx);
            else if (seq->asTuple(ctx)) tup = seq->asTuple(ctx);
            else {
                const proto::ProtoObject* data = seq->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data) {
                    if (data->asList(ctx)) list = data->asList(ctx);
                    else if (data->asTuple(ctx)) tup = data->asTuple(ctx);
                }
            }
            if (list) {
                if (static_cast<int>(list->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j)
                    stack.push_back(list->getAt(ctx, j));
            } else if (tup) {
                if (static_cast<int>(tup->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j)
                    stack.push_back(tup->getAt(ctx, j));
            }
        } else if (op == OP_LOAD_GLOBAL && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                const proto::ProtoObject* val = frame->getAttribute(ctx, nameObj->asString(ctx));
                if (val && val != PROTO_NONE) stack.push_back(val);
            }
        } else if (op == OP_STORE_GLOBAL && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            if (nameObj->isString(ctx))
                frame->setAttribute(ctx, nameObj->asString(ctx), val);
        } else if (op == OP_BUILD_SLICE) {
            i++;
            if ((arg != 2 && arg != 3) || stack.size() < static_cast<size_t>(arg)) continue;
            long long step = 1;
            if (arg == 3) {
                step = stack.back()->asLong(ctx);
                stack.pop_back();
            }
            const proto::ProtoObject* stopObj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* startObj = stack.back();
            stack.pop_back();
            proto::ProtoObject* sliceObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            sliceObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "start"), startObj);
            sliceObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "stop"), stopObj);
            sliceObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "step"), ctx->fromInteger(step));
            stack.push_back(sliceObj);
        } else if (op == OP_ROT_TWO) {
            if (stack.size() >= 2) {
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                stack.push_back(a);
                stack.push_back(b);
            }
        } else if (op == OP_DUP_TOP) {
            if (!stack.empty())
                stack.push_back(stack.back());
        } else if (op == OP_POP_TOP) {
            if (!stack.empty())
                stack.pop_back();
        }
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

} // namespace protoPython
