#include <protoPython/MarshalModule.h>
#include <protoPython/PythonEnvironment.h>
#include <iostream>

namespace protoPython {
namespace library {

static const proto::ProtoObject* py_marshal_loads(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        env->raiseValueError(context, PythonEnvironment::getInternedString(context, "marshal.loads not implemented in ProtoPython")->asObject(context));
    }
    return nullptr;
}

static const proto::ProtoObject* py_marshal_dumps(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        env->raiseValueError(context, PythonEnvironment::getInternedString(context, "marshal.dumps not implemented in ProtoPython")->asObject(context));
    }
    return nullptr;
}

const proto::ProtoObject* MarshalModule::createMarshalModule(proto::ProtoContext* context) {
    proto::ProtoObject* moduleObj = const_cast<proto::ProtoObject*>(context->newObject(true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    
    if (env && env->getModulePrototype()) {
        moduleObj = const_cast<proto::ProtoObject*>(moduleObj->addParent(context, env->getModulePrototype()));
        moduleObj->setAttribute(context, env->getClassString(), env->getModulePrototype());
    } else {
        moduleObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__class__"), proto::ProtoString::createSymbol(context, "module")->asObject(context));
    }

    const proto::ProtoString* nameS = env ? env->getNameString() : proto::ProtoString::createSymbol(context, "__name__");
    moduleObj->setAttribute(context, nameS, proto::ProtoString::createSymbol(context, "marshal")->asObject(context));

    // Expose functions
    moduleObj->setAttribute(context, proto::ProtoString::createSymbol(context, "loads"), context->fromMethod(moduleObj, py_marshal_loads));
    moduleObj->setAttribute(context, proto::ProtoString::createSymbol(context, "dumps"), context->fromMethod(moduleObj, py_marshal_dumps));

    return moduleObj;
}

} // namespace library
} // namespace protoPython
