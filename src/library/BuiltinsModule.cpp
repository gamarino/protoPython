#include <protoPython/BuiltinsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/Parser.h>
#include <protoPython/Compiler.h>
#include <protoPython/Tokenizer.h>
#include <protoCore.h>
#include <proto_internal.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>

namespace protoPython {
namespace builtins {

static bool get_env_diag() {
    static bool diag = std::getenv("PROTO_ENV_DIAG") != nullptr;
    return diag;
}

namespace {
struct GlobalsScope {
    GlobalsScope(const proto::ProtoObject* g) : old(PythonEnvironment::getCurrentGlobals()) {
        PythonEnvironment::setCurrentGlobals(g);
        PythonEnvironment* env = PythonEnvironment::getCurrentEnvironment();
        if (env) env->invalidateResolveCache();
    }
    ~GlobalsScope() {
        PythonEnvironment::setCurrentGlobals(old);
        PythonEnvironment* env = PythonEnvironment::getCurrentEnvironment();
        if (env) env->invalidateResolveCache();
    }
    const proto::ProtoObject* old;
};
}
using protoPython::PythonEnvironment;

static const proto::ProtoObject* py_import(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_vars(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_globals(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

const proto::ProtoObject* py_object_new(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

const proto::ProtoObject* py_type_prepare(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* dictObj = context->newObject(true);
    if (env && env->getDictPrototype()) {
        dictObj = dictObj->addParent(context, env->getDictPrototype());
    }
    // Initialize __data__ and __keys__ if needed
    if (env) env->initDictStorage(context, dictObj);
    
    // Explicitly mark this dict as intended for a Python class namespace
    dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__is_python_class__"), PROTO_TRUE));
    
    return dictObj;
}
static const proto::ProtoObject* py_import(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    size_t argCount = positionalParameters->getSize(context);
    if (argCount == 0) return PROTO_NONE;
    
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 0);
    std::string moduleName;
    if (nameObj && nameObj->isString(context)) {
        nameObj->asString(context)->toUTF8String(context, moduleName);
    }
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: __import__('%s') level=%d\n", moduleName.c_str(), (int)((argCount >= 5) ? (positionalParameters->getAt(context, 4)->isInteger(context) ? positionalParameters->getAt(context, 4)->asLong(context) : 0) : 0));
        fflush(stderr);
    }

    const proto::ProtoObject* globals = (argCount >= 2) ? positionalParameters->getAt(context, 1) : PROTO_NONE;
    const proto::ProtoObject* fromListObj = (argCount >= 4) ? positionalParameters->getAt(context, 3) : PROTO_NONE;
    int level = 0;
    if (argCount >= 5) {
        const proto::ProtoObject* levelObj = positionalParameters->getAt(context, 4);
        if (levelObj->isInteger(context)) level = (int)levelObj->asLong(context);
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;

    if (level > 0) {
        if (globals == PROTO_NONE) globals = env->getGlobals();
        if (globals && globals != PROTO_NONE) {
            const proto::ProtoObject* pkgObj = globals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__package__"));
            if (!pkgObj || pkgObj == PROTO_NONE) {
                pkgObj = globals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
                // If __name__ is not a package, we might need to go up one level.
                // For simplicity, if it doesn't have __path__, it's a module.
                if (pkgObj && pkgObj != PROTO_NONE && globals->hasAttribute(context, proto::ProtoString::fromUTF8String(context, "__path__")) == PROTO_FALSE) {
                    std::string name;
                    pkgObj->asString(context)->toUTF8String(context, name);
                    size_t lastDot = name.find_last_of('.');
                    if (lastDot != std::string::npos) {
                        pkgObj = context->fromUTF8String(name.substr(0, lastDot).c_str());
                    } else {
                        pkgObj = context->fromUTF8String("");
                    }
                }
            }
            
            if (pkgObj && pkgObj->isString(context)) {
                std::string base;
                pkgObj->asString(context)->toUTF8String(context, base);
                
                // Handle level: 1 = current package, 2 = parent, etc.
                for (int i = 1; i < level; ++i) {
                    size_t lastDot = base.find_last_of('.');
                    if (lastDot == std::string::npos) {
                        base = "";
                        break;
                    }
                    base = base.substr(0, lastDot);
                }
                
                if (moduleName.empty()) {
                    moduleName = base;
                } else if (!base.empty()) {
                    moduleName = base + "." + moduleName;
                }
            }
        }
    }

    const proto::ProtoObject* leaf = env->resolveModule(moduleName, context);
    
    if (!leaf || leaf == PROTO_NONE) {
        if (env->hasPendingException()) return nullptr;
        env->raiseImportError("No module named '" + moduleName + "'");
        return nullptr;
    }

    if (fromListObj && fromListObj->asList(context) && leaf != PROTO_NONE) {
        const proto::ProtoList* fromList = fromListObj->asList(context);
        unsigned long fromSize = fromList->getSize(context);
        for (unsigned long i = 0; i < fromSize; ++i) {
            const proto::ProtoObject* itemObj = fromList->getAt(context, i);
            if (itemObj && itemObj->isString(context)) {
                std::string itemName;
                itemObj->asString(context)->toUTF8String(context, itemName);
                if (itemName == "*") continue;
                // Only try to resolve as a submodule if the parent module is a package (has __path__)
                // and the attribute is not already present.
                if (leaf->hasAttribute(context, itemObj->asString(context)) == PROTO_FALSE) {
                    const proto::ProtoObject* pathAttr = leaf->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__path__"));
                    if (pathAttr && pathAttr != PROTO_NONE) {
                        std::string subModuleName = moduleName + "." + itemName;
                        env->resolve(subModuleName, context);
                    }
                }
            }
        }
    }
    bool returnLeaf = (argCount > 1 && positionalParameters->getAt(context, 1) == PROTO_TRUE);
    if (!returnLeaf && fromListObj && fromListObj != PROTO_NONE && fromListObj->asList(context) && fromListObj->asList(context)->getSize(context) > 0) {
        returnLeaf = true;
    }

    if (!returnLeaf && moduleName.find('.') != std::string::npos) {
        size_t dot = moduleName.find('.');
        std::string topLevel = moduleName.substr(0, dot);
        return env->resolve(topLevel, context);
    }
    return leaf;
}




static const proto::ProtoObject* py_gc_collect(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    return ctx->fromInteger(0);
}

static const proto::ProtoObject* py_gc_isenabled(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_id(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) == 0) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    return context->fromInteger((long long)(uintptr_t)obj);
}

static const proto::ProtoObject* py_complete(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return context->newList()->asObject(context);
    
    std::string prefix;
    if (positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* pObj = positionalParameters->getAt(context, 0);
        if (pObj && pObj->isString(context)) pObj->asString(context)->toUTF8String(context, prefix);
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoList* results = context->newList();
    std::unordered_set<std::string> uniqueResults;

    auto collectFrom = [&](const proto::ProtoObject* obj) {
        if (!obj || obj == PROTO_NONE) return;
        
        // If it's a dictionary-like object (like globals()), it might have a __keys__ list.
        const proto::ProtoString* keysName = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(context, "__keys__");
        const proto::ProtoObject* keysObj = obj->getAttribute(context, keysName);
        if (keysObj && keysObj->asList(context)) {
            const proto::ProtoList* keysList = keysObj->asList(context);
            const proto::ProtoListIterator* it = keysList->getIterator(context);
            while (it && it->hasNext(context)) {
                const proto::ProtoObject* keyObj = it->next(context);
                if (keyObj && keyObj->isString(context)) {
                    std::string name;
                    keyObj->asString(context)->toUTF8String(context, name);
                    if (name.compare(0, prefix.size(), prefix) == 0) {
                        if (uniqueResults.insert(name).second) {
                            results = results->appendLast(context, keyObj);
                        }
                    }
                }
                it = it->advance(context);
            }
        }

        const proto::ProtoSparseList* attrs = obj->getOwnAttributes(context);
        if (attrs) {
            auto* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(context));
            while (it && it->hasNext(context)) {
                unsigned long key = it->nextKey(context);
                const proto::ProtoString* s = reinterpret_cast<const proto::ProtoObject*>(key)->asString(context);
                std::string name;
                if (s) s->toUTF8String(context, name);
                if (name.compare(0, prefix.size(), prefix) == 0) {
                    if (uniqueResults.insert(name).second) {
                        results = results->appendLast(context, context->fromUTF8String(name.c_str()));
                    }
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it->advance(context));
            }
        }
    };

    // For now, simple global/builtin completion.
    const proto::ProtoObject* target = nullptr;
    if (positionalParameters->getSize(context) >= 2) {
        target = positionalParameters->getAt(context, 1);
    }
    
    if (target && target != PROTO_NONE) {
        collectFrom(target);
    } else {
        const proto::ProtoObject* globals = PythonEnvironment::getCurrentGlobals();
        collectFrom(globals);
    }

    if (env) collectFrom(env->getBuiltins());

    // Wrap in a Python list object
    const proto::ProtoObject* listObj = context->newObject(false);
    if (env && env->getListPrototype()) {
        listObj = listObj->addParent(context, env->getListPrototype());
    }
    const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
    listObj = listObj->setAttribute(context, dataName, results->asObject(context));

    return listObj;
}

static const proto::ProtoObject* py_locals(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);


static const proto::ProtoObject* py_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    
    // Try calling __len__ first if it exists
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* lenStr = env ? env->getLenString() : proto::ProtoString::fromUTF8String(context, "__len__");
    const proto::ProtoObject* lenMethod = obj->getAttribute(context, lenStr);
    if (lenMethod && lenMethod->asMethod(context)) {
        const proto::ProtoList* emptyArgs = env ? env->getEmptyList() : context->newList();
        const proto::ProtoObject* res = lenMethod->asMethod(context)(context, obj, nullptr, emptyArgs, nullptr);
        if (res && res != PROTO_NONE && proto::isInteger(res)) {
            return res;
        }
    }
    
    // Otherwise fallback to native types
    if (obj->asList(context)) return context->fromInteger(obj->asList(context)->getSize(context));
    if (obj->asTuple(context)) return context->fromInteger(obj->asTuple(context)->getSize(context));
    if (obj->asSet(context)) return context->fromInteger(obj->asSet(context)->getSize(context));
    if (obj->asSparseList(context)) return context->fromInteger(obj->asSparseList(context)->getSize(context));
    if (obj->isString(context)) return context->fromInteger(obj->asString(context)->getSize(context));

    if (env) env->raiseTypeError(context, "'" + PythonEnvironment::reprObject(context, obj) + "' has no len()");
    return context->fromInteger(0);
}


static const proto::ProtoObject* py_print(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: py_print called with %lu args\n", positionalParameters->getSize(context));
    }
    (void)keywordParameters;
    std::string sep = " ";
    std::string end = "\n";

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* strS = env ? env->getStrString() : proto::ProtoString::fromUTF8String(context, "__str__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    unsigned long size = positionalParameters->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* obj = positionalParameters->getAt(context, static_cast<int>(i));

        const proto::ProtoObject* strObj = PROTO_NONE;
        if (!obj || obj == PROTO_NONE || (env && obj == env->getNonePrototype())) {
            strObj = context->fromUTF8String("None");
        } else if (obj->isInteger(context)) {
            strObj = context->fromUTF8String(std::to_string(obj->asLong(context)).c_str());
        } else if (obj->isDouble(context)) {
            strObj = context->fromUTF8String(std::to_string(obj->asDouble(context)).c_str());
        } else if (obj->isString(context)) {
            strObj = obj;
        } else if (obj == PROTO_TRUE) {
            strObj = context->fromUTF8String("True");
        } else if (obj == PROTO_FALSE) {
            strObj = context->fromUTF8String("False");
        } else {
            const proto::ProtoString* strS = env ? env->getStrString() : proto::ProtoString::fromUTF8String(context, "__str__");
            const proto::ProtoString* reprS = env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__");
            
            // Try __str__ first, then __repr__
            const proto::ProtoObject* strMethod = env ? env->getAttribute(context, obj, strS) : obj->getAttribute(context, strS);
            if (!strMethod || strMethod == PROTO_NONE) {
                strMethod = env ? env->getAttribute(context, obj, reprS) : obj->getAttribute(context, reprS);
            }

            if (strMethod && strMethod != PROTO_NONE && env) {
                // If we have an environment, use callObject which handles descriptors and bound methods correctly
                strObj = env->callObject(strMethod, {});
            } else if (strMethod && strMethod != PROTO_NONE) {
                // Fallback for minimal runtime
                strObj = obj->call(context, nullptr, strS, obj, emptyL, nullptr);
            }
        }

        if (strObj && strObj->isString(context)) {
            std::string out;
            strObj->asString(context)->toUTF8String(context, out);
            std::cout << out;
        } else {
            std::cout << "<unprintable>";
        }

        if (i < size - 1) std::cout << sep;
    }
    std::cout << end << std::flush;
    return env ? env->getNonePrototype() : PROTO_NONE;
}

static const proto::ProtoObject* py_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    if (env) {
        const proto::ProtoObject* it = env->iter(obj);
        if (it) return it;
    } else {
        const proto::ProtoString* iterS = proto::ProtoString::fromUTF8String(context, "__iter__");
        const proto::ProtoObject* iterMethod = obj->getAttribute(context, iterS);
        if (iterMethod && iterMethod->asMethod(context)) {
            return iterMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* defaultVal = positionalParameters->getSize(context) >= 2
        ? positionalParameters->getAt(context, 1) : nullptr;

    if (obj->asStringIterator(context)) {
        proto::ProtoStringIterator* it = const_cast<proto::ProtoStringIterator*>(obj->asStringIterator(context));
        if (!it || !it->hasNext(context)) return defaultVal ? defaultVal : PROTO_NONE;
        return it->next(context);
    }

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* nextMethod = obj->getAttribute(context, nextS);
    if (nextMethod && nextMethod->asMethod(context)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
        const proto::ProtoObject* result = nextMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
        if (!result) {
            if (env && env->hasPendingException()) return nullptr;
            if (defaultVal) return defaultVal;
            if (env) env->raiseStopIteration(context);
            return nullptr;
        }
        if (result == (env ? env->getNonePrototype() : nullptr)) {
            // In Python, __next__ returning None is NOT StopIteration.
             // But we should check if an exception was raised and returned None (unlikely but possible)
            if (env && env->hasPendingException()) return nullptr;
        }
        return result;
    }

    if (env) {
        std::string typeName = "object";
        const proto::ProtoString* classS = env->getClassString();
        const proto::ProtoString* nameS = env->getNameString();
        const proto::ProtoObject* cls = obj->getAttribute(context, classS);
        if (cls) {
            const proto::ProtoObject* nameAttr = cls->getAttribute(context, nameS);
            if (nameAttr && nameAttr->isString(context)) nameAttr->asString(context)->toUTF8String(context, typeName);
        }
        env->raiseTypeError(context, "'" + typeName + "' object is not an iterator");
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* item = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* container = positionalParameters->getAt(context, 1);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);

    if (container->isString(context)) {
        const proto::ProtoString* str = container->asString(context);
        unsigned long size = str->getSize(context);
        if (!item->isString(context)) return PROTO_FALSE;
        std::string sub;
        item->asString(context)->toUTF8String(context, sub);
        if (sub.empty()) return PROTO_TRUE;
        std::string haystack;
        str->toUTF8String(context, haystack);
        return haystack.find(sub) != std::string::npos ? PROTO_TRUE : PROTO_FALSE;
    }

    const proto::ProtoString* containsS = env ? env->getContainsString() : proto::ProtoString::fromUTF8String(context, "__contains__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* containsMethod = container->getAttribute(context, containsS);
    if (containsMethod && containsMethod->asMethod(context)) {
        const proto::ProtoList* args = context->newList()->appendLast(context, item);
        return containsMethod->asMethod(context)(context, container, nullptr, args, nullptr);
    }

    // FLAT approach fallback: if not iterable via __contains__, check as attribute if item is string
    if (item->isString(context)) {
        return container->hasAttribute(context, item->asString(context)) ? PROTO_TRUE : PROTO_FALSE;
    }

    return PROTO_FALSE;
}

static const proto::ProtoObject* py_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    if (obj == PROTO_TRUE) return PROTO_TRUE;
    if (obj == PROTO_FALSE) return PROTO_FALSE;
    if (obj == PROTO_NONE || !obj) return PROTO_FALSE;

    if (obj->isString(context)) {
        return obj->asString(context)->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
    }
    if (obj->isInteger(context)) {
        return obj->asLong(context) != 0 ? PROTO_TRUE : PROTO_FALSE;
    }
    if (obj->isDouble(context)) {
        return obj->asDouble(context) != 0.0 ? PROTO_TRUE : PROTO_FALSE;
    }

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* boolS = env ? env->getBoolString() : proto::ProtoString::fromUTF8String(context, "__bool__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* cls = env ? env->getType(context, obj) : obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
    const proto::ProtoObject* boolMethod = cls ? cls->getAttribute(context, boolS) : obj->getAttribute(context, boolS);
    if (boolMethod && boolMethod->asMethod(context)) {
        return boolMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    }

    return PROTO_TRUE;
}

const proto::ProtoObject* py_complex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    double real = 0.0;
    double imag = 0.0;
    
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    size_t argCount = positionalParameters ? positionalParameters->getSize(context) : 0;
    
    if (argCount >= 1) {
        const proto::ProtoObject* rObj = positionalParameters->getAt(context, 0);
        if (rObj->isDouble(context)) real = rObj->asDouble(context);
        else if (rObj->isInteger(context)) real = (double)rObj->asLong(context);
        else if (rObj->isString(context)) {
            // Very basic string parsing for now, Python supports "1+2j"
            std::string s;
            rObj->asString(context)->toUTF8String(context, s);
            try { real = std::stod(s); } catch(...) {}
        }
    }
    if (argCount >= 2) {
        const proto::ProtoObject* iObj = positionalParameters->getAt(context, 1);
        if (iObj->isDouble(context)) imag = iObj->asDouble(context);
        else if (iObj->isInteger(context)) imag = (double)iObj->asLong(context);
    }

    // Keyword arguments "real" and "imag"
    if (keywordParameters) {
        const proto::ProtoString* realS = proto::ProtoString::fromUTF8String(context, "real");
        const proto::ProtoString* imagS = proto::ProtoString::fromUTF8String(context, "imag");
        if (keywordParameters->has(context, realS->getHash(context))) {
            const proto::ProtoObject* r = keywordParameters->getAt(context, realS->getHash(context));
            if (r->isDouble(context)) real = r->asDouble(context);
            else if (r->isInteger(context)) real = (double)r->asLong(context);
        }
        if (keywordParameters->has(context, imagS->getHash(context))) {
            const proto::ProtoObject* i = keywordParameters->getAt(context, imagS->getHash(context));
            if (i->isDouble(context)) imag = i->asDouble(context);
            else if (i->isInteger(context)) imag = (double)i->asLong(context);
        }
    }

    const proto::ProtoObject* res = context->newObject(false);
    if (env && env->getComplexPrototype()) {
        res = res->addParent(context, env->getComplexPrototype());
    }
    res = res->setAttribute(context, proto::ProtoString::fromUTF8String(context, "real"), context->fromDouble(real));
    res = res->setAttribute(context, proto::ProtoString::fromUTF8String(context, "imag"), context->fromDouble(imag));
    
    return res;
}

const proto::ProtoObject* py_complex_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* rObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "real"));
    const proto::ProtoObject* iObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "imag"));
    double real = (rObj && rObj->isDouble(context)) ? rObj->asDouble(context) : 0.0;
    double imag = (iObj && iObj->isDouble(context)) ? iObj->asDouble(context) : 0.0;
    
    char buf[128];
    if (real == 0.0) {
        std::snprintf(buf, sizeof(buf), "%.17gj", imag);
    } else {
        std::snprintf(buf, sizeof(buf), "(%.17g%s%.17gj)", real, (imag >= 0 ? "+" : ""), imag);
    }
    return context->fromUTF8String(buf);
}

