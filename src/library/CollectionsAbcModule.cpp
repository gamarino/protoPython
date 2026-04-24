#include <protoPython/CollectionsAbcModule.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/PythonEnvironment.h>
#include <cstring>
#include <protoCore.h>

namespace protoPython {

// Forward declaration — implemented in ExecutionEngine.cpp
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);
static const proto::ProtoObject* py_check_methods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* sparseArgs) {


    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_FALSE;
    const proto::ProtoObject* C = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* mro = C->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__mro__"));
    if (!mro) return PROTO_FALSE;
    const proto::ProtoList* mroList = mro->asList(ctx);
    if (!mroList) return PROTO_FALSE;

    for (size_t i = 1; i < posArgs->getSize(ctx); ++i) {
        const proto::ProtoObject* methodObj = posArgs->getAt(ctx, i);
        const proto::ProtoString* methodName = methodObj ? methodObj->asString(ctx) : nullptr;
        if (!methodName) continue;

        bool found = false;
        for (size_t j = 0; j < mroList->getSize(ctx); ++j) {
            const proto::ProtoObject* base = mroList->getAt(ctx, j);
            const proto::ProtoObject* dict = base->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__dict__"));
            if (dict && dict->hasAttribute(ctx, methodName)) {
                found = true;
                break;
            }
        }
        if (!found) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}




namespace collections_abc {

// Helper: iterate a mapping (using __iter__ + __getitem__) and collect (key, value) tuples.
// Returns a ProtoList of 2-element tuples, or nullptr on error.
static const proto::ProtoList* collectMappingItems(proto::ProtoContext* ctx, const proto::ProtoObject* mapping) {
    if (!mapping || mapping == PROTO_NONE) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* emptyL = ctx->newList();

    auto callMethod = [&](const proto::ProtoObject* method, const proto::ProtoObject* receiver,
                          const proto::ProtoList* args) -> const proto::ProtoObject* {
        if (!method || method == PROTO_NONE) return nullptr;
        if (method->asMethod(ctx)) {
            return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(receiver), nullptr, args, nullptr);
        }
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, receiver);
        unsigned long n = args ? args->getSize(ctx) : 0;
        for (unsigned long j = 0; j < n; ++j) selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, j));
        // Use the extern helper declared in ExecutionEngine.cpp
        return invokePythonCallable(ctx, method, selfArgs, nullptr);
    };

    const proto::ProtoString* iterS = PythonEnvironment::getInternalString(ctx, "__iter__");
    const proto::ProtoObject* iterM = mapping->getAttribute(ctx, iterS);
    if (!iterM || iterM == PROTO_NONE) return nullptr;
    const proto::ProtoObject* itObj = callMethod(iterM, mapping, emptyL);
    if (!itObj || itObj == PROTO_NONE) return nullptr;

    const proto::ProtoString* nextS = PythonEnvironment::getInternalString(ctx, "__next__");
    const proto::ProtoObject* nextM = itObj->getAttribute(ctx, nextS);
    if (!nextM || nextM == PROTO_NONE) return nullptr;

    const proto::ProtoList* result = ctx->newList();
    for (;;) {
        const proto::ProtoObject* key = callMethod(nextM, itObj, emptyL);
        if (env && env->peekPendingException()) {
            if (env->isStopIteration(ctx, env->peekPendingException()))
                env->clearPendingException();
            break;
        }
        if (!key || key == PROTO_NONE) break;
        // Get value via __getitem__
        const proto::ProtoList* getArgs = ctx->newList()->appendLast(ctx, key);
        const proto::ProtoObject* val = env ? env->getItem(mapping, key, ctx) : PROTO_NONE;
        if (env && env->peekPendingException()) { env->clearPendingException(); val = PROTO_NONE; }
        // Yield as (key, value) tuple
        const proto::ProtoList* pair = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, val ? val : PROTO_NONE);
        result = result->appendLast(ctx, ctx->newTupleFromList(pair)->asObject(ctx));
    }
    return result;
}

/** Mapping.items(): returns a list of (key, value) tuples */
static const proto::ProtoObject* py_mapping_items(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* mapping = self;
    // If self is None/null, try args[0]
    if (!mapping || mapping == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) > 0) mapping = posArgs->getAt(ctx, 0);
    }
    const proto::ProtoList* items = collectMappingItems(ctx, mapping);
    return items ? items->asObject(ctx) : ctx->newList()->asObject(ctx);
}

