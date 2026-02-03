#include <protoPython/SysModule.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace sys {

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

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env) {
    const proto::ProtoObject* sys = ctx->newObject(true);
    
    // Store env pointer for trace functions
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__env_ptr__"), ctx->fromExternalPointer(env));
    
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

    return sys;
}

} // namespace sys
} // namespace protoPython