// Forward declarations for implicit method wrappings
static const proto::ProtoObject* py_staticmethod(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_classmethod(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

// NOTE: The `py_type` function definition is not present in the provided content.
// The instruction asks to modify a loop within `py_type`.
// Since `py_type` is not here, I cannot apply the internal modification.
// I have only added the forward declarations as requested before where `py_type` would logically be.

static const proto::ProtoObject* py_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj == PROTO_TRUE) return context->fromUTF8String("True");
    if (obj == PROTO_FALSE) return context->fromUTF8String("False");
    if (obj->isInteger(context)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)obj->asLong(context));
        return context->fromUTF8String(buf);
    }
    if (obj->isDouble(context)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(context));
        return context->fromUTF8String(buf);
    }
    if (obj->isString(context)) {
        std::string s;
        obj->asString(context)->toUTF8String(context, s);
        std::string out = "'";
        for (unsigned char c : s) {
            if (c == '\'') out += "\\'";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 32 || c >= 127) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            } else {
                out += c;
            }
        }
        out += "'";
        return context->fromUTF8String(out.c_str());
    }

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* reprS = env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* cls = env ? env->getType(context, obj) : obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
    const proto::ProtoObject* reprMethod = cls ? cls->getAttribute(context, reprS) : obj->getAttribute(context, reprS);
    if (reprMethod && reprMethod->asMethod(context)) {
        return reprMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    }
    return context->fromUTF8String("<object>");
}

static const proto::ProtoObject* py_format(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj->isDouble(context)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(context));
        return context->fromUTF8String(buf);
    }
    if (obj->isInteger(context)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)obj->asLong(context));
        return context->fromUTF8String(buf);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* formatS = env ? env->getFormatString() : proto::ProtoString::fromUTF8String(context, "__format__");

    const proto::ProtoObject* formatMethod = obj->getAttribute(context, formatS);
    if (!formatMethod || !formatMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* args = context->newList();
    if (positionalParameters->getSize(context) >= 2) {
        args = args->appendLast(context, positionalParameters->getAt(context, 1));
    } else {
        args = args->appendLast(context, context->fromUTF8String(""));
    }
    return formatMethod->asMethod(context)(context, obj, nullptr, args, nullptr);
}

static const proto::ProtoObject* py_open(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* ioMod = self->getAttribute(context, env ? env->getIOModuleString() : proto::ProtoString::fromUTF8String(context, "__io_module__"));
    if (!ioMod || ioMod == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* openFunc = ioMod->getAttribute(context, env ? env->getOpenString() : proto::ProtoString::fromUTF8String(context, "open"));
    if (!openFunc || !openFunc->asMethod(context)) return PROTO_NONE;
    return openFunc->asMethod(context)(context, ioMod, nullptr, positionalParameters, keywordParameters);
}

static const proto::ProtoObject* py_self_iter(
    proto::ProtoContext*,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_enumerate_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);

static const proto::ProtoObject* py_enumerate(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 1);
    long long start = 0;
    if (positionalParameters->getSize(context) >= 3 && positionalParameters->getAt(context, 2)->isInteger(context))
        start = positionalParameters->getAt(context, 2)->asLong(context);

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* enumProtoS = env ? env->getEnumProtoString() : proto::ProtoString::fromUTF8String(context, "__enumerate_proto__");
    const proto::ProtoString* itS = env ? env->getEnumIterString() : proto::ProtoString::fromUTF8String(context, "__enumerate_it__");
    const proto::ProtoString* idxS = env ? env->getEnumIdxString() : proto::ProtoString::fromUTF8String(context, "__enumerate_idx__");

    const proto::ProtoObject* it = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
    if (!it || it == (env ? env->getNonePrototype() : nullptr) || it == PROTO_NONE) {
        if (get_env_diag()) printf("DEBUG: py_enumerate py_iter returned empty for iterable=%p (it=%p, envNone=%p)\n", (void*)iterable, (void*)it, (void*)(env ? env->getNonePrototype() : nullptr));
        return PROTO_NONE;
    }

    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* enumObj = cls->newChild(context, true);
    enumObj = enumObj->setAttribute(context, itS, it);
    enumObj = enumObj->setAttribute(context, idxS, context->fromInteger(start));
    if (get_env_diag()) printf("DEBUG: py_enumerate successfully created enumObj=%p\n", (void*)enumObj);
    return enumObj;
}

static const proto::ProtoObject* py_enumerate_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* itS = env ? env->getEnumIterString() : proto::ProtoString::fromUTF8String(context, "__enumerate_it__");
    const proto::ProtoString* idxS = env ? env->getEnumIdxString() : proto::ProtoString::fromUTF8String(context, "__enumerate_idx__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* it = self->getAttribute(context, itS);
    const proto::ProtoObject* idxObj = self->getAttribute(context, idxS);
    if (!it || !idxObj) {
        if (get_env_diag()) printf("DEBUG: py_enumerate_next it=%p idxObj=%p\n", (void*)it, (void*)idxObj);
        return nullptr;
    }

    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) {
        if (get_env_diag()) printf("DEBUG: py_enumerate_next nextMethod missing or not a method\n");
        return nullptr;
    }
    
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* value = nextMethod->asMethod(context)(context, it, nullptr, emptyL, nullptr);
    if (!value) {
        if (get_env_diag()) printf("DEBUG: py_enumerate_next nextMethod returned nullptr\n");
        return nullptr;
    }

    long long idx = idxObj->asLong(context);
    self->setAttribute(context, idxS, context->fromInteger(idx + 1));
    
    const proto::ProtoList* l = context->newList();
    l = l->appendLast(context, idxObj);
    l = l->appendLast(context, value);
    return context->newTupleFromList(l)->asObject(context);
}

    extern const proto::ProtoObject* invokePythonCallable(proto::ProtoContext* ctx, const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);

static const proto::ProtoObject* py_reversed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) {
        ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
        if (env) env->raiseTypeError(context, "reversed expected at least 1 argument, got 0");
        return nullptr;
    }
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0); // reversed type
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 1);

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* reversedS = env ? env->getReversedString() : proto::ProtoString::fromUTF8String(context, "__reversed__");
    const proto::ProtoString* lenS = env ? env->getLenString() : proto::ProtoString::fromUTF8String(context, "__len__");
    const proto::ProtoString* getitemS = env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(context, "__getitem__");
    const proto::ProtoString* revProtoS = env ? env->getRevProtoString() : proto::ProtoString::fromUTF8String(context, "__reversed_proto__");
    const proto::ProtoString* revObjS = env ? env->getRevObjString() : proto::ProtoString::fromUTF8String(context, "__reversed_obj__");
    const proto::ProtoString* revIdxS = env ? env->getRevIdxString() : proto::ProtoString::fromUTF8String(context, "__reversed_idx__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* revMethod = env ? env->getAttribute(context, obj, reversedS) : obj->getAttribute(context, reversedS);
    if (revMethod && revMethod != PROTO_NONE) {
        const proto::ProtoObject* r = ::protoPython::invokePythonCallable(context, revMethod, emptyL, nullptr);
        if (r) return r;
        if (env) env->clearPendingException(); // fallback if exception
    }

    const proto::ProtoObject* lenMethod = env ? env->getAttribute(context, obj, lenS) : obj->getAttribute(context, lenS);
    const proto::ProtoObject* getitemMethod = env ? env->getAttribute(context, obj, getitemS) : obj->getAttribute(context, getitemS);
    if (!lenMethod || !getitemMethod) {
        if (env) env->raiseTypeError(context, "'" + PythonEnvironment::reprObject(context, obj) + "' object is not reversible");
        return nullptr;
    }
    
    if (get_env_diag()) printf("DEBUG: py_reversed calling lenMethod=%p\n", (void*)lenMethod);
    const proto::ProtoObject* lenObj = ::protoPython::invokePythonCallable(context, lenMethod, emptyL, nullptr);
    if (!lenObj) {
        if (get_env_diag()) printf("DEBUG: py_reversed lenMethod->call returned nullptr\n");
        return nullptr; // Exception thrown by __len__
    }
    if (get_env_diag()) printf("DEBUG: py_reversed lenObj=%p repr=%s\n", (void*)lenObj, PythonEnvironment::reprObject(context, lenObj).c_str());
    if (!lenObj->isInteger(context)) {
        if (env) env->raiseTypeError(context, "'" + PythonEnvironment::reprObject(context, lenObj) + "' returned from __len__ cannot be interpreted as an integer");
        return nullptr;
    }
    long long n = lenObj->asLong(context);

    const proto::ProtoObject* revProto = self->getAttribute(context, revProtoS);
    if (!revProto || revProto == PROTO_NONE) {
        if (get_env_diag()) {
            printf("DEBUG: py_reversed failed: revProto is null or PROTO_NONE\n");
        }
        return PROTO_NONE;
    }
    const proto::ProtoObject* revObj = revProto->newChild(context, true);
    revObj->setAttribute(context, revObjS, obj);
    revObj->setAttribute(context, revIdxS, context->fromInteger(n - 1));
    return revObj;
}

static const proto::ProtoObject* py_reversed_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* objS = env ? env->getRevObjString() : proto::ProtoString::fromUTF8String(context, "__reversed_obj__");
    const proto::ProtoString* idxS = env ? env->getRevIdxString() : proto::ProtoString::fromUTF8String(context, "__reversed_idx__");
    const proto::ProtoString* getitemS = env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(context, "__getitem__");

    const proto::ProtoObject* obj = self->getAttribute(context, objS);
    const proto::ProtoObject* idxObj = self->getAttribute(context, idxS);
    if (!obj || !idxObj) return nullptr;
    long long idx = idxObj->asLong(context);
    if (idx < 0) return nullptr;

    const proto::ProtoObject* getitemMethod = obj->getAttribute(context, getitemS);
    if (!getitemMethod || !getitemMethod->asMethod(context)) return nullptr;
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(idx));
    const proto::ProtoObject* value = getitemMethod->asMethod(context)(context, obj, nullptr, args, nullptr);
    self->setAttribute(context, idxS, context->fromInteger(idx - 1));
    return value;
}

static const proto::ProtoObject* py_sum(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* start = positionalParameters->getSize(context) >= 2 ? positionalParameters->getAt(context, 1) : context->fromInteger(0);

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return start;

    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return start;

    long long acc = start->isInteger(context) ? start->asLong(context) : 0;
    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;
    
    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val) {
             if (env && env->handleExhaustion(context)) break;
             return nullptr; // Propagate other errors
        }
        if (val == noneObj) break;
        if (val->isInteger(context)) acc += val->asLong(context);
    }
    return context->fromInteger(acc);
}

static const proto::ProtoObject* py_all(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_TRUE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_TRUE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return PROTO_TRUE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_TRUE;

    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val) {
             if (env && env->handleExhaustion(context)) break;
             return nullptr; // Propagate other errors
        }
        if (val == noneObj) break;
        if (!val->asBoolean(context)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_any(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_FALSE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return PROTO_FALSE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_FALSE;

    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val) {
             if (env && env->handleExhaustion(context)) break;
             return nullptr; // Propagate other errors
        }
        if (val == noneObj) break;
        if (val->asBoolean(context)) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_callable(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj->asMethod(context)) return PROTO_TRUE;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* proto = obj->getPrototype(context);
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");
    const proto::ProtoObject* call = proto ? (env ? env->getAttribute(context, proto, callS) : proto->getAttribute(context, callS)) : nullptr;
    
    return (call && call != PROTO_NONE && call->asMethod(context)) ? PROTO_TRUE : PROTO_FALSE;
}

/** object.__init__(self) **/
static const proto::ProtoObject* py_object_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters && positionalParameters->getSize(context) > 1) { // 1 is self
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        std::string clsName = "unknown";
        bool isNewOverridden = false;
        
        if (env && positionalParameters->getSize(context) > 0) {
             const proto::ProtoObject* inst = positionalParameters->getAt(context, 0);
             if (inst) {
                  const proto::ProtoObject* cls = inst->getAttribute(context, env->getClassString());
                  if (cls) {
                       const proto::ProtoObject* nAttr = cls->getAttribute(context, env->getNameString());
                       if (nAttr && nAttr->isString(context)) nAttr->asString(context)->toUTF8String(context, clsName);
                       
                       const proto::ProtoObject* newAttr = cls->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__new__"));
                       if (std::getenv("PROTO_ENV_DIAG")) {
                           fprintf(stderr, "DEBUG py_object_init: newAttr=%p isMethod=%d\n", (void*)newAttr, newAttr ? newAttr->isMethod(context) : 0);
                       }
                       if (newAttr && newAttr->isMethod(context)) {
                           auto mPtr = newAttr->asMethod(context);
                           if (std::getenv("PROTO_ENV_DIAG")) {
                               fprintf(stderr, "DEBUG py_object_init: mPtr=%p py_object_new=%p\n", (void*)mPtr, (void*)py_object_new);
                           }
                           if (mPtr != py_object_new) {
                               isNewOverridden = true;
                           }
                       }
                  }
             }
        }
        
        if (!isNewOverridden) {
            if (std::getenv("PROTO_ENV_DIAG")) {
                 fprintf(stderr, "DEBUG HANG: py_object_init called on instance with class '%s' args=%zu\n", clsName.c_str(), positionalParameters->getSize(context));
                 fprintf(stderr, "DEBUG HANG: py_object_init addr pointer is %p\n", (void*)py_object_init);
            }
            if (env) env->raiseTypeError(context, "object.__init__() takes exactly one argument (the instance to initialize)");
        }
    }
    return PROTO_NONE;
}

/** object.__getattribute__(self, name) **/
static const proto::ProtoObject* py_object_getattribute(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* target = self;
    const proto::ProtoObject* nameObj = nullptr;
    if (positionalParameters && positionalParameters->getSize(context) >= 2) {
        target = positionalParameters->getAt(context, 0);
        nameObj = positionalParameters->getAt(context, 1);
    } else if (positionalParameters && positionalParameters->getSize(context) == 1) {
        nameObj = positionalParameters->getAt(context, 0);
    }
    if (!nameObj || !nameObj->isString(context)) return PROTO_NONE;
    std::string nameStr;
    nameObj->asString(context)->toUTF8String(context, nameStr);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(context, nameStr.c_str());
    const proto::ProtoObject* val = env ? env->getAttribute(context, target, key) : target->getAttribute(context, key);
    return val ? val : PROTO_NONE;
}

/** object.__setattr__(self, name, value) **/
static const proto::ProtoObject* py_object_setattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* target = self;
    const proto::ProtoObject* nameObj = nullptr;
    const proto::ProtoObject* value = nullptr;
    if (positionalParameters && positionalParameters->getSize(context) >= 3) {
        target = positionalParameters->getAt(context, 0);
        nameObj = positionalParameters->getAt(context, 1);
        value = positionalParameters->getAt(context, 2);
    } else if (positionalParameters && positionalParameters->getSize(context) == 2) {
        nameObj = positionalParameters->getAt(context, 0);
        value = positionalParameters->getAt(context, 1);
    }
    if (!nameObj || !nameObj->isString(context)) return PROTO_NONE;
    std::string nameStr;
    nameObj->asString(context)->toUTF8String(context, nameStr);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(context, nameStr.c_str());
    if (env) {
        env->setAttribute(context, const_cast<proto::ProtoObject*>(target), key, value);
    } else {
        const_cast<proto::ProtoObject*>(target)->setAttribute(context, key, value);
    }
    return PROTO_NONE;
}

