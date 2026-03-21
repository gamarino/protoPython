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
        const proto::ProtoString* strName = env ? env->getStrString() : proto::ProtoString::fromUTF8String(context, "__str__");
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
        const proto::ProtoObject* nameAttr = env ? env->getAttribute(context, category, env->getNameString()) : category->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
        if (nameAttr && nameAttr->isString(context)) {
            nameAttr->asString(context)->toUTF8String(context, catStr);
        }
    }

    

    return PROTO_NONE;
}

const proto::ProtoObject* WarningsModule::createWarningsModule(proto::ProtoContext* context) {
    proto::ProtoObject* moduleObj = const_cast<proto::ProtoObject*>(context->newObject(true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    
    if (env && env->getModulePrototype()) {
        moduleObj = const_cast<proto::ProtoObject*>(moduleObj->addParent(context, env->getModulePrototype()));
        moduleObj->setAttribute(context, env->getClassString(), env->getModulePrototype());
    } else {
        moduleObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), proto::ProtoString::fromUTF8String(context, "module")->asObject(context));
    }

    const proto::ProtoString* nameS = env ? env->getNameString() : proto::ProtoString::fromUTF8String(context, "__name__");
    moduleObj->setAttribute(context, nameS, proto::ProtoString::fromUTF8String(context, "_warnings")->asObject(context));

    // Expose warn function
    const proto::ProtoString* warnS = proto::ProtoString::fromUTF8String(context, "warn");
    moduleObj->setAttribute(context, warnS, context->fromMethod(moduleObj, py_warnings_warn));

    return moduleObj;
}

} // namespace library
} // namespace protoPython
