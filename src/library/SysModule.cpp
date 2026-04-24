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
        if (env) env->raiseValueError(context, PythonEnvironment::getInternedString(context, "_getframe() depth must be >= 0")->asObject(context));
        return PROTO_NONE;
    }

    const proto::ProtoObject* frame = PythonEnvironment::getCurrentFrame();
    const proto::ProtoString* f_back_s = env ? env->getFBackString() : proto::ProtoString::createSymbol(context, "f_back");
    
    for (int i = 0; i < depth; ++i) {
        if (!frame || frame == PROTO_NONE) {
            if (env) env->raiseValueError(context, PythonEnvironment::getInternedString(context, "call stack is not deep enough")->asObject(context));
            return PROTO_NONE;
        }
        frame = frame->getAttribute(context, f_back_s);
    }
    
    if (!frame || frame == PROTO_NONE) {
        if (env) env->raiseValueError(context, PythonEnvironment::getInternedString(context, "call stack is not deep enough")->asObject(context));
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
                    if (env) env->raiseValueError(context, PythonEnvironment::getInternedString(context, "recursion limit must be > 0")->asObject(context));
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

static const proto::ProtoObject* sys_exception(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    const proto::ProtoObject* exc = PythonEnvironment::getActiveException();
    return exc ? exc : PROTO_NONE;
}

static const proto::ProtoObject* sys_exc_info(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* exc = PythonEnvironment::getActiveException();
    if (!exc || exc == PROTO_NONE) {
        const proto::ProtoList* l = context->newList()->appendLast(context, PROTO_NONE)->appendLast(context, PROTO_NONE)->appendLast(context, PROTO_NONE);
        const proto::ProtoObject* tupleObj = context->newTupleFromList(l)->asObject(context);
        if (env && env->getTuplePrototype()) {
            tupleObj = tupleObj->addParent(context, env->getTuplePrototype());
            tupleObj = tupleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "__class__"), env->getTuplePrototype());
        }
        return tupleObj;
    }
    const proto::ProtoObject* type = env ? env->getType(context, exc) : PROTO_NONE;
    const proto::ProtoObject* tb = env ? env->getAttribute(context, exc, PythonEnvironment::getInternedString(context, "__traceback__"), false) : PROTO_NONE;
    if (!tb) tb = PROTO_NONE;
    
    const proto::ProtoList* l = context->newList()->appendLast(context, type)->appendLast(context, exc)->appendLast(context, tb);
    const proto::ProtoObject* tupleObj = context->newTupleFromList(l)->asObject(context);
    if (env && env->getTuplePrototype()) {
        tupleObj = tupleObj->addParent(context, env->getTuplePrototype());
        tupleObj = tupleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "__class__"), env->getTuplePrototype());
    }
    return tupleObj;
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
    return PythonEnvironment::getInternedString(context, "utf-8")->asObject(context);
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
    return PythonEnvironment::getInternedString(context, "surrogateescape")->asObject(context);
}

static const proto::ProtoObject* sys_is_remote_debug_enabled(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)context; (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    return PROTO_FALSE;
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

    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "exception"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_exception));
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "exc_info"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_exc_info));
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
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "is_remote_debug_enabled"), ctx->fromMethod(const_cast<proto::ProtoObject*>(sys), sys_is_remote_debug_enabled));
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
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "platform"), PythonEnvironment::getInternedString(ctx, plat)->asObject(ctx));
    
    // sys.byteorder
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    const char* bo = "big";
#else
    const char* bo = "little";