/** getattr(obj, name[, default]): return obj.name, or default if given and attribute missing. */
static const proto::ProtoObject* py_getattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_NONE;
    /* Use canonical ProtoString from content so getAttribute matches setAttribute storage. */
    std::string nameStr;
    nameObj->asString(context)->toUTF8String(context, nameStr);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(context, nameStr.c_str());

    if (std::getenv("PROTO_RESOLVE_DIAG")) {
        std::string s;
        key->toUTF8String(context, s);
        fprintf(stderr, "DEBUG: getattr(obj=%p, key='%s')\n", (void*)obj, s.c_str());
    } 

    const proto::ProtoObject* val = env ? env->getAttribute(context, obj, key) : obj->getAttribute(context, key);
    if (std::getenv("PROTO_RESOLVE_DIAG")) {
        fprintf(stderr, "DEBUG: py_getattr val=%p PROTO_NONE=%p\n", (void*)val, (void*)PROTO_NONE);
    }
    if (val && (val != PROTO_NONE || obj->hasAttribute(context, key) == PROTO_TRUE)) {
        if (std::getenv("PROTO_RESOLVE_DIAG") && val == PROTO_NONE) {
            fprintf(stderr, "DEBUG: getattr returning None for key: %s\n", nameStr.c_str());
        }
        return val;
    }

    size_t argCount = positionalParameters->getSize(context);
    if (std::getenv("PROTO_RESOLVE_DIAG")) {
        fprintf(stderr, "DEBUG: py_getattr falling back. argCount=%zu\n", argCount);
    }
    if (argCount >= 3) {
        return positionalParameters->getAt(context, 2);
    }

    if (env) {
        env->raiseAttributeError(context, obj, nameStr);
    }
    return nullptr;
}

static const proto::ProtoObject* py_setattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 3) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* value = positionalParameters->getAt(context, 2);
    if (!nameObj->isString(context)) return PROTO_NONE;
    std::string nameStr;
    nameObj->asString(context)->toUTF8String(context, nameStr);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(context, nameStr.c_str());
    if (env) env->setAttribute(context, obj, key, value);
    else obj->setAttribute(context, key, value);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dir(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    
    const proto::ProtoObject* target = nullptr;
    if (positionalParameters->getSize(context) >= 1) {
        target = positionalParameters->getAt(context, 0);
    } else {
        target = py_locals(context, self, parentLink, positionalParameters, keywordParameters);
    }

    if (!target || target == PROTO_NONE) return context->newList()->asObject(context);

    std::vector<std::string> names;
    std::function<void(const proto::ProtoObject*)> collect = [&](const proto::ProtoObject* obj) {
        if (!obj) return;
        const proto::ProtoSparseList* attrs = obj->getAttributes(context);
        if (attrs) {
            auto* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(context));
            while (it && it->hasNext(context)) {
                unsigned long key = it->nextKey(context);
                const proto::ProtoString* s = reinterpret_cast<const proto::ProtoObject*>(key)->asString(context);
                if (s) {
                    std::string name;
                    s->toUTF8String(context, name);
                    if (!name.empty()) names.push_back(name);
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it->advance(context));
            }
        }
        const proto::ProtoList* parents = obj->getParents(context);
        if (parents) {
            for (size_t i = 0; i < parents->getSize(context); ++i) {
                collect(parents->getAt(context, i));
            }
        }
    };

    collect(target);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    const proto::ProtoList* result = context->newList();
    for (const auto& name : names) {
        result = result->appendLast(context, context->fromUTF8String(name.c_str()));
    }
    
    // Wrap in a Python list object (Step 1347 fix)
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* listType = self->getAttribute(context, env ? env->getListTypeString() : proto::ProtoString::fromUTF8String(context, "list"));
    if (listType && listType != PROTO_NONE) {
        const proto::ProtoObject* listObj = listType->newChild(context, true);
        listObj->setAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"), result->asObject(context));
        return listObj;
    }

    return result->asObject(context);
}

/** input([prompt]): read line from stdin, return as string. */
static const proto::ProtoObject* py_input(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* promptObj = positionalParameters->getAt(context, 0);
        if (promptObj) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
            const proto::ProtoObject* strMethod = promptObj->getAttribute(context, env ? env->getStrString() : proto::ProtoString::fromUTF8String(context, "__str__"));
            if (strMethod && strMethod->asMethod(context)) {
                const proto::ProtoObject* s = strMethod->asMethod(context)(context, promptObj, nullptr, emptyL, nullptr);
                if (s && s->isString(context)) {
                    std::string prompt;
                    s->asString(context)->toUTF8String(context, prompt);
                    std::cout << prompt << std::flush;
                }
            } else if (promptObj->isString(context)) {
                std::string prompt;
                promptObj->asString(context)->toUTF8String(context, prompt);
                std::cout << prompt << std::flush;
            }
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    std::istream* in = env ? env->getStdin() : &std::cin;
    std::string line;
    if (in && std::getline(*in, line))
        return context->fromUTF8String(line.c_str());
    
    if (in && in->eof()) {
        if (env) env->raiseEOFError(context);
        return PROTO_NONE;
    }
    return context->fromUTF8String("");
}

/** _tokenize_source(source): return list of (toktype_int, value_str) for tokenizer. Internal use by tokenize module. */
static const proto::ProtoObject* py__tokenize_source(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* srcObj = positionalParameters->getAt(context, 0);
    if (!srcObj->isString(context)) return PROTO_NONE;
    std::string source;
    srcObj->asString(context)->toUTF8String(context, source);
    Tokenizer tok(source);
    const proto::ProtoList* result = context->newList();
    while (true) {
        Token t = tok.next();
        if (t.type == TokenType::EndOfFile) break;
        const proto::ProtoList* pair = context->newList();
        pair = pair->appendLast(context, context->fromInteger(static_cast<int>(t.type)));
        pair = pair->appendLast(context, context->fromUTF8String(t.value.c_str()));
        result = result->appendLast(context, pair->asObject(context));
    }
    return result->asObject(context);
}

/** compile(source, filename='<string>', mode='eval'): return code object. */
static const proto::ProtoObject* py_compile(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* sourceObj = positionalParameters->getAt(context, 0);
    if (!sourceObj->isString(context)) return PROTO_NONE;
    std::string source;
    sourceObj->asString(context)->toUTF8String(context, source);
    std::string filename = "<string>";
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* fn = positionalParameters->getAt(context, 1);
        if (fn->isString(context)) fn->asString(context)->toUTF8String(context, filename);
    }
    std::string mode = "eval";
    if (positionalParameters->getSize(context) >= 3) {
        const proto::ProtoObject* m = positionalParameters->getAt(context, 2);
        if (m->isString(context)) m->asString(context)->toUTF8String(context, mode);
    }
    Compiler compiler(context, filename);
    if (mode == "eval") {
        Parser parser(source);
        std::unique_ptr<ASTNode> expr = parser.parseExpression();
        if (!expr || !compiler.compileExpression(expr.get())) return PROTO_NONE;
    } else {
        Parser parser(source);
        std::unique_ptr<ModuleNode> mod = parser.parseModule();
        if (!mod || mod->body.empty() || !compiler.compileModule(mod.get())) return PROTO_NONE;
    }
    return makeCodeObject(context, compiler.getConstants(), compiler.getNames(), compiler.getBytecode(), nullptr, nullptr, 0, 0, 0, false);
}

/** eval(expr, globals=None, locals=None): compile and run expression. */
static const proto::ProtoObject* py_eval(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* exprObj = positionalParameters->getAt(context, 0);
    if (!exprObj->isString(context)) return PROTO_NONE;
    std::string source;
    exprObj->asString(context)->toUTF8String(context, source);
    Parser parser(source);
    std::unique_ptr<ASTNode> expr = parser.parseExpression();
    if (!expr || !parser.atEOF()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::string msg = parser.hasError() ? parser.getLastErrorMsg() : "invalid syntax";
            if (!expr && !parser.hasError()) msg = "unexpected EOF while parsing";
            else if (expr && !parser.atEOF()) msg = "invalid syntax (likely a statement where expression was expected)";

            std::string lineText = source;
            int line = parser.getLastErrorLine();
            if (line == 0) line = 1;
            size_t start = 0;
            for (int i = 1; i < line; ++i) {
                size_t next = source.find('\n', start);
                if (next == std::string::npos) break;
                start = next + 1;
            }
            size_t end = source.find('\n', start);
            lineText = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
            env->raiseSyntaxError(context, msg, line, parser.getLastErrorColumn(), lineText);
        }
        return PROTO_NONE;
    }
    Compiler compiler(context, "<string>");
    if (!compiler.compileExpression(expr.get())) return PROTO_NONE;
    const proto::ProtoTuple* cos = compiler.getConstants();
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_eval compiling source='%s'\n", source.c_str());
        for (unsigned long i = 0; i < cos->getSize(context); i++) {
             fprintf(stderr, "  Const[%lu]: %s\n", i, PythonEnvironment::reprObject(context, cos->getAt(context, i)).c_str());
        }
        fflush(stderr);
    }
    const proto::ProtoObject* codeObj = makeCodeObject(context, cos, compiler.getNames(), compiler.getBytecode(), nullptr, nullptr, 0, 0, 0, false, false, nullptr, 1, compiler.getLnotab());
    if (!codeObj) return PROTO_NONE;
    proto::ProtoObject* globals = nullptr;
    proto::ProtoObject* locals = nullptr;
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* g = positionalParameters->getAt(context, 1);
        if (g && g != PROTO_NONE) globals = const_cast<proto::ProtoObject*>(g);
    }
    if (positionalParameters->getSize(context) >= 3) {
        const proto::ProtoObject* l = positionalParameters->getAt(context, 2);
        if (l && l != PROTO_NONE) locals = const_cast<proto::ProtoObject*>(l);
    }
    if (!globals) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) globals = const_cast<proto::ProtoObject*>(env->getGlobals());
    }
    if (!globals) globals = const_cast<proto::ProtoObject*>(context->newObject(false));
    if (!locals) locals = globals;

    GlobalsScope gscope(globals);
    // Note: currently runCodeObject runs in one frame/namespace. 
    // If globals and locals are different, we primarily use locals for the execution frame.
    const proto::ProtoObject* result = runCodeObject(context, codeObj, locals);
    if (get_env_diag()) {
        printf("DEBUG: py_eval source='%s' result=%p\n", source.c_str(), (void*)result);
    }
    return result ? result : PROTO_NONE;
}

/** help(obj): stub returning None. */
static const proto::ProtoObject* py_help(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; 
    (void)parentLink;
    (void)keywordParameters;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);

    if (positionalParameters->getSize(context) == 0) {
        std::cout << "Welcome to protoPython 0.1.0 help!\n"
                  << "If this is your first time using Python, you should definitely check out\n"
                  << "the tutorial on the Internet at https://docs.python.org/3/tutorial/.\n\n"
                  << "Enter the name of any module, keyword, or topic to get help on writing\n"
                  << "Python programs and using Python modules.  To quit this help utility and\n"
                  << "return to the interpreter, just type \"quit\".\n\n"
                  << "To get help on an object, type help(object).\n";
        return PROTO_NONE;
    }

    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* doc = obj->getAttribute(context, env ? env->getDocString() : proto::ProtoString::fromUTF8String(context, "__doc__"));
    const proto::ProtoObject* nameAttr = obj->getAttribute(context, env ? env->getNameString() : proto::ProtoString::fromUTF8String(context, "__name__"));
    std::string typeName = "object";
    if (nameAttr && nameAttr->isString(context)) {
        nameAttr->asString(context)->toUTF8String(context, typeName);
    } else {
        const proto::ProtoObject* type = env ? env->getType(context, obj) : obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
        if (type) {
            const proto::ProtoObject* tNameAttr = type->getAttribute(context, env ? env->getNameString() : proto::ProtoString::fromUTF8String(context, "__name__"));
            if (tNameAttr && tNameAttr->isString(context)) tNameAttr->asString(context)->toUTF8String(context, typeName);
        }
    }
    
    std::cout << "Help on " << typeName << " object:\n\n";
    if (doc && doc->isString(context)) {
        std::string ds;
        doc->asString(context)->toUTF8String(context, ds);
        std::cout << ds << "\n";
    } else {
        std::cout << "(No documentation available)\n";
    }

    // Attempt to call __repr__
    const proto::ProtoString* reprS = env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__");
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, reprS);
    if (reprMethod && reprMethod->asMethod(context)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
        const proto::ProtoObject* r = reprMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
        if (r && r->isString(context)) {
            std::string s;
            r->asString(context)->toUTF8String(context, s);
            std::cout << "  Value: " << s << "\n";
        }
    }

    return PROTO_NONE;
}

/** memoryview(obj): stub returning None. */
static const proto::ProtoObject* py_memoryview(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_super_getattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 0);
    if (!nameObj->isString(context)) return PROTO_NONE;
    
    // Get stored 'obj' and 'type' from proxy
    const proto::ProtoObject* obj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "obj"));
    const proto::ProtoObject* type = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "type"));
    if (!obj || !type) return PROTO_NONE;

    // Search MRO. For Python classes, __mro__ from the `obj` is the source of truth perfectly linearized.
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    
    bool isClass = obj->hasOwnAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__")) == PROTO_TRUE;
    const proto::ProtoObject* mroSrc = isClass ? obj : env->getType(context, obj);
    const proto::ProtoObject* mroAttr = mroSrc ? mroSrc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__")) : nullptr;
    
    std::vector<const proto::ProtoObject*> parentsList;
    if (mroAttr && mroAttr->isTuple(context)) {
        const proto::ProtoTuple* mro = mroAttr->asTuple(context);
        bool found = false;
        for (size_t i = 0; i < mro->getSize(context); ++i) {
            if (found) {
                parentsList.push_back(mro->getAt(context, i));
            } else if (mro->getAt(context, i) == type) {
                found = true;
            }
        }
        if (!found && mro->getSize(context) > 1) {
            for (size_t i = 1; i < mro->getSize(context); ++i) {
                parentsList.push_back(mro->getAt(context, i));
            }
        }
    } else {
        const proto::ProtoList* parents = type->getParents(context);
        if (parents) {
            for (size_t i = 0; i < parents->getSize(context); ++i) {
                parentsList.push_back(parents->getAt(context, i));
            }
        }
    }
    
    if (!parentsList.empty()) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::string nameS;
            nameObj->asString(context)->toUTF8String(context, nameS);
            fprintf(stderr, "DEBUG: py_super_getattr type=%p parents_size=%lu name=%s (%p) env=%p\n", 
                (void*)type, parentsList.size(), nameS.c_str(), (void*)nameObj, (void*)env);
        }
        for (size_t i = 0; i < parentsList.size(); ++i) {
            const proto::ProtoObject* parent = parentsList[i];
            if (std::getenv("PROTO_ENV_DIAG")) {
                std::string s;
                nameObj->asString(context)->toUTF8String(context, s);
                std::string tName = "unknown";
                std::string pName = "unknown";
                const proto::ProtoObject* tNameAttr = type->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
                if (tNameAttr && tNameAttr->isString(context)) tNameAttr->asString(context)->toUTF8String(context, tName);
                const proto::ProtoObject* pNameAttr = parent->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
                if (pNameAttr && pNameAttr->isString(context)) pNameAttr->asString(context)->toUTF8String(context, pName);
                fprintf(stderr, "DEBUG: py_super_getattr checking parent %p (name=%s) of type %p (name=%s) for %s\n", (void*)parent, pName.c_str(), (void*)type, tName.c_str(), s.c_str());
            }
            
            // Use hasOwnAttribute / direct data lookup to avoid full MRO search from this parent
            // which would defeat C3 linearization (e.g., finding object.__init__ on first parent).
            const proto::ProtoString* nameStr = nameObj->asString(context);
            unsigned long target_hash = (unsigned long)nameStr;
            const proto::ProtoObject* val = PROTO_NONE;
            
            // Fast path: hasOwnAttribute check (using interned pointer address as sparse list key)
            const proto::ProtoSparseList* attrs = parent->getOwnAttributes(context);
            bool isNew = (nameStr && nameStr->cmp_to_string(context, proto::ProtoString::fromUTF8String(context, "__new__")) == 0);
            if (isNew) {
                if (!attrs) fprintf(stderr, "DEBUG py_super_getattr(__new__): attrs is null on parent %p\n", (void*)parent);
                else {
                    fprintf(stderr, "DEBUG py_super_getattr(__new__): looking for hash %lu on parent %p:\n", target_hash, (void*)parent);
                    const proto::ProtoSparseListIterator* it = attrs->getIterator(context);
                    while (it && it->hasNext(context)) {
                        unsigned long hk = it->nextKey(context);
                        const proto::ProtoObject* vk = reinterpret_cast<const proto::ProtoObject*>(hk);
                        std::string k_str = "?";
                        if (vk && vk->isString(context)) vk->asString(context)->toUTF8String(context, k_str);
                        fprintf(stderr, "DEBUG   - attr hash %lu (str='%s')\n", hk, k_str.c_str());
                        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(context);
                    }
                }
            }
            if (attrs) {
                const proto::ProtoObject* match = attrs->getAt(context, target_hash);
                if (match) {
                    val = match;
                } else {
                    // Search by string comparison just in case pointer was different but string identical
                    const proto::ProtoSparseListIterator* it = attrs->getIterator(context);
                    while (it && it->hasNext(context)) {
                        unsigned long kHash = it->nextKey(context);
                        const proto::ProtoObject* v = it->nextValue(context);
                        if (kHash != 0) {
                            const proto::ProtoObject* keyObj = reinterpret_cast<const proto::ProtoObject*>(kHash);
                            if (keyObj && keyObj->isString(context)) {
                                if (keyObj->asString(context)->cmp_to_string(context, nameStr) == 0) {
                                    val = v; break;
                                }
                            }
                        }
                        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(context);
                    }
                }
            }
            
            if (!val || val == PROTO_NONE) {
                // Check __dict__ exactly properly matching the Python spec
                const proto::ProtoObject* dict = parent->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__dict__"));
                if (dict && dict != PROTO_NONE && dict->asSparseList(context)) {
                    const proto::ProtoObject* match = dict->asSparseList(context)->getAt(context, nameStr->getHash(context));
                    if (match) val = match;
                } else if (dict && dict != PROTO_NONE) {
                    // Try dictionary getitem
                    const proto::ProtoObject* getItemMethod = dict->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__getitem__"));
                    if (getItemMethod && getItemMethod->isMethod(context)) {
                        const proto::ProtoList* args = context->newList()->appendLast(context, nameObj);
                        val = getItemMethod->asMethod(context)(context, dict, nullptr, args, nullptr);
                        if (env && env->hasPendingException()) env->clearPendingException();
                    }
                }
            }
            
            if (isNew) fprintf(stderr, "DEBUG py_super_getattr(__new__): parent %p returned val %p\n", (void*)parent, (void*)val);
            
            if (val && val != PROTO_NONE) {
                // Found! Now check for descriptor protocol.
                // Create __get__ string
                const proto::ProtoString* getStr = proto::ProtoString::fromUTF8String(context, "__get__");
                const proto::ProtoObject* descrGet = val->getAttribute(context, getStr);
                
                if (descrGet && descrGet != PROTO_NONE && descrGet->asMethod(context)) {
                    // Invoke __get__(obj, type)
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        fprintf(stderr, "DEBUG: py_super_getattr binding descriptor for %p (descrGet=%p). obj=%p type=%p\n", (void*)val, (void*)descrGet, (void*)obj, (void*)type);
                        fflush(stderr);
                    }
                    const proto::ProtoList* args = context->newList()->appendLast(context, obj)->appendLast(context, type);
                    return descrGet->asMethod(context)(context, val, nullptr, args, nullptr);
                }
                
                if (std::getenv("PROTO_ENV_DIAG")) {
                    fprintf(stderr, "DEBUG: py_super_getattr returning unbound val %p\n", (void*)val);
                    fflush(stderr);
                }
                return val;
            }
        }
    }
    
    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super_getattr FAILED to find attribute\n");
    return PROTO_NONE;
}

