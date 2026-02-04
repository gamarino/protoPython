#include <protoPython/ThreadModule.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>

namespace protoPython {
namespace thread_module {

/** Bootstrap run in the new ProtoThread: args = [callable, arg0, arg1, ...]. */
static const proto::ProtoObject* thread_bootstrap(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (!args || args->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = args->getAt(context, 0);
    const proto::ProtoList* argList = context->newList();
    for (unsigned long i = 1; i < args->getSize(context); ++i)
        argList = argList->appendLast(context, args->getAt(context, static_cast<int>(i)));
    return protoPython::invokePythonCallable(context, callable, argList);
}

static const proto::ProtoObject* py_start_new_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);
    /* Build [callable, ...args]. If second arg is a tuple, unpack it; else use (posArgs[1], ...). */
    const proto::ProtoList* argsForThread = ctx->newList()->appendLast(ctx, callable);
    if (posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* second = posArgs->getAt(ctx, 1);
        if (second->asTuple(ctx)) {
            const proto::ProtoTuple* tup = second->asTuple(ctx);
            for (unsigned long i = 0; i < tup->getSize(ctx); ++i)
                argsForThread = argsForThread->appendLast(ctx, tup->getAt(ctx, static_cast<int>(i)));
        } else {
            argsForThread = argsForThread->appendLast(ctx, second);
        }
    }
    const proto::ProtoString* name = proto::ProtoString::fromUTF8String(ctx, "thread");
    const proto::ProtoThread* thread = ctx->space->newThread(ctx, name, thread_bootstrap, argsForThread, nullptr);
    return ctx->fromExternalPointer(const_cast<proto::ProtoThread*>(thread), nullptr);
}

static const proto::ProtoObject* py_join_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    proto::ProtoThread* thread = static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (thread) thread->join(ctx);
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "start_new_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_start_new_thread));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "join_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_join_thread));
    return mod;
}

} // namespace thread_module
} // namespace protoPython
