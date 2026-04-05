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
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__env_ptr__"));
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
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__env_ptr__"));
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
        // log removed
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
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__env_ptr__"));
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
        if (env) env->raiseValueError(context, proto::ProtoString::fromUTF8(context, "_getframe() depth must be >= 0")->asObject(context));
        return PROTO_NONE;
    }

    const proto::ProtoObject* frame = PythonEnvironment::getCurrentFrame();
    const proto::ProtoString* f_back_s = env ? env->getFBackString() : proto::ProtoString::createSymbol(context, "f_back");
    
    for (int i = 0; i < depth; ++i) {
        if (!frame || frame == PROTO_NONE) {
            if (env) env->raiseValueError(context, proto::ProtoString::fromUTF8(context, "call stack is not deep enough")->asObject(context));
            return PROTO_NONE;
        }
        frame = frame->getAttribute(context, f_back_s);
    }
    
    if (!frame || frame == PROTO_NONE) {
        if (env) env->raiseValueError(context, proto::ProtoString::fromUTF8(context, "call stack is not deep enough")->asObject(context));
        return PROTO_NONE;
    }
    
    return frame;
}

static const proto::ProtoObject* sys_setrecursionlimit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__env_ptr__"));
    if (envPtr && envPtr->asExternalPointer(context)) {
        auto* env = static_cast<PythonEnvironment*>(envPtr->asExternalPointer(context)->getPointer(context));
        if (positionalParameters->getSize(context) > 0) {
            const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
            if (arg->isInteger(context)) {
                int limit = static_cast<int>(arg->asLong(context));
                if (limit <= 0) {
                    if (env) env->raiseValueError(context, proto::ProtoString::fromUTF8(context, "recursion limit must be > 0")->asObject(context));
                    return PROTO_NONE;
                }
                if (env) env->setRecursionLimit(limit);
            }
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* sys_intern(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) > 0) {
        return positionalParameters->getAt(context, 0);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* sys_getfilesystemencoding(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return proto::ProtoString::fromUTF8(context, "utf-8")->asObject(context);
}

static const proto::ProtoObject* sys_getfilesystemencodeerrors(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return proto::ProtoString::fromUTF8(context, "surrogateescape")->asObject(context);
}

static const proto::ProtoObject* sys_get_cpu_count_config(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return context->fromInteger(-1);
}

static const proto::ProtoObject* sys_file_write(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
        if (arg->isString(context)) {
            std::string s;
            arg->asString(context)->toUTF8String(context, s);
            const proto::ProtoObject* streamType = self->getAttribute(context, proto::ProtoString::createSymbol(context, "_stream_type"));
            if (streamType && streamType->isInteger(context) && streamType->asLong(context) == 2) {
                fprintf(stderr, "%s", s.c_str());
                fflush(stderr);
            } else {
                fprintf(stdout, "%s", s.c_str());
                fflush(stdout);
            }
        }
    }
    return context->fromInteger(0); // Return number of characters or 0
}

static const proto::ProtoObject* sys_file_flush(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* streamType = self->getAttribute(context, proto::ProtoString::createSymbol(context, "_stream_type"));
    if (streamType && streamType->isInteger(context) && streamType->asLong(context) == 2) {
        fflush(stderr);
    } else {
        fflush(stdout);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* sys_getrecursionlimit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* envPtr = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__env_ptr__"));
    if (envPtr && envPtr->asExternalPointer(context)) {
        auto* env = static_cast<PythonEnvironment*>(envPtr->asExternalPointer(context)->getPointer(context));
        if (env) return context->fromInteger(env->getRecursionLimit());
    }
    return context->fromInteger(1000);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env,
                                     const std::vector<std::string>* argv) {
    const proto::ProtoObject* sys = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);

    // Store env pointer for trace functions and exit
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__env_ptr__"), ctx->fromExternalPointer(env));

    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "exit"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_exit));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "settrace"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_settrace));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "gettrace"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_gettrace));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getsizeof"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getsizeof));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_getframe"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getframe));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "setrecursionlimit"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_setrecursionlimit));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getrecursionlimit"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getrecursionlimit));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getfilesystemencoding"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getfilesystemencoding));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getfilesystemencodeerrors"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_getfilesystemencodeerrors));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_get_cpu_count_config"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_get_cpu_count_config));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "intern"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_intern));
    const proto::ProtoObject* traceDefault = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, false) : ctx->newObject(false);
    traceDefault = traceDefault->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(traceDefault), sys_trace_default));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_trace_default"), traceDefault);
    
    // sys.platform