static const proto::ProtoObject* py_super_setattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* valueObj = positionalParameters->getAt(context, 1);
    
    if (!nameObj->isString(context)) return PROTO_NONE;
    
    // Get stored 'obj' and 'type' from proxy
    const proto::ProtoObject* obj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "obj"));
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;

    // Call setAttribute directly on the bound object
    return obj->setAttribute(context, nameObj->asString(context), valueObj);
}

static const proto::ProtoObject* py_super(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {

    const proto::ProtoObject* type = nullptr;
    const proto::ProtoObject* obj = nullptr;
    
    if (positionalParameters->getSize(context) == 0) {
       if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super 0-arg ENTER\n");
       // 0-arg super(): deduce from frame
       PythonEnvironment* env = PythonEnvironment::fromContext(context);
       const proto::ProtoObject* frame = env ? env->getCurrentFrame() : nullptr;
       if (!frame) return PROTO_NONE;
       
       // Get first argument (self/cls) from locals
       // Check fast locals (slots) first if available
       if (context->getAutomaticLocalsCount() > 0 && context->getAutomaticLocals()) {
           obj = context->getAutomaticLocals()[0];
           if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super found obj in fast locals: %p\n", (void*)obj);
       }
       
       const proto::ProtoObject* locals = frame->getAttribute(context, env->getFLocalsString());

       if (locals && locals != PROTO_NONE) {
           bool foundArg = false;
           // robust: lookup the exact first argument name from co_varnames
           const proto::ProtoObject* codeObj = frame->getAttribute(context, env->getFCodeString());
           if (codeObj && codeObj != PROTO_NONE) {
               const proto::ProtoObject* varnamesObj = codeObj->getAttribute(context, env->getCoVarnamesString());
               if (varnamesObj && varnamesObj != PROTO_NONE && varnamesObj->isTuple(context)) {
                   const proto::ProtoTuple* varnames = varnamesObj->asTuple(context);
                   if (varnames->getSize(context) > 0) {
                       const proto::ProtoObject* firstArgName = varnames->getAt(context, 0);
                       if (firstArgName && firstArgName->isString(context)) {
                           const proto::ProtoObject* slowObj = locals->getAttribute(context, firstArgName->asString(context));
                           if (slowObj && slowObj != PROTO_NONE) {
                               obj = slowObj;
                               foundArg = true;
                               if (std::getenv("PROTO_ENV_DIAG")) {
                                   std::string n;
                                   firstArgName->asString(context)->toUTF8String(context, n);
                                   fprintf(stderr, "DEBUG: py_super found obj in locals (%s): %p\n", n.c_str(), (void*)obj);
                               }
                           }
                       }
                   }
               }
           }
           
           if (!foundArg && (!obj || obj == PROTO_NONE)) {
               // Fallback heuristics: 'self', 'cls', 'mcls'
               const proto::ProtoObject* slowObj = locals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "self"));
               if (slowObj && slowObj != PROTO_NONE) {
                   obj = slowObj;
               } else {
                   obj = locals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "cls"));
                   if (!obj || obj == PROTO_NONE) {
                        obj = locals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "metacls"));
                        if (!obj || obj == PROTO_NONE) {
                             obj = locals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "mcls"));
                        }
                   }
               }
           }
           
           // BFS search for __class__ in closure scopes (for super)
           const proto::ProtoString* classStr = env->getClassString(); // "__class__"
           
           // Use a worklist to traverse scopes via __closure__
           std::vector<const proto::ProtoObject*> worklist;
           std::unordered_set<const proto::ProtoObject*> visited;
           
           if (frame) {
               worklist.push_back(frame);
               visited.insert(frame);
           }
           
           size_t idx = 0;
           while (idx < worklist.size()) {
               const proto::ProtoObject* curr = worklist[idx++];
               
               // 1. Check frame locals (fast locals usually include freevars)
               const proto::ProtoObject* locals = curr->getAttribute(context, env->getFLocalsString());
               if (locals && locals != PROTO_NONE) {
                     if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super BFS checking locals of scope %p\n", (void*)curr);
                     // Fix: locals is a ProtoObject (dict or frame), not a SparseList directly.
                     // Use getAttribute to retrieve the value.
                     const proto::ProtoObject* val = PROTO_NONE;
                      if (locals->hasOwnAttribute(context, classStr) == PROTO_TRUE) {
                           val = locals->getAttribute(context, classStr);
                      }
                      if (val && val != PROTO_NONE) {
                          type = val;
                          if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super found __class__ in f_locals of scope %p\n", (void*)curr);
                          goto found_class;
                     }
               }
               
               // 2. Check closure via code object freevars
               const proto::ProtoObject* code = curr->getAttribute(context, env->getFCodeString());
               const proto::ProtoObject* closure = curr->getAttribute(context, env->getClosureString());

               if (std::getenv("PROTO_ENV_DIAG")) {
                   fprintf(stderr, "DEBUG: py_super BFS scope %p: code=%p, closure=%p, closure_is_tuple=%d\n", 
                           (void*)curr, (void*)code, (void*)closure, 
                           (closure && closure->isTuple(context)));
               }
               
               if (code && code != PROTO_NONE && closure && closure->isTuple(context)) {
                     const proto::ProtoObject* freevars = code->getAttribute(context, proto::ProtoString::fromUTF8String(context, "co_freevars"));
                     if (freevars && freevars->isTuple(context)) {
                          const proto::ProtoTuple* freeTup = freevars->asTuple(context);
                          const proto::ProtoTuple* closureTup = closure->asTuple(context);
                          for (size_t i = 0; i < freeTup->getSize(context); ++i) {
                               const proto::ProtoObject* name = freeTup->getAt(context, i);
                               if (name->isString(context) && name->asString(context)->cmp_to_string(context, classStr) == 0) {
                                    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super found __class__ in freevars at index %zu\n", i);
                                    if (i < closureTup->getSize(context)) {
                                         const proto::ProtoObject* cell = closureTup->getAt(context, i);
                                         // Use getAttribute("cell_contents")
                                         const proto::ProtoObject* val = cell->getAttribute(context, proto::ProtoString::fromUTF8String(context, "cell_contents"));
                                         if (val && val != PROTO_NONE) {
                                              type = val;
                                              if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super retrieved __class__ from closure cell\n");
                                              goto found_class;
                                         }
                                    }
                               }
                          }
                     }
               }
               
               // Expand closure
               if (closure && closure != PROTO_NONE) {
                   if (closure->asList(context)) {
                       const proto::ProtoList* l = closure->asList(context);
                       for (unsigned long i = 0; i < l->getSize(context); ++i) {
                           const proto::ProtoObject* next = l->getAt(context, i);
                           if (visited.find(next) == visited.end()) {
                               visited.insert(next);
                               worklist.push_back(next);
                           }
                       }
                   } else if (closure->asTuple(context)) {
                       const proto::ProtoTuple* t = closure->asTuple(context);
                       for (unsigned long i = 0; i < t->getSize(context); ++i) {
                           const proto::ProtoObject* next = t->getAt(context, i);
                           if (visited.find(next) == visited.end()) {
                               visited.insert(next);
                               worklist.push_back(next);
                           }
                       }
                   }
               }
           }
           found_class:;
       }
       
        if (obj && obj != PROTO_NONE) {
            if (!type) {
                // Fallback: heuristically find the defining class
                if (frame && env) {
                    const proto::ProtoObject* codeObj = frame->getAttribute(context, env->getFCodeString());
                    if (codeObj && codeObj != PROTO_NONE) {
                        const proto::ProtoObject* co_name = codeObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "co_name"));
                        bool isClass = obj->hasOwnAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__")) == PROTO_TRUE;
                        const proto::ProtoObject* mroSrc = isClass ? obj : env->getType(context, obj);
                        const proto::ProtoObject* mroObj = mroSrc ? mroSrc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__")) : nullptr;
                        const proto::ProtoTuple* mro = (mroObj && mroObj != PROTO_NONE) ? mroObj->asTuple(context) : nullptr;
                        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG_SUPER_MRO_CHECK: obj=%p isClass=%d mroSrc=%p mroObj=%p mro=%p co_name=%p co_nameStr=%d\n", (void*)obj, isClass, (void*)mroSrc, (void*)mroObj, (void*)mro, (void*)co_name, co_name ? co_name->isString(context) : 0);
                        if (mro && co_name && co_name->isString(context)) {
                            for (size_t i = 0; i < mro->getSize(context); ++i) {
                                const proto::ProtoObject* cls = mro->getAt(context, i);
                                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG_SUPER_MRO: cls=%p hasOwn=%d\n", (void*)cls, cls->hasOwnAttribute(context, co_name->asString(context)) == PROTO_TRUE);
                                if (cls->hasOwnAttribute(context, co_name->asString(context)) == PROTO_TRUE) {
                                    const proto::ProtoObject* attr = cls->getAttribute(context, co_name->asString(context));
                                    const proto::ProtoObject* attrCode = attr ? attr->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__code__")) : nullptr;
                                    if (!attrCode || attrCode == PROTO_NONE) {
                                        const proto::ProtoObject* func = attr ? attr->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__func__")) : nullptr;
                                        if (func && func != PROTO_NONE) {
                                            attrCode = func->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__code__"));
                                        }
                                    }
                                    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG_SUPER_MRO: cls=%p attr=%p attrCode=%p codeObj=%p\n", (void*)cls, (void*)attr, (void*)attrCode, (void*)codeObj);
                                    if (attrCode == codeObj) {
                                        type = cls;
                                        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG_SUPER_MRO: MATCH! type set to %p\n", (void*)type);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                if (!type) {
                    if (env) {
                        type = env ? env->getType(context, obj) : obj->getAttribute(context, env->getClassString());
                    } else {
                        type = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
                    }
                }
                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super 0-arg inferred type=%p from obj=%p\n", (void*)type, (void*)obj);
            }
        } else {
            if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super 0-arg failed to find self/cls\n");
            return PROTO_NONE;
        }
    } else {
        type = positionalParameters->getAt(context, 0);
        obj = positionalParameters->getAt(context, 1);
    }
    
    if (!type || !obj) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super returning NONE (type=%p, obj=%p)\n", (void*)type, (void*)obj);
        return PROTO_NONE;
    }

    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_super returning PROXY\n");
    proto::ProtoObject* proxy = const_cast<proto::ProtoObject*>(context->newObject(true));
    proxy->setAttribute(context, proto::ProtoString::fromUTF8String(context, "type"), type);
    proxy->setAttribute(context, proto::ProtoString::fromUTF8String(context, "obj"), obj);
    proxy->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__getattr__"), context->fromMethod(proxy, py_super_getattr));
    proxy->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__setattr__"), context->fromMethod(proxy, py_super_setattr));
    proxy->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__is_super_proxy__"), context->fromBoolean(true));

    return proxy;
}

/** exec(source, globals=None, locals=None): compile and run source. */
static const proto::ProtoObject* py_exec(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* sourceObj = positionalParameters->getAt(context, 0);
    if (!sourceObj->isString(context)) return PROTO_NONE;
    std::string source;
    sourceObj->asString(context)->toUTF8String(context, source);
    Parser parser(source);
    std::unique_ptr<ModuleNode> mod = parser.parseModule();
    if (parser.hasError()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::string lineText = source;
            int line = parser.getLastErrorLine();
            size_t start = 0;
            for (int i = 1; i < line; ++i) {
                size_t next = source.find('\n', start);
                start = (next == std::string::npos) ? start : next + 1;
            }
            size_t end = source.find('\n', start);
            lineText = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
            env->raiseSyntaxError(context, parser.getLastErrorMsg(), line, parser.getLastErrorColumn(), lineText);
        }
        return PROTO_NONE;
    }
    if (!mod || mod->body.empty()) {
        return PROTO_NONE;
    }
    Compiler compiler(context, "<string>");
    if (get_env_diag()) {
        std::string source;
        sourceObj->asString(context)->toUTF8String(context, source);
        fprintf(stderr, "DEBUG: py_exec compiling source='%.100s...'\n", source.c_str());
    }
    if (!compiler.compileModule(mod.get())) {
        return PROTO_NONE;
    }
    const proto::ProtoObject* codeObj = makeCodeObject(context, compiler.getConstants(), compiler.getNames(), compiler.getBytecode(), nullptr, nullptr, 0, 0, 0, 0, false, nullptr, 1, compiler.getLnotab());
    if (!codeObj) {
        return PROTO_NONE;
    }
    proto::ProtoObject* globals = nullptr;
    proto::ProtoObject* locals = nullptr;
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* g = positionalParameters->getAt(context, 1);
        if (g && g != PROTO_NONE) globals = const_cast<proto::ProtoObject*>(g);
    }
    if (positionalParameters->getSize(context) >= 3) {
        const proto::ProtoObject* l = positionalParameters->getAt(context, 2);
        if (l && l != PROTO_NONE) locals = const_cast<proto::ProtoObject*>(l);
    }
    if (!globals) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) globals = const_cast<proto::ProtoObject*>(env->getGlobals());
    }
    if (!globals) globals = const_cast<proto::ProtoObject*>(context->newObject(false));
    if (!locals) locals = globals;

    GlobalsScope gscope(globals);
    return runCodeObject(context, codeObj, locals);
}

/** breakpoint(): no-op stub; real breakpoint requires debugger integration. */
static const proto::ProtoObject* py_breakpoint(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters; (void)positionalParameters;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* sys = env ? env->resolve("sys", context) : nullptr;
    if (sys) {
        const proto::ProtoObject* hook = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "breakpointhook"));
        if (hook && hook->asMethod(context)) {
            return hook->asMethod(context)(context, sys, nullptr, env->getEmptyList(), nullptr);
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_globals(proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* positionalParameters, const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* f = PythonEnvironment::getCurrentFrame();
    if (f && f != PROTO_NONE && env) {
        const proto::ProtoObject* mod = f->getAttribute(context, env->getFGlobalsString());
        if (mod && mod != PROTO_NONE) return mod;
    }
    return PythonEnvironment::getCurrentGlobals();
}

static const proto::ProtoObject* py_locals(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    const proto::ProtoObject* f = PythonEnvironment::getCurrentFrame();
    if (!f || f == PROTO_NONE) {
         return py_globals(ctx, self, parentLink, nullptr, nullptr);
    }
    return f;
}

static const proto::ProtoObject* py_vars(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (!args || args->getSize(ctx) == 0) {
        return py_locals(ctx, self, parentLink, nullptr, nullptr);
    }
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* dictS = env ? env->getDictDunderString() : proto::ProtoString::fromUTF8String(ctx, "__dict__");
    
    const proto::ProtoObject* dict = obj->getAttribute(ctx, dictS);
    if (dict && dict != PROTO_NONE) return dict;
    
    return obj;
}

/** Compare two objects for sorting: int, string, else compare(). */
static int sorted_compare(proto::ProtoContext* context, const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a == b) return 0;
    if (a->isInteger(context) && b->isInteger(context)) {
        long long av = a->asLong(context);
        long long bv = b->asLong(context);
        if (av == bv) return 0;
        return av < bv ? -1 : 1;
    }
    if (a->isString(context) && b->isString(context)) {
        std::string sa;
        std::string sb;
        a->asString(context)->toUTF8String(context, sa);
        b->asString(context)->toUTF8String(context, sb);
        if (sa == sb) return 0;
        return sa < sb ? -1 : 1;
    }
    int cmp = a->compare(context, b);
    if (cmp != 0) return cmp;
    unsigned long ha = a->getHash(context);
    unsigned long hb = b->getHash(context);
    if (ha == hb) return 0;
    return ha < hb ? -1 : 1;
}