/** Mapping.keys(): returns a list of keys */
static const proto::ProtoObject* py_mapping_keys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* mapping = self;
    if (!mapping || mapping == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) > 0) mapping = posArgs->getAt(ctx, 0);
    }
    if (!mapping || mapping == PROTO_NONE) return ctx->newList()->asObject(ctx);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* emptyL = ctx->newList();

    auto callM = [&](const proto::ProtoObject* method, const proto::ProtoObject* recv) -> const proto::ProtoObject* {
        if (!method || method == PROTO_NONE) return nullptr;
        if (method->asMethod(ctx)) return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(recv), nullptr, emptyL, nullptr);
        const proto::ProtoList* sa = ctx->newList()->appendLast(ctx, recv);
        return invokePythonCallable(ctx, method, sa, nullptr);
    };

    const proto::ProtoString* iterS = PythonEnvironment::getInternalString(ctx, "__iter__");
    const proto::ProtoObject* iterM = mapping->getAttribute(ctx, iterS);
    const proto::ProtoObject* itObj = iterM ? callM(iterM, mapping) : nullptr;
    if (!itObj || itObj == PROTO_NONE) return ctx->newList()->asObject(ctx);

    const proto::ProtoString* nextS = PythonEnvironment::getInternalString(ctx, "__next__");
    const proto::ProtoObject* nextM = itObj->getAttribute(ctx, nextS);
    if (!nextM || nextM == PROTO_NONE) return ctx->newList()->asObject(ctx);

    const proto::ProtoList* result = ctx->newList();
    for (;;) {
        const proto::ProtoObject* key = callM(nextM, itObj);
        if (env && env->peekPendingException()) {
            if (env->isStopIteration(ctx, env->peekPendingException())) env->clearPendingException();
            break;
        }
        if (!key || key == PROTO_NONE) break;
        result = result->appendLast(ctx, key);
    }
    return result->asObject(ctx);
}

/** Mapping.values(): returns a list of values */
static const proto::ProtoObject* py_mapping_values(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* mapping = self;
    if (!mapping || mapping == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) > 0) mapping = posArgs->getAt(ctx, 0);
    }
    const proto::ProtoList* items = collectMappingItems(ctx, mapping);
    if (!items) return ctx->newList()->asObject(ctx);
    const proto::ProtoList* result = ctx->newList();
    for (unsigned long i = 0; i < items->getSize(ctx); ++i) {
        const proto::ProtoObject* pair = items->getAt(ctx, i);
        if (pair && pair->isTuple(ctx) && pair->asTuple(ctx)->getSize(ctx) >= 2)
            result = result->appendLast(ctx, pair->asTuple(ctx)->getAt(ctx, 1));
    }
    return result->asObject(ctx);
}

/** Minimal __call__ for ABC stub: return new child (for isinstance/callable use). */
static const proto::ProtoObject* py_abc_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return self->newChild(ctx, true);
}

/** Minimal register for ABC stub: just return the argument. */
static const proto::ProtoObject* py_abc_register(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return self;
    return posArgs->getAt(ctx, 0);
}

