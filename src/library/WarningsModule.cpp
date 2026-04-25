#include <protoPython/WarningsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <iostream>
#include <string>

namespace protoPython {
namespace library {

/**
 * @brief warn(message, category=None, stacklevel=1, source=None)
 */
static const proto::ProtoObject* py_warnings_warn(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;

    if (!positionalParameters || positionalParameters->getSize(context) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseTypeError(context, "warn() takes at least 1 positional argument");
        return nullptr;
    }

    const proto::ProtoObject* message = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* category = (positionalParameters->getSize(context) > 1) ? positionalParameters->getAt(context, 1) : nullptr;
    
    std::string msgStr = "<unknown warning message>";
    if (message && message->isString(context)) {
        message->asString(context)->toUTF8String(context, msgStr);
    } else if (message) {
        // Handle custom exception instances as warnings by extracting their string representation
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoString* strName = env ? env->getStrString() : proto::ProtoString::createSymbol(context, "__str__");
        const proto::ProtoObject* strMethod = env ? env->getAttribute(context, message, strName) : message->getAttribute(context, strName);
        if (strMethod && strMethod->asMethod(context)) {
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
            const proto::ProtoObject* sObj = strMethod->asMethod(context)(context, message, nullptr, emptyL, nullptr);
            if (sObj && sObj->isString(context)) {
                sObj->asString(context)->toUTF8String(context, msgStr);
            }
        }
    }

    std::string catStr = "Warning";
    if (category && category != PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* nameAttr = env ? env->getAttribute(context, category, env->getNameString()) : category->getAttribute(context, proto::ProtoString::createSymbol(context, "__name__"));
        if (nameAttr && nameAttr->isString(context)) {
            nameAttr->asString(context)->toUTF8String(context, catStr);
        }
    }

    

    return PROTO_NONE;
}

static const proto::ProtoObject* py_warnings_filters_mutated_lock_held(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    
    // Return a dummy object that acts as a context manager
    proto::ProtoObject* lock = const_cast<proto::ProtoObject*>(context->newObject(true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    
    auto dummyFunc = [](proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        return PROTO_NONE;
    };
    
    const proto::ProtoString* enterS = env ? env->getEnterString() : PythonEnvironment::getInternedString(context, "__enter__");
    const proto::ProtoString* exitS = env ? env->getExitString() : PythonEnvironment::getInternedString(context, "__exit__");
    
    lock->setAttribute(context, enterS, context->fromMethod(lock, dummyFunc));
    lock->setAttribute(context, exitS, context->fromMethod(lock, dummyFunc));
    
    return lock;
}

const proto::ProtoObject* WarningsModule::createWarningsModule(proto::ProtoContext* context) {
    proto::ProtoObject* moduleObj = const_cast<proto::ProtoObject*>(context->newObject(true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    
    if (env && env->getModulePrototype()) {
        moduleObj = const_cast<proto::ProtoObject*>(moduleObj->addParent(context, env->getModulePrototype()));
        moduleObj->setAttribute(context, env->getClassString(), env->getModulePrototype());
    } else {
        moduleObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__class__"), proto::ProtoString::createSymbol(context, "module")->asObject(context));
    }

    const proto::ProtoString* nameS = env ? env->getNameString() : proto::ProtoString::createSymbol(context, "__name__");
    moduleObj->setAttribute(context, nameS, proto::ProtoString::createSymbol(context, "_warnings")->asObject(context));

    // We deliberately do NOT export `warn`/`warn_explicit` from _warnings.
    // Python's lib/warnings.py imports the _warnings versions when available
    // and overrides _py_warnings'.  Our C-level py_warnings_warn was a stub
    // that returned PROTO_NONE without invoking any showwarning hook, which
    // broke catch_warnings(record=True).  Falling through to the
    // _py_warnings.warn (which honours filters and showwarning correctly)
    // is much simpler than re-implementing the warn protocol in C++.
    (void)py_warnings_warn;

    // Expose _acquire_lock and _release_lock (dummy for single-threaded V78)
    auto dummyFunc = [](proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        return PROTO_NONE;
    };
    moduleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "_acquire_lock"), context->fromMethod(moduleObj, dummyFunc));
    moduleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "_release_lock"), context->fromMethod(moduleObj, dummyFunc));

    // Expose _filters_version (Step V77 Fix)
    const proto::ProtoString* filtersVersionS = PythonEnvironment::getInternedString(context, "_filters_version");
    const proto::ProtoList* versionList = context->newList()->appendLast(context, context->fromInteger(0));
    moduleObj->setAttribute(context, filtersVersionS, versionList->asObject(context));

    // Expose _filters_mutated_lock_held
    const proto::ProtoString* lockS = PythonEnvironment::getInternedString(context, "_filters_mutated_lock_held");
    moduleObj->setAttribute(context, lockS, context->fromMethod(moduleObj, py_warnings_filters_mutated_lock_held));

    // Expose _onceregistry
    moduleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "_onceregistry"), (context->newObject(true)));
    // Expose filters
    moduleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "filters"), (context->newList())->asObject(context));
    // Expose _warnings_defaults
    moduleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "_warnings_context"), (context->newObject(true)));
    // Expose _defaultaction
    moduleObj->setAttribute(context, PythonEnvironment::getInternedString(context, "_defaultaction"), PythonEnvironment::getInternedString(context, "default")->asObject(context));
    // warn_explicit also intentionally NOT exported (see comment on `warn`
    // above).  The Python-level _py_warnings.warn_explicit handles the full
    // protocol — filters, registry, showwarning hook — correctly.

    return moduleObj;
}

} // namespace library
} // namespace protoPython