static const proto::ProtoObject* py_sorted(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    (void)self;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, env ? env->getEmptyList() : context->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_NONE;

    std::vector<const proto::ProtoObject*> elems;
    for (;;) {
        const proto::ProtoObject* val = nextMethod->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        elems.push_back(val);
    }

    std::sort(elems.begin(), elems.end(), [context](const proto::ProtoObject* a, const proto::ProtoObject* b) {
        return sorted_compare(context, a, b) < 0;
    });

    const proto::ProtoList* resultList = context->newList();
    for (const proto::ProtoObject* obj : elems)
        resultList = resultList->appendLast(context, obj);

    if (!env) return PROTO_NONE;
    const proto::ProtoObject* listObj = env->getListPrototype()->newChild(context, true);
    listObj->setAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"), resultList->asObject(context));
    return listObj;
}

const proto::ProtoObject* py_object_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* obj = self;
    if (!obj && positionalParameters->getSize(context) > 0) {
        obj = positionalParameters->getAt(context, 0);
    }
    if (!obj) return PROTO_NONE;
    // Identity hash
    unsigned long h = reinterpret_cast<unsigned long>(obj);
    return context->fromInteger(static_cast<long long>(h));
}


static const proto::ProtoObject* py_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* hashMethod = obj->getAttribute(context, env ? env->getHashString() : proto::ProtoString::fromUTF8String(context, "__hash__"));
    if (!hashMethod || !hashMethod->asMethod(context)) return PROTO_NONE;
    return hashMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
}

static const proto::ProtoObject* py_hasattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_FALSE;
    const proto::ProtoString* nameStr = nameObj->asString(context);
    
    std::string nString;
    nameStr->toUTF8String(context, nString);
    if (nString == "CodeType" || nString == "MappingProxyType") {
        if (get_env_diag()) { fprintf(stderr, "DEBUG_HASATTR: obj=%p name='%s' name_ptr=%p hasAttr=%d\n", (void*)obj, nString.c_str(), (void*)nameStr, obj->hasAttribute(context, nameStr)==PROTO_TRUE); fflush(stderr); }
        // Also dump its keys!
        const proto::ProtoObject* data = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
        if (get_env_diag() && data) { fprintf(stderr, "DEBUG_HASATTR: obj has __data__=%p\n", (void*)data); fflush(stderr); }
    }
    
    return obj->hasAttribute(context, nameStr);
}

static const proto::ProtoObject* py_raise(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    if (positionalParameters->getSize(context) > 0) {
        env->setPendingException(positionalParameters->getAt(context, 0));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_delattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_NONE;
    obj->setAttribute(context, nameObj->asString(context), PROTO_NONE);
    return PROTO_NONE;
}

static const proto::ProtoList* computeC3MRO(proto::ProtoContext* context, const proto::ProtoObject* cls, const proto::ProtoTuple* bases) {
    if (!bases || bases->getSize(context) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* objProto = env ? env->getObjectPrototype() : nullptr;
        fprintf(stderr, "DEBUG computeC3MRO EMPTY BASES. cls=%p objProto=%p\n", (void*)cls, (void*)objProto);
        if (cls == objProto || !objProto) {
            return context->newList()->appendLast(context, cls);
        }
        const proto::ProtoList* res = context->newList()->appendLast(context, cls);
        res = res->appendLast(context, objProto);
        return res;
    }
    
    std::vector<const proto::ProtoList*> mros;
    for (size_t i = 0; i < bases->getSize(context); ++i) {
        const proto::ProtoObject* baseCls = bases->getAt(context, i);
        const proto::ProtoObject* mroAttr = baseCls->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__"));
        const proto::ProtoTuple* tup = nullptr;
        if (mroAttr) {
            tup = mroAttr->asTuple(context);
            if (!tup) {
                PythonEnvironment* env = PythonEnvironment::fromContext(context);
                const proto::ProtoObject* dataAttr = mroAttr->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
                if (dataAttr) tup = dataAttr->asTuple(context);
            }
        }
        if (tup) {
            mros.push_back(tup->asList(context));
        } else {
            mros.push_back(context->newList()->appendLast(context, baseCls));
        }
    }
    
    const proto::ProtoList* basesList = context->newList();
    for (size_t i = 0; i < bases->getSize(context); ++i) basesList = basesList->appendLast(context, bases->getAt(context, i));
    mros.push_back(basesList);
    
    const proto::ProtoList* result = context->newList()->appendLast(context, cls);
    
    while (true) {
        std::vector<const proto::ProtoList*> nonEmptyMros;
        for (auto m : mros) {
            if (m->getSize(context) > 0) nonEmptyMros.push_back(m);
        }
        if (nonEmptyMros.empty()) break;
        
        const proto::ProtoObject* candidate = nullptr;
        for (auto m : nonEmptyMros) {
            candidate = m->getAt(context, 0);
            bool foundInTail = false;
            for (auto m2 : nonEmptyMros) {
                for (size_t i = 1; i < m2->getSize(context); ++i) {
                    if (m2->getAt(context, i) == candidate) {
                        foundInTail = true;
                        break;
                    }
                }
                if (foundInTail) break;
            }
            if (!foundInTail) break;
            candidate = nullptr;
        }
        
        if (!candidate) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseTypeError(context, "Cannot create a consistent method resolution order (MRO) for bases");
            return nullptr;
        }
        
        result = result->appendLast(context, candidate);
        
        for (size_t i = 0; i < mros.size(); ++i) {
            if (mros[i]->getSize(context) > 0 && mros[i]->getAt(context, 0) == candidate) {
                const proto::ProtoList* sliced = context->newList();
                for (size_t j = 1; j < mros[i]->getSize(context); ++j) {
                    sliced = sliced->appendLast(context, mros[i]->getAt(context, j));
                }
                mros[i] = sliced;
            }
        }
    }
    return result;
}

} // namespace builtins

extern const proto::ProtoObject* runUserClassCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

namespace builtins {

static const proto::ProtoObject* py_type_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // type.__init__ generally ignores its arguments as initialization 
    // happens inside py_type (which acts as __new__).
    return PROTO_NONE;
}

const proto::ProtoObject* py_type(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: py_type executing early unconditional: size=%zu\n", positionalParameters ? positionalParameters->getSize(context) : 0);
    }
    
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* typeProto = env ? env->getTypePrototype() : nullptr;
    
    // Decoration: runUserClassCall moved to py_type_call
    if (!positionalParameters || positionalParameters->getSize(context) == 0) {
        return PROTO_NONE;
    }
    
    size_t argCount = positionalParameters->getSize(context);
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: py_type executing unconditional: argCount=%zu\n", argCount);
    }
    // Unconditional print to trace caller
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_type called unconditionally self=%p argCount=%zu\n", (void*)self, argCount);
    }
    
    if (argCount == 1 || argCount == 2) {
        const proto::ProtoObject* obj = (argCount == 2) ? positionalParameters->getAt(context, 1) : positionalParameters->getAt(context, 0);
        if (get_env_diag()) {
            printf("DEBUG: py_type(1/2) obj=%p\n", (void*)obj);
        }
        
        if (obj == PROTO_NONE) return env->getNoneTypePrototype();
        
        return env ? env->getType(context, obj) : obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
    }
    
    if (argCount == 3 || argCount == 4) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_type argCount=%zu self=%p\n", argCount, (void*)self);
        }
        // type(name, bases, dict) (argCount == 3, self is metaclass)
        // type.__new__(cls, name, bases, dict) (argCount == 4)
        const proto::ProtoObject* cls = (argCount == 4) ? positionalParameters->getAt(context, 0) : self;
        size_t baseIdx = (argCount == 4) ? 1 : 0;
        
        if (argCount == 3) {
            if (get_env_diag()) {}
            // Find __new__ on self (the metaclass)
            const proto::ProtoString* py_new = proto::ProtoString::fromUTF8String(context, "__new__");
            const proto::ProtoObject* newMethod = self->getAttribute(context, py_new);
            
            const proto::ProtoObject* typeProto = env ? env->getTypePrototype() : nullptr;
            bool isBaseType = (env && self == typeProto);
            if (!isBaseType && newMethod && newMethod != PROTO_NONE) {
                auto m = newMethod->asMethod(context);
                // Check if the method is the base py_type implementation
                if (m && m != py_type) {
                    const proto::ProtoList* newArgs = context->newList()->appendLast(context, self);
                    for (unsigned long i = 0; i < argCount; ++i) newArgs = newArgs->appendLast(context, positionalParameters->getAt(context, i));
                    return newMethod->call(context, nullptr, py_new, self, newArgs, keywordParameters);
                }
            }
        }

        const proto::ProtoObject* name = positionalParameters->getAt(context, baseIdx + 0);
        const proto::ProtoObject* bases = positionalParameters->getAt(context, baseIdx + 1);
        const proto::ProtoObject* dict = positionalParameters->getAt(context, baseIdx + 2);

        if (get_env_diag()) {
            printf("DEBUG: py_type(3) name=%p bases=%p dict=%p\n", (void*)name, (void*)bases, (void*)dict);
        }

        proto::ProtoObject* targetClass = const_cast<proto::ProtoObject*>(context->newObject(true));
        
        // Add metaclass first so that its attributes are searched after the class MRO bases 
        // (which are added below in reverse order, meaning they are searched before the metaclass).
        if (cls && cls != targetClass) {
            targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(context, cls));
        } else if (env && env->getObjectPrototype()) {
            targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(context, env->getObjectPrototype()));
        }
        
        if (get_env_diag()) {
            std::string n = "unknown";
            if (name && name->isString(context)) name->asString(context)->toUTF8String(context, n);
            fprintf(stderr, "DEBUG: py_type name='%s' targetClass=%p __class__=cls=%p\n", n.c_str(), (void*)targetClass, (void*)cls);
        }
        targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, env ? env->getClassString() : proto::ProtoString::fromUTF8String(context, "__class__"), cls));
        const proto::ProtoString* py_name = env ? env->getNameString() : proto::ProtoString::fromUTF8String(context, "__name__");
        targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, py_name, name));
        
        // NEW RULE: Explicitly mark this as a Python class
        targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__is_python_class__"), PROTO_TRUE));

        
        // Ensure the new class has dictionary storage (for __dict__ and consistency)
        if (env) env->initDictStorage(context, targetClass);

        // Copy dictionary attributes and handle __set_name__
        if (dict) {
            const proto::ProtoString* keysName = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(context, "__keys__");
            const proto::ProtoObject* keysObj = dict->getAttribute(context, keysName);
            const proto::ProtoList* keysList = (keysObj && keysObj->asList(context)) ? keysObj->asList(context) : nullptr;
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG py_type: dict=%p keysObj=%p keysSize=%lu\n", (void*)dict, (void*)keysObj, keysList ? keysList->getSize(context) : 0);
            }
            
            if (get_env_diag()) {
            }

            if (keysList) {
                for (size_t i = 0; i < keysList->getSize(context); ++i) {
                    const proto::ProtoObject* keyObj = keysList->getAt(context, i);

                    if (keyObj && keyObj->isString(context)) {
                        const proto::ProtoString* k = keyObj->asString(context);
                        std::string ks; k->toUTF8String(context, ks);
                        if (get_env_diag()) {
                            printf("DEBUG: py_type copying key iter %zu: '%s'\n", i, ks.c_str());
                            fflush(stdout);
                        }
                        
                        const proto::ProtoObject* val = nullptr;
                        const proto::ProtoString* getItemStr = proto::ProtoString::fromUTF8String(context, "__getitem__");
                        const proto::ProtoObject* getItemMethod = env ? env->getAttribute(context, dict, getItemStr) : dict->getAttribute(context, getItemStr);
                        if (get_env_diag()) {
                            fprintf(stderr, "DEBUG py_type loop: key '%s' dict=%p getItemMethod=%p\n", ks.c_str(), (void*)dict, (void*)getItemMethod);
                        }
                        if (getItemMethod && getItemMethod != PROTO_NONE) {
                            const proto::ProtoList* mArgs = context->newList()->appendLast(context, keyObj);
                            val = protoPython::invokePythonCallable(context, getItemMethod, mArgs, nullptr);
                            if (env && env->hasPendingException()) {
                                if (get_env_diag()) fprintf(stderr, "DEBUG py_type loop: getItem exception caught\n");
                                env->clearPendingException();
                                val = nullptr;
                            } else {
                                if (get_env_diag()) fprintf(stderr, "DEBUG py_type loop: getItem returned %p\n", (void*)val);
                            }
                        }
                        if (!val) { // nullptr ONLY! PROTO_NONE is a valid value.
                            val = dict->getAttribute(context, k);
                            if (get_env_diag()) fprintf(stderr, "DEBUG py_type loop: fallback getAttribute returned %p\n", (void*)val);
                        }
                        
                        // Implicitly wrap special methods
                        if (val && env && val != PROTO_NONE) {
                            const proto::ProtoObject* valType = env->getType(context, val);
                            const proto::ProtoObject* vName = valType ? valType->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__")) : nullptr;
                            std::string tName = "";
                            if (vName && vName->isString(context)) vName->asString(context)->toUTF8String(context, tName);

                            if (ks == "__new__" && tName != "staticmethod") {
                                const proto::ProtoObject* smCls = env->getBuiltins()->getAttribute(context, proto::ProtoString::fromUTF8String(context, "staticmethod"));
                                if (smCls && smCls != PROTO_NONE) {
                                    const proto::ProtoList* smArgs = context->newList()->appendLast(context, smCls)->appendLast(context, val);
                                    val = py_staticmethod(context, nullptr, nullptr, smArgs, nullptr);
                                }
                            } else if ((ks == "__init_subclass__" || ks == "__class_getitem__") && tName != "classmethod") {
                                const proto::ProtoObject* cmCls = env->getBuiltins()->getAttribute(context, proto::ProtoString::fromUTF8String(context, "classmethod"));
                                if (cmCls && cmCls != PROTO_NONE) {
                                    const proto::ProtoList* cmArgs = context->newList()->appendLast(context, cmCls)->appendLast(context, val);
                                    val = py_classmethod(context, nullptr, nullptr, cmArgs, nullptr);
                                }
                            }
                        }
                        
                        // Skip keys that have no actual value in the namespace
                        // (both __getitem__ and getAttribute failed).
                        // Setting nullptr on the class would corrupt inherited attributes.
                        if (!val) {
                            if (get_env_diag()) {
                                std::string kn; k->toUTF8String(context, kn);
                                fprintf(stderr, "DEBUG py_type loop: SKIPPING attr '%s' (val is nullptr)\n", kn.c_str());
                            }
                            continue;
                        }
                        
                        if (get_env_diag()) {
                            std::string kn; k->toUTF8String(context, kn);
                            fprintf(stderr, "DEBUG py_type loop: setting attr %s to val %p\n", kn.c_str(), (void*)val);
                        }
                        targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, k, val));
                        
                        // Update targetClass.__keys__
                        if (keysName) {
                            const proto::ProtoObject* tKeysObj = targetClass->getAttribute(context, keysName);
                            const proto::ProtoList* tKeysList = tKeysObj ? tKeysObj->asList(context) : nullptr;
                            if (tKeysList) {
                                if (!tKeysList->has(context, keyObj)) {
                                     targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, keysName, tKeysList->appendLast(context, keyObj)->asObject(context)));
                                }
                            } else {
                                // Create __keys__ if it doesn't exist on targetClass
                                const proto::ProtoList* newList = context->newList()->appendLast(context, keyObj);
                                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, keysName, newList->asObject(context)));
                            }
                        }
                    }
                }
            } else {
                // Fallback for objects that don't have __keys__ (deprecated/legacy)
                const proto::ProtoSparseList* attrs = dict->getAttributes(context);
                if (attrs) {
                    auto* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(context));
                    while (it && it->hasNext(context)) {
                        unsigned long key = it->nextKey(context);
                        // We can't safely get the string back from the hash easily here without getStringFromHash
                        // so we skip this or use a diagnostics warning.
                        if (std::getenv("PROTO_ENV_DIAG")) {
                        }
                        it = const_cast<proto::ProtoSparseListIterator*>(it->advance(context));
                    }
                }
            }
        }

        // Compute and set __mro__ using C3 linearization
        const proto::ProtoList* mroList = nullptr;
        
        const proto::ProtoTuple* tupleBases = bases ? bases->asTuple(context) : nullptr;
        const proto::ProtoList* listBases = bases && !tupleBases ? bases->asList(context) : nullptr;
        if (bases && !tupleBases && !listBases) {
            const proto::ProtoObject* dataAttr = bases->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
            if (dataAttr) {
                tupleBases = dataAttr->asTuple(context);
                listBases = tupleBases ? nullptr : dataAttr->asList(context);
            }
        }
        
        fprintf(stderr, "DEBUG py_type before MRO: targetClass=%p tupleBases=%p (size=%lu) listBases=%p\n",
                (void*)targetClass, (void*)tupleBases, tupleBases ? tupleBases->getSize(context) : 0, (void*)listBases);
        
        if (tupleBases) {
            mroList = computeC3MRO(context, targetClass, tupleBases);
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__bases__"), bases));
        } else if (listBases) {
            const proto::ProtoTuple* convTup = context->newTupleFromList(listBases);
            mroList = computeC3MRO(context, targetClass, convTup);
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__bases__"), bases));
        } else {
            const proto::ProtoList* emptyBases = context->newList();
            mroList = computeC3MRO(context, targetClass, context->newTupleFromList(emptyBases));
            if (!bases) targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__bases__"), context->newTupleFromList(emptyBases)->asObject(context)));
        }
        
        if (mroList) {
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__"), context->newTupleFromList(mroList)->asObject(context)));
            // Update protoCore 'parents' list natively to reflect the exact same MRO list (minus self) backwards
            // so that getParents() matches C3 precisely for any C++ recursive attributes lookups!
            for (int i = static_cast<int>(mroList->getSize(context)) - 1; i >= 1; --i) {
                const proto::ProtoObject* parent = mroList->getAt(context, i);
                targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(context, parent));
            }
        }

        // Set __module__ if not present
        const proto::ProtoString* py_module = proto::ProtoString::fromUTF8String(context, "__module__");
        if (targetClass->hasOwnAttribute(context, py_module) != PROTO_TRUE) {
            const proto::ProtoObject* globals = env ? env->getCurrentGlobals() : nullptr;
            const proto::ProtoObject* moduleName = globals ? globals->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__")) : nullptr;
            if (moduleName) {
                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, py_module, moduleName));
            }
        }
        
        if (std::getenv("PROTO_ENV_DIAG")) {
             printf("DEBUG: py_type created class %p check MRO\n", (void*)targetClass);
             const proto::ProtoList* mroList = const_cast<proto::ProtoObject*>(targetClass)->getParents(context);
             if (mroList) {
                 printf("DEBUG: py_type MRO size=%zu\n", mroList->getSize(context));
                 for (size_t i = 0; i < mroList->getSize(context); ++i) {
                     printf("DEBUG: py_type MRO [%zu] = %p\n", i, (void*)mroList->getAt(context, i));
                 }
             } else {
                 printf("DEBUG: py_type MRO is NULL\n");
             }
        }
        
        // Final Pass: Call __set_name__ for all attributes that have it.
        // This MUST be done after the targetClass has been fully assembled,
        // so that descriptors can access other attributes (e.g. enum._member_type_)
        const proto::ProtoString* targetKeysName = proto::ProtoString::fromUTF8String(context, "__keys__");
        const proto::ProtoObject* tKeysObj = targetClass->getAttribute(context, targetKeysName);
        if (tKeysObj && tKeysObj->asList(context)) {
            const proto::ProtoList* tKeysList = tKeysObj->asList(context);
            for (size_t i = 0; i < tKeysList->getSize(context); ++i) {
                const proto::ProtoObject* keyObj = tKeysList->getAt(context, i);
                if (keyObj && keyObj->isString(context)) {
                    const proto::ProtoObject* val = targetClass->getAttribute(context, keyObj->asString(context));
                    if (val && val != PROTO_NONE) {
                        PythonEnvironment* pe = PythonEnvironment::fromContext(context);
                        const proto::ProtoObject* valType = pe ? pe->getType(context, val) : nullptr;
                        const proto::ProtoObject* setName = valType ? valType->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__set_name__")) : nullptr;
                        if (setName && setName != PROTO_NONE) {
                            if (get_env_diag()) {
                                std::string ks; keyObj->asString(context)->toUTF8String(context, ks);
                                std::string fnStr = PythonEnvironment::reprObject(context, setName);
                                printf("DEBUG: py_type calling __set_name__ on key '%s' with method=%s\n", ks.c_str(), fnStr.c_str());
                                fflush(stdout);
                            }
                            const proto::ProtoList* setNameArgs = context->newList()->appendLast(context, val)->appendLast(context, targetClass)->appendLast(context, keyObj);
                            protoPython::invokePythonCallable(context, setName, setNameArgs, nullptr);
                            if (pe && pe->hasPendingException()) return nullptr;
                        }
                    }
                }
            }
        }
        
        return targetClass;
    }

    return PROTO_NONE;
}

