#include <protoPython/PythonEnvironment.h>
#include <protoPython/PythonModuleProvider.h>
#include <protoPython/NativeModuleProvider.h>
#include <protoPython/SysModule.h>
#include <protoPython/BuiltinsModule.h>
#include <protoCore.h>

namespace protoPython {

// --- Dunder Methods Implementation ---

static const proto::ProtoObject* py_object_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_object_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // Basic <object at 0x...> repr
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "<object at %p>", (void*)self);
    return context->fromUTF8String(buffer);
}

static const proto::ProtoObject* py_object_str(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // Default str(obj) calls repr(obj)
    return self->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__repr__"), self, nullptr);
}

// --- List Methods ---

static const proto::ProtoObject* py_list_append(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);

    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* item = positionalParameters->getAt(context, 0);
        const proto::ProtoList* newList = list->appendLast(context, item);
        self->setAttribute(context, dataName, newList->asObject(context));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return context->fromInteger(0);
    return context->fromInteger(data->asList(context)->getSize(context));
}

// --- Dict Methods ---

static const proto::ProtoObject* py_dict_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return PROTO_NONE;

    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
        unsigned long hash = key->getHash(context);
        return data->asSparseList(context)->getAt(context, hash);
    }
    return PROTO_NONE;
}

// --- PythonEnvironment Implementation ---

PythonEnvironment::PythonEnvironment(const std::string& stdLibPath) : space() {
    context = new proto::ProtoContext(&space);
    initializeRootObjects(stdLibPath);
}

PythonEnvironment::~PythonEnvironment() {
    delete context;
}

void PythonEnvironment::initializeRootObjects(const std::string& stdLibPath) {
    const proto::ProtoString* py_init = proto::ProtoString::fromUTF8String(context, "__init__");
    const proto::ProtoString* py_repr = proto::ProtoString::fromUTF8String(context, "__repr__");
    const proto::ProtoString* py_str = proto::ProtoString::fromUTF8String(context, "__str__");
    const proto::ProtoString* py_class = proto::ProtoString::fromUTF8String(context, "__class__");
    const proto::ProtoString* py_name = proto::ProtoString::fromUTF8String(context, "__name__");
    const proto::ProtoString* py_append = proto::ProtoString::fromUTF8String(context, "append");
    const proto::ProtoString* py_getitem = proto::ProtoString::fromUTF8String(context, "__getitem__");
    const proto::ProtoString* py_len = proto::ProtoString::fromUTF8String(context, "__len__");

    // 1. Create 'object' base
    objectPrototype = context->newObject(true);
    objectPrototype = objectPrototype->setAttribute(context, py_init, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_init));
    objectPrototype = objectPrototype->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_repr));
    objectPrototype = objectPrototype->setAttribute(context, py_str, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_str));

    // 2. Create 'type'
    typePrototype = context->newObject(true);
    typePrototype = typePrototype->addParent(context, objectPrototype);
    typePrototype = typePrototype->setAttribute(context, py_class, typePrototype);

    // 3. Circularity: object's class is type
    objectPrototype = objectPrototype->setAttribute(context, py_class, typePrototype);

    // 4. Basic types
    intPrototype = context->newObject(true);
    intPrototype = intPrototype->addParent(context, objectPrototype);
    intPrototype = intPrototype->setAttribute(context, py_class, typePrototype);
    intPrototype = intPrototype->setAttribute(context, py_name, context->fromUTF8String("int"));

    strPrototype = context->newObject(true);
    strPrototype = strPrototype->addParent(context, objectPrototype);
    strPrototype = strPrototype->setAttribute(context, py_class, typePrototype);
    strPrototype = strPrototype->setAttribute(context, py_name, context->fromUTF8String("str"));

    listPrototype = context->newObject(true);
    listPrototype = listPrototype->addParent(context, objectPrototype);
    listPrototype = listPrototype->setAttribute(context, py_class, typePrototype);
    listPrototype = listPrototype->setAttribute(context, py_name, context->fromUTF8String("list"));
    listPrototype = listPrototype->setAttribute(context, py_append, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_append));
    listPrototype = listPrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_len));

    dictPrototype = context->newObject(true);
    dictPrototype = dictPrototype->addParent(context, objectPrototype);
    dictPrototype = dictPrototype->setAttribute(context, py_class, typePrototype);
    dictPrototype = dictPrototype->setAttribute(context, py_name, context->fromUTF8String("dict"));
    dictPrototype = dictPrototype->setAttribute(context, py_getitem, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_getitem));

    // 5. Initialize Native Module Provider
    auto& registry = proto::ProviderRegistry::instance();
    auto nativeProvider = std::make_unique<NativeModuleProvider>();

    // sys module
    sysModule = sys::initialize(context);
    nativeProvider->registerModule("sys", [this](proto::ProtoContext* ctx) { return sysModule; });

    // builtins module
    builtinsModule = builtins::initialize(context, objectPrototype, typePrototype, intPrototype, strPrototype, listPrototype, dictPrototype);
    nativeProvider->registerModule("builtins", [this](proto::ProtoContext* ctx) { return builtinsModule; });

    registry.registerProvider(std::move(nativeProvider));

    // 6. Initialize StdLib Module Provider
    // Use provided path or default to ./lib/python3.14
    std::string path = stdLibPath.empty() ? "./lib/python3.14" : stdLibPath;
    registry.registerProvider(std::make_unique<PythonModuleProvider>(path));

    // Prepend to resolution chain
    const proto::ProtoObject* chainObj = context->space->getResolutionChain();
    if (chainObj) {
        const proto::ProtoList* chain = chainObj->asList(context);
        if (chain) {
            chain = chain->insertAt(context, 0, context->fromUTF8String("provider:python_stdlib"));
            chain = chain->insertAt(context, 0, context->fromUTF8String("provider:native"));
            context->space->setResolutionChain(chain->asObject(context));
        }
    }
}

const proto::ProtoObject* PythonEnvironment::resolve(const std::string& name) {
    // 1. Check builtins
    if (builtinsModule) {
        const proto::ProtoObject* attr = builtinsModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, name.c_str()));
        if (attr && attr != PROTO_NONE) return attr;
    }

    // 2. Direct resolution fallback for hardcoded prototypes (already in builtins, but keep for robustness)
    if (name == "object") return objectPrototype;
    if (name == "type") return typePrototype;
    if (name == "int") return intPrototype;
    if (name == "str") return strPrototype;
    if (name == "list") return listPrototype;
    if (name == "dict") return dictPrototype;
    
    // Fallback: try to import as a module
    const proto::ProtoObject* modWrapper = context->space->getImportModule(name.c_str(), "val");
    if (modWrapper) {
        return modWrapper->getAttribute(context, proto::ProtoString::fromUTF8String(context, "val"));
    }

    return PROTO_NONE;
}

} // namespace protoPython
