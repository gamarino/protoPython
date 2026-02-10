#include <protoPython/SysModule.h>
#include <protoPython/PythonEnvironment.h>
#include <iostream>

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
        int code = 0;
        if (positionalParameters->getSize(context) > 0) {
            const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
            if (arg->isInteger(context)) code = static_cast<int>(arg->asLong(context));
        }
        if (env) env->raiseSystemExit(context, code);
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

static const proto::ProtoObject* sys_trace_default(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) >= 2 && positionalParameters->getAt(context, 1)->isString(context)) {
        std::string ev;
        positionalParameters->getAt(context, 1)->asString(context)->toUTF8String(context, ev);
        std::cerr << "[settrace] event=" << ev << std::endl;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* sys_getsizeof(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    (void)positionalParameters;
    return context->fromInteger(0);
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

static const proto::ProtoObject* sys_getframe(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    int depth = 0;
    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
        if (arg->isInteger(context)) depth = static_cast<int>(arg->asLong(context));
    }
    
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (depth < 0) {
        if (env) env->raiseValueError(context, context->fromUTF8String("_getframe() depth must be >= 0"));
        return PROTO_NONE;
    }

    const proto::ProtoObject* frame = PythonEnvironment::getCurrentFrame();
    const proto::ProtoString* f_back_s = env ? env->getFBackString() : proto::ProtoString::fromUTF8String(context, "f_back");
    
    for (int i = 0; i < depth; ++i) {
        if (!frame || frame == PROTO_NONE) {
            if (env) env->raiseValueError(context, context->fromUTF8String("call stack is not deep enough"));
            return PROTO_NONE;
        }
        frame = frame->getAttribute(context, f_back_s);
    }
    
    return frame ? frame : PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env,
                                     const std::vector<std::string>* argv) {
    const proto::ProtoObject* sys = ctx->newObject(true);
    if (env && env->getObjectPrototype()) {
        sys = sys->addParent(ctx, env->getObjectPrototype());
    }

    // Store env pointer for trace functions and exit
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__env_ptr__"), ctx->fromExternalPointer(env));

    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exit"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_exit));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "settrace"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_settrace));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "gettrace"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_gettrace));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getsizeof"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getsizeof));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_getframe"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getframe));
    const proto::ProtoObject* traceDefault = ctx->newObject(true);
    if (env && env->getObjectPrototype()) {
        traceDefault = traceDefault->addParent(ctx, env->getObjectPrototype());
    }
    traceDefault = traceDefault->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(traceDefault), sys_trace_default));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_trace_default"), traceDefault);
    
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

    // sys.path (empty for now, PythonEnvironment will populate it). Concurrent reads after init are safe; writes use protoCore setAttribute.
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"), ctx->newList()->asObject(ctx));

    // sys.modules (dict mapping names to modules). Concurrent reads after init are safe; writes use protoCore setAttribute.
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

    const proto::ProtoObject* stats = ctx->newObject(true);
    if (env && env->getObjectPrototype()) {
        stats = stats->addParent(ctx, env->getObjectPrototype());
    }
    stats = stats->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "calls"), ctx->fromInteger(0));
    stats = stats->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "objects_created"), ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "stats"), stats);

    // sys.executable
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "executable"), ctx->fromUTF8String("/usr/bin/protopy"));

    // Step 1340: sys.excepthook
    // Use an internal helper or just leave it for Python code to set. 
    // For now, let's just add the attribute.
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "excepthook"), PROTO_NONE);

    // Step 1335: sys.last_*
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "last_type"), PROTO_NONE);
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "last_value"), PROTO_NONE);
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "last_traceback"), PROTO_NONE);

    return sys;
}

} // namespace sys
} // namespace protoPython