static bool checkInterfaceInstanceOf(proto::ProtoContext* context, const proto::ProtoObject* obj, const proto::ProtoObject* cls) {
    if (cls->isTuple(context)) {
        const proto::ProtoTuple* tup = cls->asTuple(context);
        for (size_t i = 0; i < tup->getSize(context); ++i) {
            if (checkInterfaceInstanceOf(context, obj, tup->getAt(context, i))) return true;
        }
        return false;
    }
    if (obj == cls) return true;
    return obj->isInstanceOf(context, cls) == PROTO_TRUE;
}

static const proto::ProtoObject* resolveClassType(protoPython::PythonEnvironment* env, const proto::ProtoObject* self, proto::ProtoContext* context, const proto::ProtoObject* cls) {
    if (!env || !self) return cls;
    const proto::ProtoObject* typeAttr = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "type"));
    const proto::ProtoObject* listAttr = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "list"));
    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG resolveClassType cls=%p self=%p listAttr=%p typeAttr=%p\n", (void*)cls, (void*)self, (void*)listAttr, (void*)typeAttr);
    if (cls == typeAttr) {
        return env->getTypePrototype();
    }
    if (cls == listAttr) return env->getListPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "tuple"))) return env->getTuplePrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "dict"))) return env->getDictPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "int"))) return env->getIntPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "float"))) return env->getFloatPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "str"))) return env->getStrPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bytes"))) return env->getBytesPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bytearray"))) return env->getBytesPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "set"))) return env->getSetPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "frozenset"))) return env->getFrozensetPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"))) return env->getBoolPrototype();
    if (cls == self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "complex"))) return env->getComplexPrototype();
    return cls;
}

static bool py_issubclass_check_single(proto::ProtoContext* context, const proto::ProtoObject* cls, const proto::ProtoObject* base);

static const proto::ProtoObject* py_isinstance(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 1);
    cls = resolveClassType(env, self, context, cls);
    
    if (obj == PROTO_TRUE || obj == PROTO_FALSE) {
        const proto::ProtoObject* boolType = env ? env->getBoolPrototype() : nullptr;
        const proto::ProtoObject* intType = env ? env->getIntPrototype() : nullptr;
        if (cls == boolType || cls == intType) return PROTO_TRUE;
        if (intType && checkInterfaceInstanceOf(context, intType, cls)) return PROTO_TRUE;
    }
    
    if (checkInterfaceInstanceOf(context, obj, cls)) {
        return PROTO_TRUE;
    }

    // Check __class__ attribute or prototype if native parent link failed
    const proto::ProtoString* classStr = env ? env->getClassString() : proto::ProtoString::fromUTF8String(context, "__class__");
    const proto::ProtoObject* objClass = obj->getAttribute(context, classStr);
    if (!objClass) objClass = obj->getPrototype(context);
    
    
    if (objClass && objClass != obj) {
        if (py_issubclass_check_single(context, objClass, cls)) return PROTO_TRUE;
    }

    return PROTO_FALSE;
}

static bool py_issubclass_check_single(proto::ProtoContext* context, const proto::ProtoObject* cls, const proto::ProtoObject* base) {
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG py_issubclass_check_single: cls=%p base=%p\n", (void*)cls, (void*)base);
    }
    if (cls == base) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG py_issubclass_check_single returns True (cls == base)\n");
        return true;
    }

    // Fast path: use __mro__
    const proto::ProtoObject* mro = cls->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__"));
    if (mro && (mro->asList(context) || mro->asTuple(context))) {
        const proto::ProtoList* mroList = mro->asList(context);
        const proto::ProtoTuple* mroTuple = mro->asTuple(context);
        unsigned long size = mroList ? mroList->getSize(context) : (mroTuple ? mroTuple->getSize(context) : 0);
        for (unsigned long i = 0; i < size; ++i) {
            const proto::ProtoObject* item = mroList ? mroList->getAt(context, i) : mroTuple->getAt(context, i);
            if (item == base) {
                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG py_issubclass_check_single returns True (in mro)\n");
                return true;
            }
        }
        // If __mro__ is present and valid, it contains the entire hierarchy.
        // We do not need to check __bases__ recursively, which causes exponential blowup.
        return false;
    }

    // Fallback: check __bases__ recursively (only if __mro__ is absent or invalid)
    const proto::ProtoObject* bases = cls->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__bases__"));
    if (bases) {
        const proto::ProtoList* basesList = bases->asList(context);
        const proto::ProtoTuple* basesTuple = bases->asTuple(context);
        unsigned long size = basesList ? basesList->getSize(context) : (basesTuple ? basesTuple->getSize(context) : 0);
        for (unsigned long i = 0; i < size; ++i) {
            const proto::ProtoObject* item = basesList ? basesList->getAt(context, i) : basesTuple->getAt(context, i);
            if (py_issubclass_check_single(context, item, base)) {
                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG py_issubclass_check_single returns True (in __bases__)\n");
                return true;
            }
        }
    }
    
    // Abstract Base Classes subclass hook fallback:
    // If base has __subclasscheck__, we should ideally call it. For native py_issubclass, 
    // the surrounding Python code in _abc.py or equivalent manages __subclasscheck__.
    // To strictly implement issubclass, we defer to __mro__.
    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG py_issubclass_check_single returns False\n");
    return false;
}

static const proto::ProtoObject* py_issubclass(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* base = positionalParameters->getAt(context, 1);
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG py_issubclass: entry cls=%p base=%p\n", (void*)cls, (void*)base);
    }
    
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    cls = resolveClassType(env, self, context, cls);
    base = resolveClassType(env, self, context, base);
    
    // In Python, if base is a tuple, we must check if cls is a subclass of ANY element in the tuple
    if (base) {
         const proto::ProtoList* baseList = base->asList(context);
         if (!baseList && base->asTuple(context)) baseList = base->asTuple(context)->asList(context);
         if (baseList) {
             for (unsigned long i = 0; i < baseList->getSize(context); ++i) {
                  if (py_issubclass_check_single(context, cls, baseList->getAt(context, i))) {
                       return PROTO_TRUE;
                  }
             }
             return PROTO_FALSE;
         }
    }
    
    return py_issubclass_check_single(context, cls, base) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_abs(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* absM = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__abs__"));
    if (absM && absM->asMethod(context)) {
        return absM->call(context, nullptr, nullptr, obj, context->newList(), nullptr);
    }
    if (obj->isInteger(context)) {
        long long v = obj->asLong(context);
        return context->fromInteger(v < 0 ? -v : v);
    }
    if (obj->isDouble(context)) {
        return context->fromDouble(std::abs(obj->asDouble(context)));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_min_max(
    proto::ProtoContext* context,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters,
    bool isMax) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    
    const proto::ProtoObject* keyFunc = nullptr;
    if (keywordParameters) {
        const proto::ProtoString* keyS = proto::ProtoString::fromUTF8String(context, "key");
        keyFunc = keywordParameters->getAt(context, keyS->getHash(context));
    }

    std::vector<const proto::ProtoObject*> items;
    if (positionalParameters->getSize(context) == 1) {
        // Handle iterable
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
        const proto::ProtoObject* iterObj = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
        if (iterObj && iterObj != PROTO_NONE) {
            ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
            while (true) {
                const proto::ProtoObject* item = py_next(context, nullptr, nullptr, context->newList()->appendLast(context, iterObj), nullptr);
                if (!item) {
                    if (env && env->handleExhaustion(context)) break;
                    return nullptr;
                }
                if (item == PROTO_NONE) break;
                items.push_back(item);
            }
        }
    } else {
        for (size_t i = 0; i < positionalParameters->getSize(context); ++i) {
            items.push_back(positionalParameters->getAt(context, i));
        }
    }

    if (items.empty()) return PROTO_NONE;

    const proto::ProtoObject* bestItem = items[0];
    const proto::ProtoObject* bestVal = bestItem;
    if (keyFunc && keyFunc != PROTO_NONE) {
        bestVal = keyFunc->call(context, nullptr, nullptr, keyFunc, context->newList()->appendLast(context, bestItem), nullptr);
    }

    for (size_t i = 1; i < items.size(); ++i) {
        const proto::ProtoObject* currentItem = items[i];
        const proto::ProtoObject* currentVal = currentItem;
        if (keyFunc && keyFunc != PROTO_NONE) {
            currentVal = keyFunc->call(context, nullptr, nullptr, keyFunc, context->newList()->appendLast(context, currentItem), nullptr);
        }

        // Use sorted_compare or similar?
        // For simplicity, let's assume integers for now or use the environment's comparison.
        bool better = false;
        if (currentVal->isInteger(context) && bestVal->isInteger(context)) {
            better = isMax ? (currentVal->asLong(context) > bestVal->asLong(context)) : (currentVal->asLong(context) < bestVal->asLong(context));
        } else {
            // Fallback to basic comparison? 
            // Better: use the compare system if available.
        }
        
        if (better) {
            bestItem = currentItem;
            bestVal = currentVal;
        }
    }

    return bestItem;
}

static const proto::ProtoObject* py_min(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return py_min_max(context, positionalParameters, keywordParameters, false);
}

static const proto::ProtoObject* py_max(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return py_min_max(context, positionalParameters, keywordParameters, true);
}

static const proto::ProtoObject* py_pow(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    unsigned long n = positionalParameters->getSize(context);
    if (n < 2) return PROTO_NONE;
    long long base = positionalParameters->getAt(context, 0)->asLong(context);
    long long exp = positionalParameters->getAt(context, 1)->asLong(context);
    bool hasMod = n >= 3;
    long long mod = hasMod ? positionalParameters->getAt(context, 2)->asLong(context) : 0;
    if (hasMod && mod == 0) return PROTO_NONE;
    if (exp < 0) return PROTO_NONE;
    long long result = 1;
    long long b = base;
    long long e = exp;
    if (hasMod) {
        b = ((b % mod) + mod) % mod;
        while (e > 0) {
            if (e & 1) result = (result * b) % mod;
            b = (b * b) % mod;
            e >>= 1;
        }
        return context->fromInteger(result);
    }
    while (e > 0) {
        if (e & 1) result *= b;
        b *= b;
        e >>= 1;
    }
    return context->fromInteger(result);
}

static const proto::ProtoObject* py_divmod(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    long long a = positionalParameters->getAt(context, 0)->asLong(context);
    long long b = positionalParameters->getAt(context, 1)->asLong(context);
    if (b == 0) return PROTO_NONE;
    long long quot = a / b;
    long long rem = a % b;
    if (rem != 0) {
        if (b > 0 && rem < 0) { quot--; rem += b; }
        else if (b < 0 && rem > 0) { quot++; rem -= b; }
    }
    const proto::ProtoList* pair = context->newList()
        ->appendLast(context, context->fromInteger(quot))
        ->appendLast(context, context->fromInteger(rem));
    return context->newTupleFromList(pair)->asObject(context);
}

static const proto::ProtoObject* py_ascii(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__"));
    if (!reprMethod || !reprMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* reprObj = reprMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    if (!reprObj || !reprObj->isString(context)) return PROTO_NONE;
    std::string s;
    reprObj->asString(context)->toUTF8String(context, s);
    
    std::string out;
    for (unsigned char c : s) {
        if (c >= 32 && c < 127) {
            out += c;
        } else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_ord(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isString(context)) return PROTO_NONE;
    std::string s;
    arg->asString(context)->toUTF8String(context, s);
    if (s.empty()) return PROTO_NONE;
    unsigned char first = static_cast<unsigned char>(s[0]);
    if ((first & 0x80) == 0)
        return context->fromInteger(static_cast<long long>(first));
    if ((first & 0xE0) == 0xC0 && s.size() >= 2) {
        long long cp = (first & 0x1F) << 6 | (static_cast<unsigned char>(s[1]) & 0x3F);
        return context->fromInteger(cp);
    }
    if ((first & 0xF0) == 0xE0 && s.size() >= 3) {
        long long cp = (first & 0x0F) << 12 | (static_cast<unsigned char>(s[1]) & 0x3F) << 6 | (static_cast<unsigned char>(s[2]) & 0x3F);
        return context->fromInteger(cp);
    }
    if ((first & 0xF8) == 0xF0 && s.size() >= 4) {
        long long cp = (first & 0x07) << 18 | (static_cast<unsigned char>(s[1]) & 0x3F) << 12
            | (static_cast<unsigned char>(s[2]) & 0x3F) << 6 | (static_cast<unsigned char>(s[3]) & 0x3F);
        return context->fromInteger(cp);
    }
    return context->fromInteger(static_cast<long long>(first));
}

static const proto::ProtoObject* py_property_get(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj == PROTO_NONE) return self;
    const proto::ProtoObject* fget = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "fget"));
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG py_property_get: fget=%p\n", (void*)fget);
    }
    if (fget && fget != PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::vector<const proto::ProtoObject*> argsVec = {obj};
            const proto::ProtoObject* res = env->callObject(fget, argsVec);
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG py_property_get: callObject res=%p\n", (void*)res);
            }
            return res ? res : PROTO_NONE;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_property_set(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* val = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* fset = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "fset"));
    if (fset && fset != PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::vector<const proto::ProtoObject*> argsVec = {obj, val};
            env->callObject(fset, argsVec);
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_property_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // property.__init__ ignores its positional and keyword parameters.
    // Initialization is already done natively in __new__.
    return PROTO_NONE;
}