#endif
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "byteorder"), PythonEnvironment::getInternedString(ctx, bo)->asObject(ctx));

    // sys.abiflags — empty string on non-CPython (used by sysconfig)
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "abiflags"), PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
    // sys.platlibdir — standard lib dir name (used by sysconfig)
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "platlibdir"), PythonEnvironment::getInternedString(ctx, "lib")->asObject(ctx));
    // sys.base_prefix / sys.base_exec_prefix — used by sysconfig
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "base_prefix"), PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "base_exec_prefix"), PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
    
    // sys.version
    // sys.version
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "version"), PythonEnvironment::getInternedString(ctx, "3.14.0 (protoPython 1.0.0, Apr 2026)")->asObject(ctx));

    // sys.base_prefix, sys.prefix, sys.exec_prefix, sys.base_exec_prefix
    if (env) {
        std::string sl = env->getStdLibPath();
        if (sl.empty()) sl = ".";
        const proto::ProtoObject* prefixVal = PythonEnvironment::getInternedString(ctx, sl.c_str())->asObject(ctx);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "base_prefix"),      prefixVal);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "prefix"),           prefixVal);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "exec_prefix"),      prefixVal);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "base_exec_prefix"), prefixVal);
    } else {
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "base_prefix"),      PROTO_NONE);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "prefix"),           PROTO_NONE);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "exec_prefix"),      PROTO_NONE);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "base_exec_prefix"), PROTO_NONE);
    }

    // sys.pycache_prefix: None means use default __pycache__ dirs (we don't write .pyc files)
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pycache_prefix"), PROTO_NONE);

    // sys.path (empty for now, PythonEnvironment will populate it)
    const proto::ProtoObject* pathList = ctx->newList()->asObject(ctx);
    if (env && env->getListPrototype()) {
        pathList = pathList->addParent(ctx, env->getListPrototype());
    }
    sys = sys->setAttribute(ctx, env ? env->getPathS() : PythonEnvironment::getInternedString(ctx, "path"), pathList);

    // sys.modules (dict mapping names to modules)
    const proto::ProtoObject* modulesObj = env && env->getDictPrototype() ? env->getDictPrototype()->newChild(ctx, true) : ctx->newObject(false);
    if (env) {
        modulesObj = env->initDictStorage(ctx, modulesObj);
    }
    sys = sys->setAttribute(ctx, env ? env->getModulesS() : PythonEnvironment::getInternedString(ctx, "modules"), modulesObj);

    // sys.meta_path — list of import finders (empty; protoPython uses native registration)
    {
        const proto::ProtoObject* metaPathList = ctx->newList()->asObject(ctx);
        if (env && env->getListPrototype()) metaPathList = metaPathList->addParent(ctx, env->getListPrototype());
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "meta_path"), metaPathList);
    }

    // sys.path_hooks — list of path hook factories (empty)
    {
        const proto::ProtoObject* pathHooksList = ctx->newList()->asObject(ctx);
        if (env && env->getListPrototype()) pathHooksList = pathHooksList->addParent(ctx, env->getListPrototype());
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "path_hooks"), pathHooksList);
    }

    // sys.path_importer_cache — empty dict
    {
        const proto::ProtoObject* picObj = env && env->getDictPrototype() ? env->getDictPrototype()->newChild(ctx, true) : ctx->newObject(false);
        if (env) picObj = env->initDictStorage(ctx, picObj);
        sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "path_importer_cache"), picObj);
    }

    // sys.argv
    const proto::ProtoList* argvList = ctx->newList();
    if (argv) {
        for (const auto& s : *argv) {
            const proto::ProtoObject* strObj = PythonEnvironment::getInternedString(ctx, s.c_str())->asObject(ctx);
            if (env && env->getStrPrototype()) {
                strObj = strObj->addParent(ctx, env->getStrPrototype());
            }
            argvList = argvList->appendLast(ctx, strObj);
        }
    }
    const proto::ProtoObject* argvWrapper = ctx->newObject(false);
    if (env && env->getListPrototype()) {
        argvWrapper = argvWrapper->addParent(ctx, env->getListPrototype());
        argvWrapper = argvWrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), env->getListPrototype());
    }
    argvWrapper = argvWrapper->setAttribute(ctx, env ? env->getDataString() : PythonEnvironment::getInternedString(ctx, "__data__"), argvList->asObject(ctx));
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "argv"), argvWrapper);

    // sys.warnoptions (empty list by default)
    const proto::ProtoList* warnList = ctx->newList();
    const proto::ProtoObject* warnWrapper = ctx->newObject(false);
    if (env && env->getListPrototype()) {
        warnWrapper = warnWrapper->addParent(ctx, env->getListPrototype());
        warnWrapper = warnWrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), env->getListPrototype());
    }
    warnWrapper = warnWrapper->setAttribute(ctx, env ? env->getDataString() : PythonEnvironment::getInternedString(ctx, "__data__"), warnList->asObject(ctx));
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "warnoptions"), warnWrapper);

    // sys.version_info (3, 14, 0)
    const proto::ProtoList* vi = ctx->newList();
    vi = vi->appendLast(ctx, ctx->fromInteger(3));
    vi = vi->appendLast(ctx, ctx->fromInteger(14));
    vi = vi->appendLast(ctx, ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "version_info"), vi->asObject(ctx));

    const proto::ProtoObject* stats = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, false) : ctx->newObject(false);
    stats = stats->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "calls"), ctx->fromInteger(0));
    stats = stats->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "objects_created"), ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "stats"), stats);

    // sys.builtin_module_names
    const proto::ProtoList* builtinsList = ctx->newList();
    const char* builtin_names[] = {
        "builtins", "sys", "_io", "_os", "posix", "nt", "time", "_thread",
        "_signal", "re", "_weakref", "_warnings", "_collections", "logging",
        "operator", "_operator", "math", "_functools", "itertools", "json",
        "atexit", "exceptions", "_codecs", "_ast", "errno", "stat",
        "_collections_abc"
    };
    for (const char* name : builtin_names) {
        builtinsList = builtinsList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    }
    const proto::ProtoObject* bt = env ? env->newTuple(builtinsList) : ctx->newTupleFromList(builtinsList)->asObject(ctx);
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "builtin_module_names"), bt);

    // sys.executable
    const char* exe_path = (argv && !argv->empty()) ? (*argv)[0].c_str() : "/usr/bin/protopy";
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "executable"), PythonEnvironment::getInternedString(ctx, exe_path)->asObject(ctx));

    // sys.excepthook (AttributeError prevention)
    sys = sys->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "excepthook"), PROTO_NONE);

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
    impl = impl->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "name"), PythonEnvironment::getInternedString(ctx, "protopython")->asObject(ctx));
    const proto::ProtoList* impl_version = ctx->newList();
    impl_version = impl_version->appendLast(ctx, ctx->fromInteger(1));
    impl_version = impl_version->appendLast(ctx, ctx->fromInteger(0));
    impl_version = impl_version->appendLast(ctx, ctx->fromInteger(0));
    impl = impl->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "version"), impl_version->asObject(ctx));
    impl = impl->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cache_tag"), PythonEnvironment::getInternedString(ctx, "protopython-314")->asObject(ctx));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "implementation"), impl);

    // sys.flags
    const proto::ProtoObject* flags = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "debug"),                   ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "inspect"),                 ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "interactive"),             ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "optimize"),                ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "dont_write_bytecode"),     ctx->fromInteger(1));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "no_user_site"),            ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "no_site"),                 ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ignore_environment"),      ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "verbose"),                 ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "bytes_warning"),           ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "quiet"),                   ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "hash_randomization"),      ctx->fromInteger(1));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "isolated"),                ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "dev_mode"),                ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "utf8_mode"),               ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "warn_default_encoding"),   ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "safe_path"),               ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "int_max_str_digits"),      ctx->fromInteger(4300));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "context_aware_warnings"),   ctx->fromInteger(0));
    flags = flags->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "thread_inherit_context"),   ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flags"), flags);

    // sys.maxsize (64-bit signed max)
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "maxsize"), ctx->fromInteger(9223372036854775807LL));

    // sys.hash_info — CPython-compatible hash parameters
    const proto::ProtoObject* hash_info = env && env->getObjectPrototype()
        ? env->getObjectPrototype()->newChild(ctx, true)
        : ctx->newObject(false);
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "width"),    ctx->fromInteger(64));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "modulus"),  ctx->fromInteger(2305843009213693951LL));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "inf"),      ctx->fromInteger(314159));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "nan"),      ctx->fromInteger(0));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "imag"),     ctx->fromInteger(1000003));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "algorithm"), proto::ProtoString::createSymbol(ctx, "siphash24")->asObject(ctx));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "hash_bits"),  ctx->fromInteger(64));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "seed_bits"),  ctx->fromInteger(128));
    hash_info = hash_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cutoff"),     ctx->fromInteger(0));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "hash_info"), hash_info);

    // sys.int_info — CPython-compatible int internal details
    const proto::ProtoObject* int_info = env && env->getObjectPrototype()
        ? env->getObjectPrototype()->newChild(ctx, true)
        : ctx->newObject(false);
    int_info = int_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "bits_per_digit"),     ctx->fromInteger(30));
    int_info = int_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sizeof_digit"),       ctx->fromInteger(4));
    int_info = int_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "default_max_str_digits"), ctx->fromInteger(4300));
    int_info = int_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "str_digits_check_threshold"), ctx->fromInteger(640));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "int_info"), int_info);

    // sys.float_info — IEEE 754 double info
    const proto::ProtoObject* float_info = env && env->getObjectPrototype()
        ? env->getObjectPrototype()->newChild(ctx, true)
        : ctx->newObject(false);
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "max"),       ctx->fromDouble(1.7976931348623157e+308));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "max_exp"),   ctx->fromInteger(1024));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "max_10_exp"), ctx->fromInteger(308));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "min"),       ctx->fromDouble(2.2250738585072014e-308));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "min_exp"),   ctx->fromInteger(-1021));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "min_10_exp"), ctx->fromInteger(-307));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "dig"),       ctx->fromInteger(15));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "mant_dig"),  ctx->fromInteger(53));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "epsilon"),   ctx->fromDouble(2.220446049250313e-16));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "radix"),     ctx->fromInteger(2));
    float_info = float_info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "rounds"),    ctx->fromInteger(1));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "float_info"), float_info);

    // sys._jit — stub for Python 3.14 JIT API (protoPython has no JIT)
    static const auto py_jit_is_enabled = [](proto::ProtoContext* ctx,
                                              const proto::ProtoObject*,
                                              const proto::ParentLink*,
                                              const proto::ProtoList*,
                                              const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        (void)ctx; return PROTO_FALSE;
    };
    const proto::ProtoObject* jit_obj = env && env->getObjectPrototype()
        ? env->getObjectPrototype()->newChild(ctx, true)
        : ctx->newObject(false);
    jit_obj = jit_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "is_enabled"),
        ctx->fromMethod(nullptr, py_jit_is_enabled));
    sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_jit"), jit_obj);

    // sys.monitoring — PEP 669 stub (protoPython has no low-impact monitoring)
    // Required by bdb.py → pdb.py → doctest.py
    {
        auto noop = [](proto::ProtoContext* c, const proto::ProtoObject*, const proto::ParentLink*,
                       const proto::ProtoList*, const proto::ProtoSparseList*) -> const proto::ProtoObject* {
            return PROTO_NONE;
        };
        auto ret_none = [](proto::ProtoContext* c, const proto::ProtoObject*, const proto::ParentLink*,
                           const proto::ProtoList*, const proto::ProtoSparseList*) -> const proto::ProtoObject* {
            return PROTO_NONE;
        };

        // events namespace — bit flag constants used by bdb.py
        const proto::ProtoObject* events = ctx->newObject(false);
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PY_START"),       ctx->fromInteger(1));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PY_RESUME"),      ctx->fromInteger(2));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PY_RETURN"),      ctx->fromInteger(4));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PY_YIELD"),       ctx->fromInteger(8));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PY_THROW"),       ctx->fromInteger(16));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PY_UNWIND"),      ctx->fromInteger(32));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LINE"),           ctx->fromInteger(64));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "JUMP"),           ctx->fromInteger(128));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "RAISE"),          ctx->fromInteger(256));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "STOP_ITERATION"), ctx->fromInteger(512));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "INSTRUCTION"),    ctx->fromInteger(1024));
        events = events->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "NO_EVENTS"),      ctx->fromInteger(0));

        const proto::ProtoObject* monitoring = ctx->newObject(false);
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "events"),      events);
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DEBUGGER_ID"), ctx->fromInteger(0));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DISABLE"),     PROTO_NONE);
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_tool"),          ctx->fromMethod(nullptr, ret_none));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "use_tool_id"),        ctx->fromMethod(nullptr, noop));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "clear_tool_id"),      ctx->fromMethod(nullptr, noop));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "free_tool_id"),       ctx->fromMethod(nullptr, noop));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "register_callback"),  ctx->fromMethod(nullptr, noop));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "set_events"),         ctx->fromMethod(nullptr, noop));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "set_local_events"),   ctx->fromMethod(nullptr, noop));
        monitoring = monitoring->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "restart_events"),     ctx->fromMethod(nullptr, noop));
        sys = sys->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "monitoring"), monitoring);
    }

    return sys;
}

} // namespace sys
} // namespace protoPython
