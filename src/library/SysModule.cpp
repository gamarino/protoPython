#include <protoPython/SysModule.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace sys {

static const proto::ProtoObject* sys_exit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__env_ptr__"));
    if (envPtr && envPtr->asExternalPointer(context)) {
        auto* env = static_cast<PythonEnvironment*>(envPtr->asExternalPointer(context)->getPointer(context));
        int code = positionalParameters->getSize(context) > 0
            ? static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context)) : 0;
        if (env) env->setExitRequested(code);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* sys_settrace(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__env_ptr__"));
    if (envPtr) {
        const proto::ProtoExternalPointer* ext = envPtr->asExternalPointer(context);
        if (ext) {
            auto* env = static_cast<PythonEnvironment*>(ext->getPointer(context));
            if (positionalParameters->getSize(context) > 0) {
                env->setTraceFunction(positionalParameters->getAt(context, 0));
            }
        }
    }
    return PROTO_NONE; 
}

static const proto::ProtoObject* sys_gettrace(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__env_ptr__"));
    if (envPtr) {
        const proto::ProtoExternalPointer* ext = envPtr->asExternalPointer(context);
        if (ext) {
            auto* env = static_cast<PythonEnvironment*>(ext->getPointer(context));
            const proto::ProtoObject* traceFunc = env->getTraceFunction();
            return traceFunc ? traceFunc : PROTO_NONE;
        }
    }
    return PROTO_NONE; 
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env,
                                     const std::vector<std::string>* argv) {
    const proto::ProtoObject* sys = ctx->newObject(true);

    // Store env pointer for trace functions and exit
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__env_ptr__"), ctx->fromExternalPointer(env));

    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exit"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_exit));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "settrace"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_settrace));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "gettrace"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_gettrace));
    
    // sys.platform
#if defined(_WIN32)
    const char* plat = "win32";
#elif defined(__APPLE__)
    const char* plat = "darwin";
#else
    const char* plat = "linux";
#endif
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "platform"), ctx->fromUTF8String(plat));
    
    // sys.version
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "version"), ctx->fromUTF8String("3.14.0 (protoPython, Feb 2026)"));

    // sys.path (empty for now, PythonEnvironment will populate it)
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"), ctx->newList()->asObject(ctx));

    // sys.modules (dict mapping names to modules)
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"), ctx->newObject(true));

    // sys.argv
    const proto::ProtoList* argvList = ctx->newList();
    if (argv) {
        for (const auto& s : *argv) {
            argvList = argvList->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
        }
    }
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "argv"), argvList->asObject(ctx));

    // sys.version_info (3, 14, 0)
    const proto::ProtoList* vi = ctx->newList();
    vi = vi->appendLast(ctx, ctx->fromInteger(3));
    vi = vi->appendLast(ctx, ctx->fromInteger(14));
    vi = vi->appendLast(ctx, ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "version_info"), vi->asObject(ctx));

    return sys;
}

} // namespace sys
} // namespace protoPython