static const proto::ProtoObject* py_property(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0); // property class
    
    // Create new instance of cls natively
    proto::ProtoObject* prop = const_cast<proto::ProtoObject*>(cls->newChild(context, true));
    
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        prop->setAttribute(context, env->getClassString(), cls);
    } else {
        prop->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), cls);
    }
    
    if (positionalParameters->getSize(context) >= 2) {
        prop->setAttribute(context, proto::ProtoString::fromUTF8String(context, "fget"), positionalParameters->getAt(context, 1));
    }
    if (positionalParameters->getSize(context) >= 3) {
        prop->setAttribute(context, proto::ProtoString::fromUTF8String(context, "fset"), positionalParameters->getAt(context, 2));
    }
    if (positionalParameters->getSize(context) >= 4) {
        prop->setAttribute(context, proto::ProtoString::fromUTF8String(context, "fdel"), positionalParameters->getAt(context, 3));
    }
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG py_property (__new__): prop=%p cls=%p fget=%p\n", (void*)prop, (void*)cls, (void*)(positionalParameters->getSize(context) >= 2 ? positionalParameters->getAt(context, 1) : nullptr));
    }
    return prop;
}

} // namespace builtins
const proto::ProtoObject* runBoundMethodCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);
namespace builtins {

static const proto::ProtoObject* py_classmethod_get(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* type = positionalParameters->getAt(context, 1);
    
    if (get_env_diag()) {
        std::string tr = "unknown";
        if (type && type != PROTO_NONE) tr = PythonEnvironment::reprObject(context, type);
        fprintf(stderr, "DEBUG py_classmethod_get: extracted type=%p repr=%s\n", (void*)type, tr.c_str());
    }

    const proto::ProtoObject* func = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "func"));
    if (!func || func == PROTO_NONE) return PROTO_NONE;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* bound = context->newObject(false);
    if (env && env->getMethodPrototype()) {
        bound = const_cast<proto::ProtoObject*>(bound->addParent(context, env->getMethodPrototype()));
        bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, env->getClassString(), env->getMethodPrototype()));
    }
    
    // Set __self__ (the instance, which is the class for classmethods)
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__self__"), type));
    
    // Set __func__ (the original function)
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__func__"), func));
    
    // Set __call__ to delegate to runBoundMethodCall
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__"),
                               context->fromMethod(const_cast<proto::ProtoObject*>(bound), runBoundMethodCall)));
    
    return bound;
}

static const proto::ProtoObject* py_classmethod(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    
    // Create new instance of cls natively
    proto::ProtoObject* cm = const_cast<proto::ProtoObject*>(cls->newChild(context, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        cm->setAttribute(context, env->getClassString(), cls);
    } else {
        cm->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), cls);
    }
    
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* func = positionalParameters->getAt(context, 1);
        cm->setAttribute(context, proto::ProtoString::fromUTF8String(context, "func"), func);
        cm->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__func__"), func);
    }
    return cm;
}

static const proto::ProtoObject* py_staticmethod_get(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "func"));
}

static const proto::ProtoObject* py_staticmethod(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    
    proto::ProtoObject* sm = const_cast<proto::ProtoObject*>(cls->newChild(context, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        sm->setAttribute(context, env->getClassString(), cls);
    } else {
        sm->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), cls);
    }
    
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* func = positionalParameters->getAt(context, 1);
        sm->setAttribute(context, proto::ProtoString::fromUTF8String(context, "func"), func);
        sm->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__func__"), func);
    }
    return sm;
}

static const proto::ProtoObject* py_chr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i < 0 || i > 0x10FFFF) return PROTO_NONE;
    char buf[8];
    int n = 0;
    if (i <= 0x7F) {
        buf[n++] = static_cast<char>(i);
    } else if (i <= 0x7FF) {
        buf[n++] = static_cast<char>(0xC0 | (i >> 6));
        buf[n++] = static_cast<char>(0x80 | (i & 0x3F));
    } else if (i <= 0xFFFF) {
        buf[n++] = static_cast<char>(0xE0 | (i >> 12));
        buf[n++] = static_cast<char>(0x80 | ((i >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (i & 0x3F));
    } else {
        buf[n++] = static_cast<char>(0xF0 | (i >> 18));
        buf[n++] = static_cast<char>(0x80 | ((i >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((i >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (i & 0x3F));
    }
    buf[n] = '\0';
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_bin(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i == 0) return context->fromUTF8String("0b0");
    std::string s = "0b";
    unsigned long long u;
    if (i < 0) {
        s += "-";
        u = static_cast<unsigned long long>(-i);
    } else {
        u = static_cast<unsigned long long>(i);
    }
    std::string bits;
    while (u) { bits += (u & 1) ? '1' : '0'; u >>= 1; }
    for (auto it = bits.rbegin(); it != bits.rend(); ++it) s += *it;
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_oct(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i == 0) return context->fromUTF8String("0o0");
    char buf[32];
    if (i < 0)
        snprintf(buf, sizeof(buf), "-0o%llo", static_cast<unsigned long long>(-i));
    else
        snprintf(buf, sizeof(buf), "0o%llo", static_cast<unsigned long long>(i));
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_hex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i == 0) return context->fromUTF8String("0x0");
    char buf[24];
    if (i < 0)
        snprintf(buf, sizeof(buf), "-0x%llx", static_cast<unsigned long long>(-i));
    else
        snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(i));
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_round(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* n = positionalParameters->getAt(context, 0);
    int ndigits = 0;
    if (positionalParameters->getSize(context) >= 2) {
        ndigits = static_cast<int>(positionalParameters->getAt(context, 1)->asLong(context));
    }
    double d = n->isDouble(context) ? n->asDouble(context) : static_cast<double>(n->asLong(context));
    if (ndigits > 0) {
        for (int i = 0; i < ndigits; ++i) d *= 10.0;
        d = std::round(d);
        for (int i = 0; i < ndigits; ++i) d /= 10.0;
    } else if (ndigits < 0) {
        double power = std::pow(10.0, -ndigits);
        d = std::round(d / power) * power;
    } else {
        d = std::round(d);
    }
    return context->fromDouble(d);
}

static const proto::ProtoObject* py_range_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    
    // Fast path removed to adhere to architectural rules.

    // Fallback for old range objects
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* curS = env ? env->getRangeCurString() : proto::ProtoString::fromUTF8String(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : proto::ProtoString::fromUTF8String(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : proto::ProtoString::fromUTF8String(context, "__range_step__");

    const proto::ProtoObject* curObj = self->getAttribute(context, curS);
    const proto::ProtoObject* stopObj = self->getAttribute(context, stopS);
    const proto::ProtoObject* stepObj = self->getAttribute(context, stepS);
    if (!curObj || !stopObj || !stepObj) return nullptr;
    long long cur = curObj->asLong(context);
    long long stop = stopObj->asLong(context);
    long long step = stepObj->asLong(context);

    if ((step > 0 && cur >= stop) || (step < 0 && cur <= stop)) return nullptr;

    self->setAttribute(context, curS, context->fromInteger(cur + step));
    return context->fromInteger(cur);
}

static const proto::ProtoObject* py_range_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* curS = env ? env->getRangeCurString() : proto::ProtoString::fromUTF8String(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : proto::ProtoString::fromUTF8String(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : proto::ProtoString::fromUTF8String(context, "__range_step__");

    const proto::ProtoObject* curObj = self->getAttribute(context, curS);
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: py_range_iter called with self=%p, curObj=%p\n", (void*)self, (void*)curObj);
        fflush(stderr);
    }
    if (!curObj) return PROTO_NONE;
    long long start = curObj->asLong(context);
    long long stop = self->getAttribute(context, stopS)->asLong(context);
    long long step = self->getAttribute(context, stepS)->asLong(context);

    // Create a standard Python object as iterator using the rangeIteratorProto
    const proto::ProtoObject* rangeIterProto = env ? env->getRangeIteratorProto() : nullptr;
    const proto::ProtoObject* iterObj = rangeIterProto ? rangeIterProto->newChild(context, true) : context->newObject(false);
    
    // Store original range values in the iterator
    iterObj = iterObj->setAttribute(context, curS, context->fromInteger(start));
    iterObj = iterObj->setAttribute(context, stopS, context->fromInteger(stop));
    iterObj = iterObj->setAttribute(context, stepS, context->fromInteger(step));
    
    return iterObj;
}

static const proto::ProtoObject* py_range(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    
    long long start = 0;
    long long stop = 0;
    long long step = 1;

    unsigned long argsSize = positionalParameters->getSize(context);
    if (argsSize == 1) {
        const proto::ProtoObject* stopObj = positionalParameters->getAt(context, 0);
        if (!stopObj->isInteger(context)) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseTypeError(context, "range() integer stop argument expected");
            return PROTO_NONE;
        }
        stop = stopObj->asLong(context);
    } else if (argsSize >= 2) {
        const proto::ProtoObject* startObj = positionalParameters->getAt(context, 0);
        const proto::ProtoObject* stopObj = positionalParameters->getAt(context, 1);
        if (!startObj->isInteger(context) || !stopObj->isInteger(context)) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseTypeError(context, "range() integer arguments expected");
            return PROTO_NONE;
        }
        start = startObj->asLong(context);
        stop = stopObj->asLong(context);
        if (argsSize >= 3) {
            const proto::ProtoObject* stepObj = positionalParameters->getAt(context, 2);
            if (!stepObj->isInteger(context)) {
                PythonEnvironment* env = PythonEnvironment::fromContext(context);
                if (env) env->raiseTypeError(context, "range() integer step argument expected");
                return PROTO_NONE;
            }
            step = stepObj->asLong(context);
        }
    }

    if (step == 0) return PROTO_NONE;

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* curS = env ? env->getRangeCurString() : proto::ProtoString::fromUTF8String(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : proto::ProtoString::fromUTF8String(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : proto::ProtoString::fromUTF8String(context, "__range_step__");

    const proto::ProtoObject* rangeObj = self ? self->newChild(context, true) : context->newObject(false);
    rangeObj = rangeObj->setAttribute(context, curS, context->fromInteger(start));
    rangeObj = rangeObj->setAttribute(context, stopS, context->fromInteger(stop));
    rangeObj = rangeObj->setAttribute(context, stepS, context->fromInteger(step));
    
    return rangeObj;
}

static const proto::ProtoObject* py_zip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    unsigned long n = positionalParameters->getSize(context);
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* zipProtoS = env ? env->getZipProtoString() : proto::ProtoString::fromUTF8String(context, "__zip_proto__");
    const proto::ProtoString* zipItersS = env ? env->getZipItersString() : proto::ProtoString::fromUTF8String(context, "__zip_iters__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    if (n < 2) return PROTO_NONE; // map __new__ is called with at least map, iterable
    const proto::ProtoList* itersList = context->newList();
    for (unsigned long i = 1; i < n; ++i) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* it = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
        if (!it || it == noneObj) return PROTO_NONE;
        itersList = itersList->appendLast(context, it);
    }
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* zipObj = cls->newChild(context, true);
    zipObj = zipObj->setAttribute(context, zipItersS, itersList->asObject(context));
    return zipObj;
}

static const proto::ProtoObject* py_zip_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* itersS = env ? env->getZipItersString() : proto::ProtoString::fromUTF8String(context, "__zip_iters__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* itersObj = self->getAttribute(context, itersS);
    if (!itersObj || !itersObj->asList(context)) return nullptr;
    const proto::ProtoList* iters = itersObj->asList(context);
    unsigned long n = iters->getSize(context);
    if (n == 0) return nullptr;

    const proto::ProtoList* resList = context->newList();
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* it = iters->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* nextM = it ? it->getAttribute(context, nextS) : nullptr;
        if (!nextM || !nextM->asMethod(context)) return nullptr;
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
        if (!val) return nullptr;
        resList = resList->appendLast(context, val);
    }
    return context->newTupleFromList(resList)->asObject(context);
}

static const proto::ProtoObject* py_filter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 3) return PROTO_NONE;
    const proto::ProtoObject* func = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 2);
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* filterProtoS = env ? env->getFilterProtoString() : proto::ProtoString::fromUTF8String(context, "__filter_proto__");
    const proto::ProtoString* boolTypeS = env ? env->getBoolTypeNameString() : proto::ProtoString::fromUTF8String(context, "bool");
    const proto::ProtoString* filterFuncS = env ? env->getFilterFuncString() : proto::ProtoString::fromUTF8String(context, "__filter_func__");
    const proto::ProtoString* filterIterS = env ? env->getFilterIterString() : proto::ProtoString::fromUTF8String(context, "__filter_iter__");
    const proto::ProtoString* filterBoolS = env ? env->getFilterBoolString() : proto::ProtoString::fromUTF8String(context, "__filter_bool__");
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    const proto::ProtoObject* it = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
    if (!it || it == noneObj) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* boolType = env ? env->getBuiltins()->getAttribute(context, boolTypeS) : PROTO_NONE;
    const proto::ProtoObject* filterObj = cls->newChild(context, true);
    filterObj = filterObj->setAttribute(context, filterFuncS, func);
    filterObj = filterObj->setAttribute(context, filterIterS, it);
    if (boolType && boolType != PROTO_NONE) filterObj = filterObj->setAttribute(context, filterBoolS, boolType);
    return filterObj;
}

static bool filter_is_truthy(proto::ProtoContext* context, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;
    if (obj->isInteger(context)) return obj->asLong(context) != 0;
    if (obj->isDouble(context)) return obj->asDouble(context) != 0.0;
    return true;
}

static const proto::ProtoObject* py_filter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* funcS = env ? env->getFilterFuncString() : proto::ProtoString::fromUTF8String(context, "__filter_func__");
    const proto::ProtoString* iterS = env ? env->getFilterIterString() : proto::ProtoString::fromUTF8String(context, "__filter_iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");

    const proto::ProtoObject* func = self->getAttribute(context, funcS);
    const proto::ProtoObject* it = self->getAttribute(context, iterS);
    if (!func || !it) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: py_map_next failing: func=%p it=%p\n", (void*)func, (void*)it);
            fflush(stderr);
        }
        return nullptr;
    }
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!nextM || !nextM->asMethod(context)) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: py_map_next failing: it=%p nextM=%p\n", (void*)it, (void*)nextM);
            fflush(stderr);
        }
        return nullptr;
    }

    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
        if (!val) return nullptr;
        
        const proto::ProtoObject* result = env ? env->callObject(func, {val}) : nullptr;
    }
}

static const proto::ProtoObject* py_ignore_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // __init__ ignores its parameters for native types that handle initialization in __new__
    return PROTO_NONE;
}

static const proto::ProtoObject* py_map(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 3) return PROTO_NONE;
    const proto::ProtoObject* func = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 2);
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* mapProtoS = env ? env->getMapProtoString() : proto::ProtoString::fromUTF8String(context, "__map_proto__");
    const proto::ProtoString* mapFuncS = env ? env->getMapFuncString() : proto::ProtoString::fromUTF8String(context, "__map_func__");
    const proto::ProtoString* mapIterS = env ? env->getMapIterString() : proto::ProtoString::fromUTF8String(context, "__map_iter__");
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    const proto::ProtoObject* it = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
    if (!it || it == noneObj) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_map failing: py_iter returned None or nullptr\n");
        return PROTO_NONE;
    }
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* mapObj = cls->newChild(context, true);
    mapObj = mapObj->setAttribute(context, mapFuncS, func);
    mapObj = mapObj->setAttribute(context, mapIterS, it);
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: py_map created mapObj=%p func=%p iter=%p\n", (void*)mapObj, (void*)func, (void*)it);
        fflush(stderr);
    }
    return mapObj;
}

static const proto::ProtoObject* py_map_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* funcS = env ? env->getMapFuncString() : proto::ProtoString::fromUTF8String(context, "__map_func__");
    const proto::ProtoString* iterS = env ? env->getMapIterString() : proto::ProtoString::fromUTF8String(context, "__map_iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* func = self->getAttribute(context, funcS);
    const proto::ProtoObject* it = self->getAttribute(context, iterS);
    if (!func || !it) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: py_map_next failing: func=%p it=%p\n", (void*)func, (void*)it);
            fflush(stderr);
        }
        return nullptr;
    }
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!nextM || !nextM->asMethod(context)) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: py_map_next failing: it=%p nextM=%p\n", (void*)it, (void*)nextM);
            fflush(stderr);
        }
        return nullptr;
    }
    
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
    if (!val) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: py_map_next: it=%p __next__ returned nullptr (end of iteration)\n", (void*)it);
            fflush(stderr);
        }
        return nullptr;
    }
    
    const proto::ProtoObject* res = env ? env->callObject(func, {val}) : nullptr;
    if (!res && env && env->hasPendingException()) {
        return nullptr;
    }
    return res;
}

// Forward declare for exposure
const proto::ProtoObject* py_object_new(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

const proto::ProtoObject* py_object_new(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) {
        // TODO: Raise TypeError
        return PROTO_NONE; 
    }
    // First argument is cls
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    
    // Create new instance of cls natively
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(cls->newChild(context, true));
    
    // Set __class__ to cls explicitly natively
    obj = const_cast<proto::ProtoObject*>(obj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), cls));
    
    // Initialize properties tracking specifically dictionary 
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    if (env) {
        env->initDictStorage(context, obj);
    }
    
    return obj;
}