/** _check_methods(C, *methods) implementation for internal use by os.py/io.py etc. */
static const proto::ProtoObject* py_abc_check_methods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    
    if (args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* C = args->getAt(ctx, 0);

    // Get NotImplemented singleton
    const proto::ProtoObject* notImplemented = PROTO_NONE;
    if (auto* env = PythonEnvironment::fromContext(ctx)) {
        notImplemented = env->resolve("NotImplemented");
    }
    if (!notImplemented) notImplemented = PROTO_NONE;

    const proto::ProtoString* mroName = proto::ProtoString::createSymbol(ctx, "__mro__");
    const proto::ProtoObject* mroObj = C->getAttribute(ctx, mroName);
    if (!mroObj || !mroObj->asList(ctx)) return notImplemented;

    const proto::ProtoList* mro = mroObj->asList(ctx);

    // Loop through methods (args 1..N)
    for (unsigned long i = 1; i < args->getSize(ctx); ++i) {
        const proto::ProtoObject* methodObj = args->getAt(ctx, i);
        if (!methodObj->isString(ctx)) continue;
        
        unsigned long methodHash = methodObj->getHash(ctx);
        bool found = false;

        for (unsigned long j = 0; j < mro->getSize(ctx); ++j) {
            const proto::ProtoObject* B = mro->getAt(ctx, j);
            const proto::ProtoSparseList* attrs = B->getOwnAttributes(ctx);
            // fprintf(stderr, "  Checking class %lu in MRO\n", j);
            
            if (attrs && attrs->has(ctx, methodHash)) {
                // fprintf(stderr, "  Found in class %lu\n", j);
                const proto::ProtoObject* val = attrs->getAt(ctx, methodHash);
                if (val == PROTO_NONE) return notImplemented; 
                found = true;
                break;
            }
        }
        if (!found) return notImplemented;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_abc_get(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    const proto::ProtoObject* def = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return def;

    const proto::ProtoObject* val = env->getItem(self, key, ctx);
    if (val && val != PROTO_NONE) return val;
    if (val == PROTO_NONE) return val; // If found and is None, return None
    
    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        const proto::ProtoObject* keyError = env->lookupName("KeyError");
        if (keyError && env->isException(exc, keyError)) {
            env->clearPendingException();
            return def;
        }
    }
    return def;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    auto createAbc = [&](const char* name) {
        proto::ProtoObject* abc = const_cast<proto::ProtoObject*>(ctx->newObject(false));
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        
        const proto::ProtoString* s_call = env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__");
        const proto::ProtoString* s_name = env ? env->getNameString() : PythonEnvironment::getInternedString(ctx, "__name__");
        const proto::ProtoString* s_class = env ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__");
        const proto::ProtoString* s_register = PythonEnvironment::getInternedString(ctx, "register");
        const proto::ProtoString* s_is_py_class = PythonEnvironment::getInternedString(ctx, "__is_python_class__");
        const proto::ProtoString* s_bases = env ? env->getBasesString() : PythonEnvironment::getInternedString(ctx, "__bases__");

        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_call, ctx->fromMethod(nullptr, py_abc_call)));
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_register, ctx->fromMethod(nullptr, py_abc_register)));
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_name, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx)));
        
        // Set __class__ to type Prototype
        if (env) {
            abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_class, env->getTypePrototype()));
        }

        // Explicitly mark as a Python class for getType heuristic
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_is_py_class, PROTO_TRUE));

        // 1. Add dummy methods to satisfy collections/__init__.py inheritance of methods
        const char* methods[] = {
            "update", "get", "keys", "values", "items", "pop", "popitem", "clear",
            "setdefault", "index", "count", "append", "extend", "insert", "remove", "reverse",
            "__eq__", "__ne__", "__lt__", "__gt__", "__ge__",
            "__iter__", "__len__", "__contains__", "__hash__"
        };
        bool isMapping = (strcmp(name, "Mapping") == 0 || strcmp(name, "MutableMapping") == 0);
        for (const char* m : methods) {
            const proto::ProtoObject* impl = ctx->fromMethod(nullptr, py_abc_call);
            if (isMapping && strcmp(m, "get") == 0) impl = ctx->fromMethod(nullptr, py_abc_get);
            else if (isMapping && strcmp(m, "items") == 0) impl = ctx->fromMethod(nullptr, py_mapping_items);
            else if (isMapping && strcmp(m, "keys") == 0) impl = ctx->fromMethod(nullptr, py_mapping_keys);
            else if (isMapping && strcmp(m, "values") == 0) impl = ctx->fromMethod(nullptr, py_mapping_values);
            abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, m), impl));
        }

        // 2. Inherit from object to have a valid MRO
        if (env) {
            const proto::ProtoObject* objProto = env->getObjectPrototype();
            if (objProto) {
                abc = const_cast<proto::ProtoObject*>(abc->addParent(ctx, objProto));

                // Set __bases__ to (object,)
                const proto::ProtoList* bases = ctx->newList();
                bases = bases->appendLast(ctx, objProto);
                abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_bases, env->newTuple(bases)));

                // Set __mro__ = (abc, object) so subclass MRO computation includes this ABC.
                const proto::ProtoList* mroList = ctx->newList();
                mroList = mroList->appendLast(ctx, abc);
                mroList = mroList->appendLast(ctx, objProto);
                abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx,
                    PythonEnvironment::getInternedString(ctx, "__mro__"),
                    ctx->newTupleFromList(mroList)->asObject(ctx)));
            }
        }

        return abc;
    };

    const proto::ProtoObject* mod = ctx->newObject(false);
    const char* names[] = {
        "Hashable", "Iterable", "Iterator", "Reversible", "Sized", "Container", "Collection",
        "Mapping", "MutableMapping", "Sequence", "MutableSequence",
        "Set", "MutableSet", "Callable", "Awaitable", "Coroutine",
        "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Generator",
        "KeysView", "ValuesView", "ItemsView", "MappingView", "ByteString"
    };

    for (const char* name : names) {
        const proto::ProtoString* sName = PythonEnvironment::getInternedString(ctx, name);
        mod = mod->setAttribute(ctx, sName, createAbc(name));
    }

    const proto::ProtoObject* checkMethodsFunc = ctx->fromMethod(nullptr, py_check_methods);
    const proto::ProtoString* sCheck = PythonEnvironment::getInternedString(ctx, "_check_methods");
    mod = mod->setAttribute(ctx, sCheck, checkMethodsFunc);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "check_methods"), checkMethodsFunc);

    const proto::ProtoObject* abcMeta = createAbc("ABCMeta");
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "ABCMeta"), abcMeta);

    static const auto py_abstractmethod = [](proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
        if (args->getSize(nullptr) < 1) return PROTO_NONE;
        return args->getAt(nullptr, 0);
    };
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "abstractmethod"), ctx->fromMethod(nullptr, py_abstractmethod));

    const proto::ProtoList* allList = ctx->newList();
    for (const char* name : names) {
        allList = allList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    }
    allList = allList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "_check_methods")->asObject(ctx));
    allList = allList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "ABCMeta")->asObject(ctx));
    const proto::ProtoObject* allObj = env ? env->newList(allList) : allList->asObject(ctx);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__all__"), allObj);

    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_collections_abc")->asObject(ctx));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__loader__"), PROTO_NONE); 
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__executed__"), PROTO_TRUE); 

    return mod;
}

} // namespace collections_abc
} // namespace protoPython
