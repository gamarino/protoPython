#include <protoPython/BisectModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

namespace protoPython {
namespace bisect {

static const proto::ProtoObject* py_bisect_right(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* /*kwArgs*/) {
    
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* x = posArgs->getAt(ctx, 1);
    
    long long lo = 0;
    long long hi = -1;
    
    if (posArgs->getSize(ctx) > 2) lo = posArgs->getAt(ctx, 2)->asLong(ctx);
    if (posArgs->getSize(ctx) > 3) {
        const proto::ProtoObject* hiObj = posArgs->getAt(ctx, 3);
        if (hiObj && !hiObj->isNone(ctx)) hi = hiObj->asLong(ctx);
    }
    
    if (hi == -1) {
        if (a->asList(ctx)) hi = (long long)a->asList(ctx)->getSize(ctx);
        else if (a->isTuple(ctx)) hi = (long long)a->asTuple(ctx)->getSize(ctx);
        else hi = 0; // Fallback
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* ltS = env->getLtString();

    while (lo < hi) {
        long long mid = (lo + hi) / 2;
        const proto::ProtoObject* item = nullptr;
        if (a->asList(ctx)) item = a->asList(ctx)->getAt(ctx, (size_t)mid);
        else if (a->isTuple(ctx)) item = a->asTuple(ctx)->getAt(ctx, (size_t)mid);
        
        // x < item ?
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, item);
        const proto::ProtoObject* res = x->call(ctx, nullptr, ltS, x, args, nullptr);
        if (res == PROTO_TRUE) hi = mid;
        else lo = mid + 1;
    }
    
    return ctx->fromInteger(lo);
}

static const proto::ProtoObject* py_bisect_left(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* /*kwArgs*/) {
    
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* x = posArgs->getAt(ctx, 1);
    
    long long lo = 0;
    long long hi = -1;
    
    if (posArgs->getSize(ctx) > 2) lo = posArgs->getAt(ctx, 2)->asLong(ctx);
    if (posArgs->getSize(ctx) > 3) {
        const proto::ProtoObject* hiObj = posArgs->getAt(ctx, 3);
        if (hiObj && !hiObj->isNone(ctx)) hi = hiObj->asLong(ctx);
    }
    
    if (hi == -1) {
        if (a->asList(ctx)) hi = (long long)a->asList(ctx)->getSize(ctx);
        else if (a->isTuple(ctx)) hi = (long long)a->asTuple(ctx)->getSize(ctx);
        else hi = 0;
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* ltS = env->getLtString();

    while (lo < hi) {
        long long mid = (lo + hi) / 2;
        const proto::ProtoObject* item = nullptr;
        if (a->asList(ctx)) item = a->asList(ctx)->getAt(ctx, (size_t)mid);
        else if (a->isTuple(ctx)) item = a->asTuple(ctx)->getAt(ctx, (size_t)mid);
        
        // item < x ?
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, x);
        const proto::ProtoObject* res = item->call(ctx, nullptr, ltS, item, args, nullptr);
        if (res == PROTO_TRUE) lo = mid + 1;
        else hi = mid;
    }
    
    return ctx->fromInteger(lo);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bisect_right"), ctx->fromMethod(nullptr, py_bisect_right));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bisect_left"), ctx->fromMethod(nullptr, py_bisect_left));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bisect"), ctx->fromMethod(nullptr, py_bisect_right));
    // insort versions could be added too, but bisect is usually enough for test.support
    return mod;
}

} // namespace bisect
} // namespace protoPython
