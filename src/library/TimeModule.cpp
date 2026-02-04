#include <protoPython/TimeModule.h>
#include <chrono>
#include <thread>

namespace protoPython {
namespace time_module {

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) return static_cast<double>(obj->asLong(ctx));
    return 0.0;
}

static const proto::ProtoObject* py_time(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    double sec = std::chrono::duration<double>(epoch).count();
    return ctx->fromDouble(sec);
}

static const proto::ProtoObject* py_sleep(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double sec = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (sec > 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(sec));
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "time"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_time));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sleep"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sleep));
    return mod;
}

} // namespace time_module
} // namespace protoPython