const proto::ProtoObject* py_bytearray_fallback(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_bytearray_fallback called\n");
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env && env->getBytesPrototype()) {
        proto::ProtoObject* b = const_cast<proto::ProtoObject*>(env->getBytesPrototype()->newChild(ctx, true));
        b->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), proto::ProtoString::fromUTF8String(ctx, "")->asObject(ctx));
        return b;
    }
    return proto::ProtoString::fromUTF8String(ctx, "")->asObject(ctx);
}


const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                   const proto::ProtoObject* typeProto, const proto::ProtoObject* intProto,
                                   const proto::ProtoObject* strProto, const proto::ProtoObject* listProto,
                                   const proto::ProtoObject* dictProto, const proto::ProtoObject* tupleProto,
                                   const proto::ProtoObject* setProto, const proto::ProtoObject* bytesProto,
                                   const proto::ProtoObject* noneProto,
                                   const proto::ProtoObject* ellipsisProto,
                                   const proto::ProtoObject* notImplementedProto,
                                   const proto::ProtoObject* sliceType, const proto::ProtoObject* frozensetProto,
                                    const proto::ProtoObject* floatProto, const proto::ProtoObject* boolProto,
                                   const proto::ProtoObject* complexProto,
                                   const proto::ProtoObject* ioModule) {
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG BUILTINS INIT: passed objectProto=%p typeProto=%p intProto=%p\n", (void*)objectProto, (void*)typeProto, (void*)intProto);
    }
    const proto::ProtoObject* builtins = ctx->newObject(false);
    if (objectProto) builtins = builtins->addParent(ctx, objectProto);
    if (noneProto) {
        builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "None"), noneProto);
    }
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG IN BUILTINS INIT: 1 obInB=%p\n", (void*)builtins->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object")));
    }
    if (ioModule && ioModule != PROTO_NONE) {
        builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__io_module__"), ioModule);
    }
    
    // Initialize dummy gc module
    const proto::ProtoObject* gcModule = ctx->newObject(false);
    if (objectProto) gcModule = gcModule->addParent(ctx, objectProto);
    gcModule = gcModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("gc"));
    gcModule = gcModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "collect"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_collect));
    gcModule = gcModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isenabled"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_isenabled));
    gcModule = gcModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "disable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_isenabled));
    gcModule = gcModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "enable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_isenabled));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "gc"), gcModule);

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "True"), PROTO_TRUE);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "False"), PROTO_FALSE);
    
    // Initialize Ellipsis
    const proto::ProtoObject* ellipsis = ellipsisProto;
    if (!ellipsis) {
        ellipsis = ctx->newObject(false);
        if (objectProto) ellipsis = ellipsis->addParent(ctx, objectProto);
        ellipsis = ellipsis->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repr__"), ctx->fromUTF8String("Ellipsis"));
    }
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "Ellipsis"), ellipsis);

    // Initialize NotImplemented
    const proto::ProtoObject* notImpl = notImplementedProto;
    if (!notImpl) {
        notImpl = ctx->newObject(false);
        if (objectProto) notImpl = notImpl->addParent(ctx, objectProto);
        notImpl = notImpl->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repr__"), ctx->fromUTF8String("NotImplemented"));
    }
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "NotImplemented"), notImpl);

    if (objectProto) {
        const proto::ProtoString* s_setattr = proto::ProtoString::fromUTF8String(ctx, "__setattr__");
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, s_setattr, ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_setattr));
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__getattribute__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_getattribute));
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, env ? env->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_init));
        // Update the space's objectPrototype!
        ctx->space->objectPrototype = const_cast<proto::ProtoObject*>(objectProto);
    }
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object"), objectProto);
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG IN BUILTINS INIT: 2 (AFTER SET) obInB=%p\n", (void*)builtins->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object")));
    }
    if (typeProto) {
        if (get_env_diag()) {
            printf("DEBUG: Registering 'type' using typeProto=%p\n", (void*)typeProto);
        }
        builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "type"), typeProto);
    } else {
        if (get_env_diag()) {
            printf("DEBUG: Registering 'type' using fallback method cell\n");
        }
        builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "type"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_type));
    }
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "int"), intProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "str"), strProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "list"), listProto);
    if (std::getenv("PROTO_ENV_DIAG")) {
        const proto::ProtoObject* dNew = dictProto ? dictProto->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__")) : nullptr;
        fprintf(stderr, "DEBUG IN BUILTINS INIT: dictProto=%p dict.__new__=%p\n", (void*)dictProto, (void*)dNew);
    }
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dict"), dictProto);
    if (std::getenv("PROTO_ENV_DIAG")) {
        const proto::ProtoObject* dInB = builtins->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dict"));
        const proto::ProtoObject* dNew = dInB ? dInB->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__")) : nullptr;
        fprintf(stderr, "DEBUG IN BUILTINS INIT: dictInB=%p dictInB.__new__=%p\n", (void*)dInB, (void*)dNew);
    }
    
    // Add dummy bytearray
    auto py_bytearray_new = [](proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) -> const proto::ProtoObject* {
        const proto::ProtoList* shiftedArgs = context->newList();
        const proto::ProtoObject* cls = args ? args->getAt(context, 0) : nullptr;
        if (args) {
            for (size_t i = 1; i < args->getSize(context); i++) shiftedArgs = shiftedArgs->appendLast(context, args->getAt(context, i));
        }
        return py_bytearray_fallback(context, cls, parentLink, shiftedArgs, kwargs);
    };
    const proto::ProtoObject* bytearrayClass = ctx->newObject(false);
    if (objectProto) bytearrayClass = bytearrayClass->addParent(ctx, objectProto);
    const proto::ProtoString* py_class_local = PythonEnvironment::fromContext(ctx) ? PythonEnvironment::fromContext(ctx)->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__");
    const proto::ProtoString* py_name_local = PythonEnvironment::fromContext(ctx) ? PythonEnvironment::fromContext(ctx)->getNameString() : proto::ProtoString::fromUTF8String(ctx, "__name__");
    if (typeProto) bytearrayClass = bytearrayClass->setAttribute(ctx, py_class_local, typeProto);
    bytearrayClass = bytearrayClass->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "bytearray")->asObject(ctx));
    bytearrayClass = bytearrayClass->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_bytearray_new));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bytearray"), bytearrayClass);

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "tuple"), tupleProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "set"), setProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bytes"), bytesProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "slice"), sliceType);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "frozenset"), frozensetProto);
    if (floatProto) builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "float"), floatProto);
    if (boolProto) builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bool"), boolProto);
    if (complexProto) builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "complex"), complexProto);
    
    // Add functions
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "len"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_len));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "repr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_repr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "format"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_format));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("builtins"));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__package__"), ctx->fromUTF8String(""));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_open));
    // print registration diagnostic removed
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "print"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_print));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dir"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_dir));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "id"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_id));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_object_getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_getattribute));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_object_setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "iter"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_iter));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "next"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "contains"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "in"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isinstance"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_isinstance));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "issubclass"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_issubclass));
    PythonEnvironment* pEnv = PythonEnvironment::fromContext(ctx);
    auto py_range_new = [](proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) -> const proto::ProtoObject* {
        const proto::ProtoList* shiftedArgs = context->newList();
        const proto::ProtoObject* cls = args ? args->getAt(context, 0) : nullptr;
        if (args) {
            for (size_t i = 1; i < args->getSize(context); i++) shiftedArgs = shiftedArgs->appendLast(context, args->getAt(context, i));
        }
        return py_range(context, cls, parentLink, shiftedArgs, kwargs);
    };
    const proto::ProtoObject* rangeClass = ctx->newObject(false);
    if (objectProto) rangeClass = rangeClass->addParent(ctx, objectProto);
    if (typeProto) rangeClass = rangeClass->setAttribute(ctx, py_class_local, typeProto);
    rangeClass = rangeClass->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "range")->asObject(ctx));
    rangeClass = rangeClass->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_range_new));
    rangeClass = rangeClass->setAttribute(ctx, pEnv ? pEnv->getIterString() : proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(nullptr, py_range_iter));
    rangeClass = rangeClass->setAttribute(ctx, pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(nullptr, py_ignore_init));
    builtins = builtins->setAttribute(ctx, pEnv ? pEnv->getRangeString() : proto::ProtoString::fromUTF8String(ctx, "range"), rangeClass);

    const proto::ProtoObject* zipProto = ctx->newObject(false);
    if (objectProto) zipProto = zipProto->addParent(ctx, objectProto);
    if (typeProto) zipProto = zipProto->setAttribute(ctx, py_class_local, typeProto);
    zipProto = zipProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "zip")->asObject(ctx));
    zipProto = zipProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_zip));
    zipProto = zipProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(nullptr, py_ignore_init));
    zipProto = zipProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(zipProto), py_self_iter));
    zipProto = zipProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(zipProto), py_zip_next));
    builtins = builtins->setAttribute(ctx, pEnv->getZipProtoString(), zipProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "zip"), zipProto);
    const proto::ProtoObject* filterProto = ctx->newObject(false);
    if (objectProto) filterProto = filterProto->addParent(ctx, objectProto);
    if (typeProto) filterProto = filterProto->setAttribute(ctx, py_class_local, typeProto);
    filterProto = filterProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "filter")->asObject(ctx));
    filterProto = filterProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_filter));
    filterProto = filterProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(nullptr, py_ignore_init));
    filterProto = filterProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(filterProto), py_self_iter));
    filterProto = filterProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(filterProto), py_filter_next));
    builtins = builtins->setAttribute(ctx, pEnv->getFilterProtoString(), filterProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "filter"), filterProto);
    const proto::ProtoObject* mapProto = ctx->newObject(false);
    if (objectProto) mapProto = mapProto->addParent(ctx, objectProto);
    if (typeProto) mapProto = mapProto->setAttribute(ctx, py_class_local, typeProto);
    mapProto = mapProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "map")->asObject(ctx));
    mapProto = mapProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_map));
    mapProto = mapProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(nullptr, py_ignore_init));
    mapProto = mapProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(mapProto), py_self_iter));
    mapProto = mapProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(mapProto), py_map_next));
    builtins = builtins->setAttribute(ctx, pEnv->getMapProtoString(), mapProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "map"), mapProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sum"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_sum));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "all"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_all));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "any"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_any));

    const proto::ProtoObject* enumProto = ctx->newObject(false);
    if (objectProto) enumProto = enumProto->addParent(ctx, objectProto);
    if (typeProto) enumProto = enumProto->setAttribute(ctx, py_class_local, typeProto);
    enumProto = enumProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "enumerate")->asObject(ctx));
    enumProto = enumProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_enumerate));
    enumProto = enumProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(nullptr, py_ignore_init));
    enumProto = enumProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(enumProto), py_self_iter));
    enumProto = enumProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(enumProto), py_enumerate_next));
    builtins = builtins->setAttribute(ctx, pEnv->getEnumProtoString(), enumProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "enumerate"), enumProto);

    const proto::ProtoObject* revProto = ctx->newObject(false);
    if (objectProto) revProto = revProto->addParent(ctx, objectProto);
    if (typeProto) revProto = revProto->setAttribute(ctx, py_class_local, typeProto);
    revProto = revProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "reversed")->asObject(ctx));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "reversed"), revProto);

    // rangeProto initialization was removed as rangeClass handles instantiation now
    
    revProto = revProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_self_iter));
    revProto = revProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_reversed_next));
    revProto = revProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_reversed));
    builtins = builtins->setAttribute(ctx, pEnv->getRevProtoString(), revProto);
    // Also update the builtins dict with the fully initialized revProto
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "reversed"), revProto);

    // Register enumerate and reversed AFTER their prototypes are set, so 'builtins' has them.
    // They are already registered at lines 3617 and 3623. Do NOT overwrite them as methods here.

    // Note: rangeProto initialization was moved up

    // Initialize specialized RangeIterator prototype
    const proto::ProtoObject* rangeIterProto = ctx->newObject(false);
    if (objectProto) rangeIterProto = rangeIterProto->addParent(ctx, objectProto);
    if (typeProto) rangeIterProto = rangeIterProto->setAttribute(ctx, py_class_local, typeProto);
    rangeIterProto = rangeIterProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "range_iterator")->asObject(ctx));
    rangeIterProto = rangeIterProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(nullptr, py_self_iter));
    rangeIterProto = rangeIterProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(nullptr, py_range_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "range_iterator"), rangeIterProto);
    pEnv->setRangeIteratorProto(rangeIterProto);

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "abs"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_abs));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "min"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_min));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "max"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_max));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pow"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_pow));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "round"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_round));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "divmod"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_divmod));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ascii"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_ascii));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ord"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_ord));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "chr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_chr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bin"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_bin));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "oct"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_oct));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hex"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hex));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sorted"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_sorted));

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "callable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_callable));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_object_getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_getattribute));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_object_setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "delattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_delattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "raise"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_raise));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dir"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_dir));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "vars"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_vars));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "input"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_input));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "breakpoint"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_breakpoint));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "globals"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_globals));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "locals"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_locals));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_tokenize_source"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py__tokenize_source));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "compile"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_compile));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "eval"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_eval));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exec"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_exec));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hash"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hash));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "help"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_help));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_complete"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_complete));
    auto py_memoryview_new = [](proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) -> const proto::ProtoObject* {
        return py_memoryview(context, self, parentLink, args, kwargs);
    };
    const proto::ProtoObject* memoryviewClass = ctx->newObject(false);
    if (objectProto) memoryviewClass = memoryviewClass->addParent(ctx, objectProto);
    if (typeProto) memoryviewClass = memoryviewClass->setAttribute(ctx, py_class_local, typeProto);
    memoryviewClass = memoryviewClass->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "memoryview")->asObject(ctx));
    memoryviewClass = memoryviewClass->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_memoryview_new));
    memoryviewClass = memoryviewClass->setAttribute(ctx, pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__"), ctx->fromMethod(nullptr, py_ignore_init));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "memoryview"), memoryviewClass);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "super"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_super));
    const proto::ProtoObject* propertyProto = ctx->newObject(false);
    if (objectProto) propertyProto = propertyProto->addParent(ctx, objectProto);
    if (typeProto) propertyProto = propertyProto->setAttribute(ctx, py_class_local, typeProto);
    propertyProto = propertyProto->setAttribute(ctx, py_name_local, proto::ProtoString::fromUTF8String(ctx, "property")->asObject(ctx));
    propertyProto = propertyProto->setAttribute(ctx, pEnv->getGetDunderString(), ctx->fromMethod(nullptr, py_property_get));
    propertyProto = propertyProto->setAttribute(ctx, pEnv->getSetDunderString(), ctx->fromMethod(nullptr, py_property_set));
    propertyProto = propertyProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_property));
    const proto::ProtoString* initStr = pEnv ? pEnv->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__");
    propertyProto = propertyProto->setAttribute(ctx, initStr, ctx->fromMethod(nullptr, py_property_init));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "property"), propertyProto);
    
    const proto::ProtoObject* staticmethodProto = ctx->newObject(true);
    staticmethodProto = staticmethodProto->setAttribute(ctx, pEnv->getClassString(), typeProto);
    staticmethodProto = staticmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), proto::ProtoString::fromUTF8String(ctx, "staticmethod")->asObject(ctx));
    
    // Add MRO so that py_type_getattribute can find descriptor methods
    const proto::ProtoList* smMroList = ctx->newList()->appendLast(ctx, staticmethodProto)->appendLast(ctx, objectProto);
    staticmethodProto = staticmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__mro__"), smMroList->asObject(ctx));
    staticmethodProto = staticmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__bases__"), ctx->newList()->appendLast(ctx, objectProto)->asObject(ctx));
    staticmethodProto = staticmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__is_python_class__"), PROTO_TRUE);

    const proto::ProtoObject* classmethodProto = ctx->newObject(true);
    classmethodProto = classmethodProto->setAttribute(ctx, pEnv->getClassString(), typeProto);
    classmethodProto = classmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), proto::ProtoString::fromUTF8String(ctx, "classmethod")->asObject(ctx));
    
    // Add MRO so that py_type_getattribute can find descriptor methods
    const proto::ProtoList* cmMroList = ctx->newList()->appendLast(ctx, classmethodProto)->appendLast(ctx, objectProto);
    classmethodProto = classmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__mro__"), cmMroList->asObject(ctx));
    classmethodProto = classmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__bases__"), ctx->newList()->appendLast(ctx, objectProto)->asObject(ctx));
    classmethodProto = classmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__is_python_class__"), PROTO_TRUE);

    classmethodProto = classmethodProto->setAttribute(ctx, pEnv->getGetDunderString(), ctx->fromMethod(nullptr, py_classmethod_get));
    classmethodProto = classmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_classmethod));
    
    staticmethodProto = staticmethodProto->setAttribute(ctx, pEnv->getGetDunderString(), ctx->fromMethod(nullptr, py_staticmethod_get));
    staticmethodProto = staticmethodProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__new__"), ctx->fromMethod(nullptr, py_staticmethod));

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "staticmethod"), staticmethodProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "classmethod"), classmethodProto);
    
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__import__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_import));
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG IN BUILTINS INIT: END obInB=%p\n", (void*)builtins->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object")));
    }
    return builtins;
}

} // namespace builtins
} // namespace protoPython
