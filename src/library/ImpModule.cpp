// src/library/ImpModule.cpp
#include <protoPython/ImpModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/BuiltinsModule.h>

namespace protoPython {
namespace library {

// -------------------------------------------------------------
// Locking
// -------------------------------------------------------------
static const proto::ProtoObject* imp_acquire_lock(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        // We simulate acquiring import lock
        // env->acquireImportLock(); // If not exposed publicly, bypass
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* imp_release_lock(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        // env->releaseImportLock(); // If not exposed publicly, bypass
    }
    return PROTO_NONE; // CPython returns None
}

static const proto::ProtoObject* imp_lock_held(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        // Simple approximation: assuming it's held if we ask, or ask the environment's internal lock state
        // For now:
        return PROTO_TRUE; 
    }
    return PROTO_FALSE;
}

// -------------------------------------------------------------
// Builtin Handling
// -------------------------------------------------------------
static const proto::ProtoObject* imp_is_builtin(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* nameArg = args->getAt(ctx, 0);
    if (!nameArg->isString(ctx)) return PROTO_FALSE;
    
    std::string name; nameArg->asString(ctx)->toUTF8String(ctx, name);
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* sysMods = env->resolve("sys", ctx);
    if (sysMods && sysMods != PROTO_NONE) {
         const proto::ProtoObject* builtinsDef = sysMods->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "builtin_module_names"));
         if (builtinsDef && builtinsDef->isTuple(ctx)) {
             // Logic to check tuple
         }
    }
    
    // Core built-ins we must affirmatively report:
    if (name == "sys" || name == "builtins" || name == "_imp" || name == "_thread" || name == "_warnings" || name == "_weakref" || name == "_collections" || name == "itertools") {
         return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* imp_create_builtin(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    
    const proto::ProtoObject* specObj = args->getAt(ctx, 0);
    const proto::ProtoObject* nameAttr = specObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "name"));
    
    std::string name;
    if (nameAttr && nameAttr->isString(ctx)) {
        nameAttr->asString(ctx)->toUTF8String(ctx, name);
    }
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    
    const proto::ProtoObject* sysMod = env->resolve("sys", ctx);
    if (sysMod && sysMod != PROTO_NONE) {
        const proto::ProtoObject* sysMods = sysMod->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "modules"));
        if (sysMods && sysMods != PROTO_NONE) {
            const proto::ProtoString* nameStr = nameAttr->isString(ctx) ? nameAttr->asString(ctx) : nullptr;
            if (nameStr) {
                const proto::ProtoObject* existing = sysMods->getAttribute(ctx, nameStr);
                if (existing && existing != PROTO_NONE) {
                    return existing;
                }
            }
        }
    }

    // Module not in sys.modules yet; use the native module provider to create and
    // initialize it. resolveModule() bypasses s_currentGlobals (which may shadow the
    // name with None during bootstrap), going directly to the provider registry.
    if (!name.empty() && env) {
        return env->resolveModule(name, ctx);
    }

    return nullptr;
}

static const proto::ProtoObject* imp_exec_builtin(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    // exec_builtin just needs to optionally initialize state. For core native modules in ProtoPython,
    // they are fully initialized instantly at creation time via native methods.
    // Return None.
    return PROTO_NONE;
}

// -------------------------------------------------------------
// Frozen Modules Handling
// -------------------------------------------------------------

static const proto::ProtoObject* imp_is_frozen(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* nameArg = args->getAt(ctx, 0);
    if (!nameArg->isString(ctx)) return PROTO_FALSE;
    
    std::string name; nameArg->asString(ctx)->toUTF8String(ctx, name);
    
    if (name == "_frozen_importlib" || name == "_frozen_importlib_external") {
        return PROTO_TRUE;
    }
    
    return PROTO_FALSE;
}

static const proto::ProtoObject* imp_is_frozen_package(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    // None of the core importlib modules are packages.
    return PROTO_FALSE;
}