#if defined(_WIN32)
    const char* plat = "win32";
#elif defined(__APPLE__)
    const char* plat = "darwin";
#else
    const char* plat = "linux";
#endif
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "platform"), proto::ProtoString::fromUTF8(ctx, plat)->asObject(ctx));
    
    // sys.byteorder
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    const char* bo = "big";
#else
    const char* bo = "little";
#endif
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "byteorder"), proto::ProtoString::fromUTF8(ctx, bo)->asObject(ctx));
    
    // sys.version
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "version"), proto::ProtoString::fromUTF8(ctx, "3.14.0 (protoPython, Feb 2026)")->asObject(ctx));

    // sys.base_prefix and sys.prefix (Required for many stdlib modules like gettext)
    if (env) {
        std::string sl = env->getStdLibPath();
        if (sl.empty()) sl = ".";
        sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "base_prefix"), proto::ProtoString::fromUTF8(ctx, sl.c_str())->asObject(ctx));
        sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "prefix"), proto::ProtoString::fromUTF8(ctx, sl.c_str())->asObject(ctx));
    } else {
        sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "base_prefix"), PROTO_NONE);
        sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "prefix"), PROTO_NONE);
    }

    // sys.path (empty for now, PythonEnvironment will populate it)
    const proto::ProtoObject* pathList = ctx->newList()->asObject(ctx);
    if (env && env->getListPrototype()) {
        pathList = pathList->addParent(ctx, env->getListPrototype());
    }
    sys = sys->setAttribute(ctx, env ? env->getPathS() : proto::ProtoString::createSymbol(ctx, "path"), pathList);

    // sys.modules (dict mapping names to modules)
    const proto::ProtoObject* modulesObj = env && env->getDictPrototype() ? env->getDictPrototype()->newChild(ctx, true) : ctx->newObject(false);
    if (env) {
        env->initDictStorage(ctx, modulesObj);
    }
    sys = sys->setAttribute(ctx, env ? env->getModulesS() : proto::ProtoString::createSymbol(ctx, "modules"), modulesObj);

    // sys.argv
    const proto::ProtoList* argvList = ctx->newList();
    if (argv) {
        printf("DEBUG SYS MODULE argv size=%zu\n", argv->size());
        for (const auto& s : *argv) {
            const proto::ProtoObject* strObj = proto::ProtoString::fromUTF8(ctx, s.c_str())->asObject(ctx);
            if (env && env->getStrPrototype()) {
                strObj = strObj->addParent(ctx, env->getStrPrototype());
            }
            argvList = argvList->appendLast(ctx, strObj);
        }
    }
    const proto::ProtoObject* argvWrapper = ctx->newObject(false);
    if (env && env->getListPrototype()) {
        argvWrapper = argvWrapper->addParent(ctx, env->getListPrototype());
        argvWrapper = argvWrapper->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__class__"), env->getListPrototype());
    }
    argvWrapper = argvWrapper->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::createSymbol(ctx, "__data__"), argvList->asObject(ctx));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "argv"), argvWrapper);

    // sys.warnoptions (empty list by default)
    const proto::ProtoList* warnList = ctx->newList();
    const proto::ProtoObject* warnWrapper = ctx->newObject(false);
    if (env && env->getListPrototype()) {
        warnWrapper = warnWrapper->addParent(ctx, env->getListPrototype());
        warnWrapper = warnWrapper->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__class__"), env->getListPrototype());
    }
    warnWrapper = warnWrapper->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::createSymbol(ctx, "__data__"), warnList->asObject(ctx));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "warnoptions"), warnWrapper);

    // sys.version_info (3, 14, 0)
    const proto::ProtoList* vi = ctx->newList();
    vi = vi->appendLast(ctx, ctx->fromInteger(3));
    vi = vi->appendLast(ctx, ctx->fromInteger(14));
    vi = vi->appendLast(ctx, ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "version_info"), vi->asObject(ctx));

    const proto::ProtoObject* stats = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, false) : ctx->newObject(false);
    stats = stats->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "calls"), ctx->fromInteger(0));
    stats = stats->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "objects_created"), ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stats"), stats);

    // sys.builtin_module_names
    const proto::ProtoList* builtinsList = ctx->newList();
    const char* builtin_names[] = {
        "builtins", "sys", "_io", "_os", "posix", "nt", "time", "_thread", 
        "_signal", "re", "_weakref", "_collections", "logging", "operator", 
        "_operator", "math", "_functools", "itertools", "json", "atexit", 
        "exceptions", "_codecs", "_ast", "errno", "stat", "_collections_abc"
    };
    for (const char* name : builtin_names) {
        builtinsList = builtinsList->appendLast(ctx, proto::ProtoString::fromUTF8(ctx, name)->asObject(ctx));
    }
    const proto::ProtoObject* bt = ctx->newTupleFromList(builtinsList)->asObject(ctx);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "builtin_module_names"), bt);

    // sys.executable
    const char* exe_path = (argv && !argv->empty()) ? (*argv)[0].c_str() : "/usr/bin/protopy";
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "executable"), proto::ProtoString::fromUTF8(ctx, exe_path)->asObject(ctx));

    // sys.excepthook (AttributeError prevention)
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "excepthook"), PROTO_NONE);

    // sys.last_*
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "last_type"), PROTO_NONE);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "last_value"), PROTO_NONE);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "last_traceback"), PROTO_NONE);

    // V75: Add stdin, stdout, stderr dummy objects
    auto create_dummy_file = [&](int type) {
        const proto::ProtoObject* f = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
        f = f->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "write"), ctx->fromMethod(const_cast<proto::ProtoObject*>(f), sys_file_write));
        f = f->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flush"), ctx->fromMethod(const_cast<proto::ProtoObject*>(f), sys_file_flush));
        f = f->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_stream_type"), ctx->fromInteger(type));
        return f;
    };

    const proto::ProtoObject* stdin_obj = create_dummy_file(0);
    const proto::ProtoObject* stdout_obj = create_dummy_file(1);
    const proto::ProtoObject* stderr_obj = create_dummy_file(2);

    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stdin"), stdin_obj);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stdout"), stdout_obj);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stderr"), stderr_obj);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__stdin__"), stdin_obj);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__stdout__"), stdout_obj);
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__stderr__"), stderr_obj);

    // sys.implementation
    const proto::ProtoObject* impl = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    impl = impl->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "name"), proto::ProtoString::fromUTF8(ctx, "protopython")->asObject(ctx));
    const proto::ProtoList* impl_version = ctx->newList();
    impl_version = impl_version->appendLast(ctx, ctx->fromInteger(0));
    impl_version = impl_version->appendLast(ctx, ctx->fromInteger(2));
    impl_version = impl_version->appendLast(ctx, ctx->fromInteger(0));
    impl = impl->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "version"), impl_version->asObject(ctx));
    impl = impl->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cache_tag"), proto::ProtoString::fromUTF8(ctx, "protopython-314")->asObject(ctx));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "implementation"), impl);

    // sys.flags
    const proto::ProtoObject* flags = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "optimize"), ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ignore_environment"), ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "dont_write_bytecode"), ctx->fromInteger(1));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "dev_mode"), ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "context_aware_warnings"), ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flags"), flags);

    // sys.maxsize (64-bit signed max)
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "maxsize"), ctx->fromInteger(9223372036854775807LL));

    return sys;
}

} // namespace sys
} // namespace protoPython