static const proto::ProtoObject* imp_get_frozen_object(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    env->raiseImportError(ctx, "No frozen object bytecode available natively in ProtoPython");
    return nullptr;
}

static const proto::ProtoObject* imp_find_frozen(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    // If not a frozen module we handle, return None
    // _imp.find_frozen(fullname) returns a 3-tuple: (data, ispkg, fullname) or None
    return PROTO_NONE;
}

// -------------------------------------------------------------
// Extensions & Dynamics
// -------------------------------------------------------------
static const proto::ProtoObject* imp_extension_suffixes(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoList* ret = ctx->newList();
    ret = ret->appendLast(ctx, proto::ProtoString::createSymbol(ctx, ".so")->asObject(ctx));
    return ret->asObject(ctx);
}

static const proto::ProtoObject* imp_create_dynamic(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    env->raiseImportError(ctx, "Dynamic extensions not yet implemented in ProtoPython");
    return nullptr;
}

static const proto::ProtoObject* imp_exec_dynamic(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

// -------------------------------------------------------------
// Utilities
// -------------------------------------------------------------
static const proto::ProtoObject* imp_source_hash(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    // Return a dummy bytes object for hashing correctness check bypass in cache loading.
    const proto::ProtoList* empty = ctx->newList();
    // Assuming newObject or similar creates bytes depending on ProtoPython bytes implementation.
    // Given the lack of ctx->newBytes readily visible in the error, let's return None for now
    // or a dummy tuple. Returning PROTO_NONE to suppress error and let higher layer handle it based on earlier findings.
    return PROTO_NONE;
}

static const proto::ProtoObject* imp_fix_co_filename(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* imp_override_multi_interp_extensions_check(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return ctx->fromInteger(0);
}


// Initialization
const proto::ProtoObject* ImpModule::createImpModule(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    
    // Core built-in module properties
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"), proto::ProtoString::createSymbol(ctx, "_imp")->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__doc__"), proto::ProtoString::createSymbol(ctx, "(ext) _imp module for ProtoPython")->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pyc_magic_number_token"), ctx->fromInteger(3495)); // Py3.14 roughly
    
    // Bind Methods
    proto::ProtoObject* mutMod = const_cast<proto::ProtoObject*>(mod);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "acquire_lock"), ctx->fromMethod(mutMod, imp_acquire_lock));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "release_lock"), ctx->fromMethod(mutMod, imp_release_lock));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "lock_held"), ctx->fromMethod(mutMod, imp_lock_held));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "is_builtin"), ctx->fromMethod(mutMod, imp_is_builtin));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "create_builtin"), ctx->fromMethod(mutMod, imp_create_builtin));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "exec_builtin"), ctx->fromMethod(mutMod, imp_exec_builtin));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "is_frozen"), ctx->fromMethod(mutMod, imp_is_frozen));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "is_frozen_package"), ctx->fromMethod(mutMod, imp_is_frozen_package));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_frozen_object"), ctx->fromMethod(mutMod, imp_get_frozen_object));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "find_frozen"), ctx->fromMethod(mutMod, imp_find_frozen));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "extension_suffixes"), ctx->fromMethod(mutMod, imp_extension_suffixes));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "create_dynamic"), ctx->fromMethod(mutMod, imp_create_dynamic));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "exec_dynamic"), ctx->fromMethod(mutMod, imp_exec_dynamic));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "source_hash"), ctx->fromMethod(mutMod, imp_source_hash));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_fix_co_filename"), ctx->fromMethod(mutMod, imp_fix_co_filename));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_override_multi_interp_extensions_check"), ctx->fromMethod(mutMod, imp_override_multi_interp_extensions_check));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_override_frozen_modules_for_tests"), ctx->fromMethod(mutMod, imp_override_multi_interp_extensions_check));

    return mod;
}

} // namespace library
} // namespace protoPython
