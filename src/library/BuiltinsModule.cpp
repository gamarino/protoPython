#include <protoPython/BuiltinsModule.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/Parser.h>
#include <protoPython/Compiler.h>
#include <protoPython/Tokenizer.h>
#include <protoCore.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>

namespace protoPython {
namespace builtins {

// NOTE: a previous revision of this file declared a local
// `static bool get_env_diag()` here, which shadowed the
// canonical `::get_env_diag()` defined in DiagUtils.h and
// silently kept the old "any value = enabled" semantics.
// Removed: every site below now resolves to the unqualified
// helper in DiagUtils.h, which honours the falsy spellings
// ("0", "false", "off", "no").

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

static bool areSameClasses(proto::ProtoContext* context, const proto::ProtoObject* c1, const proto::ProtoObject* c2);

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
    if (env) dictObj = env->initDictStorage(context, dictObj);
    
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
    if (get_env_diag()) {
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
            const proto::ProtoString* pkgKey = proto::ProtoString::createSymbol(context, "__package__");
            const proto::ProtoString* nameKey = proto::ProtoString::createSymbol(context, "__name__");
            const proto::ProtoString* pathKey = proto::ProtoString::createSymbol(context, "__path__");

            const proto::ProtoObject* pkgObj = globals->getAttribute(context, pkgKey);
            std::string base;

            if (pkgObj && pkgObj->isString(context) && pkgObj != PROTO_NONE) {
                pkgObj->asString(context)->toUTF8String(context, base);
            } else {
                const proto::ProtoObject* nameObj = globals->getAttribute(context, nameKey);
                if (nameObj && nameObj->isString(context)) {
                    nameObj->asString(context)->toUTF8String(context, base);
                    // If it's a module (no __path__), we take the parent package
                    if (globals->hasAttribute(context, pathKey) == PROTO_FALSE) {
                        size_t lastDot = base.find_last_of('.');
                        if (lastDot != std::string::npos) {
                            base = base.substr(0, lastDot);
                        } else {
                            base = "";
                        }
                    }
                }
            }
            
            if (!base.empty() || level > 1) {
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
                    if (std::getenv("PROTO_IMPORT_DIAG")) {
                        fprintf(stderr, "DEBUG IMPORT: attr '%s' NOT FOUND in module '%s'\n", itemName.c_str(), moduleName.c_str());
                        const proto::ProtoSparseList* attrs = leaf->getAttributes(context);
                        if (attrs) {
                            fprintf(stderr, "DEBUG IMPORT: Module has %lu attributes:\n", attrs->getSize(context));
                            const proto::ProtoSparseListIterator* it = attrs->getIterator(context);
                            while (it && it->hasNext(context)) {
                                unsigned long key = it->nextKey(context);
                                const proto::ProtoObject* kObj = reinterpret_cast<const proto::ProtoObject*>(key);
                                if (kObj && kObj->isString(context)) {
                                    std::string kn;
                                    kObj->asString(context)->toUTF8String(context, kn);
                                    fprintf(stderr, "  - %s\n", kn.c_str());
                                }
                                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(context);
                            }
                        }
                    }
                    const proto::ProtoObject* pathAttr = leaf->getAttribute(context, PythonEnvironment::getInternedString(context, "__path__"));
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
        const proto::ProtoString* keysName = env ? env->getKeysString() : PythonEnvironment::getInternedString(context, "__keys__");
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
                        results = results->appendLast(context, PythonEnvironment::getInternedString(context, name.c_str())->asObject(context));
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
    const proto::ProtoString* dataName = env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__");
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
    const proto::ProtoString* lenStr = env ? env->getLenString() : PythonEnvironment::getInternedString(context, "__len__");
    const proto::ProtoObject* lenMethod = env ? env->getAttribute(context, obj, lenStr, false) : (obj->hasOwnAttribute(context, lenStr) == PROTO_TRUE ? obj->getAttribute(context, lenStr) : nullptr);
    if (lenMethod && lenMethod->asMethod(context)) {
        const proto::ProtoList* emptyArgs = env ? env->getEmptyList() : context->newList();
        const proto::ProtoObject* res = lenMethod->asMethod(context)(context, obj, nullptr, emptyArgs, nullptr);
        if (res && res != PROTO_NONE && res->isInteger(context)) {
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
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_print called with %lu args\n", positionalParameters->getSize(context));
    }
    (void)keywordParameters;
    std::string sep = " ";
    std::string end = "\n";

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* strS = env ? env->getStrString() : PythonEnvironment::getInternedString(context, "__str__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    unsigned long size = positionalParameters->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* obj = positionalParameters->getAt(context, static_cast<int>(i));
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_print arg[%lu]=%p noneProto=%p\n", i, (void*)obj, (void*)(env ? env->getNonePrototype() : nullptr));
        }

        bool isNone = !obj || obj == PROTO_NONE || (env && obj == env->getNonePrototype());
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_print isNone=%d obj=%p noneProto=%p\n", isNone, (void*)obj, (void*)(env ? env->getNonePrototype() : nullptr));
        }
        if (isNone) {
            std::cout << "None";
        } else if (obj->isInteger(context)) {
            // asLong overflows for bignums; route through reprObject which
            // falls back to Integer::toString when the value doesn't fit
            // into long long.
            try {
                std::cout << std::to_string(obj->asLong(context));
            } catch (const std::overflow_error&) {
                std::cout << (env ? env->reprObject(context, obj) : std::string("<int>"));
            }
        } else if (obj->isDouble(context)) {
            std::cout << std::to_string(obj->asDouble(context));
        } else if (obj->isString(context)) {
            std::string s;
            obj->asString(context)->toUTF8String(context, s);
            std::cout << s;
        } else if (obj == PROTO_TRUE) {
            std::cout << "True";
        } else if (obj == PROTO_FALSE) {
            std::cout << "False";
        } else {
            if (env) {
                std::cout << env->reprObject(context, obj);
            } else {
                const proto::ProtoString* reprS = PythonEnvironment::getInternedString(context, "__repr__");
                const proto::ProtoObject* reprMethod = obj->getAttribute(context, reprS);
                if (reprMethod && reprMethod != PROTO_NONE) {
                    const proto::ProtoObject* out = obj->call(context, nullptr, reprS, obj, emptyL, nullptr);
                    if (out && out->isString(context)) {
                        std::string s;
                        out->asString(context)->toUTF8String(context, s);
                        std::cout << s;
                    } else {
                        std::cout << "<unprintable>";
                    }
                } else {
                    std::cout << "<unprintable>";
                }
            }
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
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_iter(obj=%p, repr=%s)\n", (void*)obj, PythonEnvironment::reprObject(context, obj).c_str());
        }
        const proto::ProtoObject* it = env->iter(obj);
        if (it) return it;
    } else {
        const proto::ProtoString* iterS = PythonEnvironment::getInternedString(context, "__iter__");
        const proto::ProtoObject* iterMethod = env ? env->getAttribute(context, obj, iterS, false) : (obj->hasOwnAttribute(context, iterS) == PROTO_TRUE ? obj->getAttribute(context, iterS) : nullptr);
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
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");

    const proto::ProtoObject* nextMethod = env ? env->getAttribute(context, obj, nextS, false) : (obj->hasOwnAttribute(context, nextS) == PROTO_TRUE ? obj->getAttribute(context, nextS) : nullptr);
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
        const proto::ProtoObject* cls = env ? env->getAttribute(context, obj, classS, false) : (obj->hasOwnAttribute(context, classS) == PROTO_TRUE ? obj->getAttribute(context, classS) : nullptr);
        if (cls) {
            const proto::ProtoObject* nameAttr = env ? env->getAttribute(context, cls, nameS, false) : (cls->hasOwnAttribute(context, nameS) == PROTO_TRUE ? cls->getAttribute(context, nameS) : nullptr);
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

    const proto::ProtoString* containsS = env ? env->getContainsString() : PythonEnvironment::getInternedString(context, "__contains__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* containsMethod = env ? env->getAttribute(context, container, containsS, false) : (container->hasOwnAttribute(context, containsS) == PROTO_TRUE ? container->getAttribute(context, containsS) : nullptr);
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
        // Integer::sign is bignum-safe.
        return obj->integerSign(context) != 0 ? PROTO_TRUE : PROTO_FALSE;
    }
    if (obj->isDouble(context)) {
        return obj->asDouble(context) != 0.0 ? PROTO_TRUE : PROTO_FALSE;
    }

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* boolS = env ? env->getBoolString() : PythonEnvironment::getInternedString(context, "__bool__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* cls = env ? env->getType(context, obj) : obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
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
        const proto::ProtoString* realS = PythonEnvironment::getInternedString(context, "real");
        const proto::ProtoString* imagS = PythonEnvironment::getInternedString(context, "imag");
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
    res = res->setAttribute(context, PythonEnvironment::getInternedString(context, "real"), context->fromDouble(real));
    res = res->setAttribute(context, PythonEnvironment::getInternedString(context, "imag"), context->fromDouble(imag));
    
    return res;
}

const proto::ProtoObject* py_complex_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* rObj = self->getAttribute(context, PythonEnvironment::getInternedString(context, "real"));
    const proto::ProtoObject* iObj = self->getAttribute(context, PythonEnvironment::getInternedString(context, "imag"));
    double real = (rObj && rObj->isDouble(context)) ? rObj->asDouble(context) : 0.0;
    double imag = (iObj && iObj->isDouble(context)) ? iObj->asDouble(context) : 0.0;
    
    char buf[128];
    if (real == 0.0) {
        std::snprintf(buf, sizeof(buf), "%.17gj", imag);
    } else {
        std::snprintf(buf, sizeof(buf), "(%.17g%s%.17gj)", real, (imag >= 0 ? "+" : ""), imag);
    }
    return PythonEnvironment::getInternedString(context, buf)->asObject(context);
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
    if (obj == PROTO_TRUE) return PythonEnvironment::getInternedString(context, "True")->asObject(context);
    if (obj == PROTO_FALSE) return PythonEnvironment::getInternedString(context, "False")->asObject(context);
    if (obj->isInteger(context)) {
        // Integer::toString handles bignum (asLong + snprintf would
        // overflow for LargeInteger).
        const proto::ProtoString* s = obj->asIntegerString(context, 10);
        return s->asObject(context);
    }
    if (obj->isDouble(context)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(context));
        return PythonEnvironment::getInternedString(context, buf)->asObject(context);
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
        return PythonEnvironment::getInternedString(context, out.c_str())->asObject(context);
    }

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);

    // Delegate to PythonEnvironment::reprObject which handles both native methods
    // and Python callables via the full descriptor protocol (type-based __repr__ lookup).
    std::string result = PythonEnvironment::reprObject(context, obj);
    return PythonEnvironment::getInternedString(context, result.c_str())->asObject(context);
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
        return PythonEnvironment::getInternedString(context, buf)->asObject(context);
    }
    if (obj->isInteger(context)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)obj->asLong(context));
        return PythonEnvironment::getInternedString(context, buf)->asObject(context);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* formatS = env ? env->getFormatString() : PythonEnvironment::getInternedString(context, "__format__");

    const proto::ProtoObject* formatMethod = obj->getAttribute(context, formatS);
    if (!formatMethod || !formatMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* args = context->newList();
    if (positionalParameters->getSize(context) >= 2) {
        args = args->appendLast(context, positionalParameters->getAt(context, 1));
    } else {
        args = args->appendLast(context, PythonEnvironment::getInternedString(context, "")->asObject(context));
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
    const proto::ProtoObject* ioMod = self->getAttribute(context, env ? env->getIOModuleString() : PythonEnvironment::getInternedString(context, "__io_module__"));
    if (!ioMod || ioMod == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* openFunc = ioMod->getAttribute(context, env ? env->getOpenString() : PythonEnvironment::getInternedString(context, "open"));
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
    const proto::ProtoString* enumProtoS = env ? env->getEnumProtoString() : PythonEnvironment::getInternedString(context, "__enumerate_proto__");
    const proto::ProtoString* itS = env ? env->getEnumIterString() : PythonEnvironment::getInternedString(context, "__enumerate_it__");
    const proto::ProtoString* idxS = env ? env->getEnumIdxString() : PythonEnvironment::getInternedString(context, "__enumerate_idx__");

    const proto::ProtoObject* it = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
    if (!it || it == (env ? env->getNonePrototype() : nullptr) || it == PROTO_NONE) {
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_enumerate py_iter returned empty for iterable=%p (it=%p, envNone=%p)\n", (void*)iterable, (void*)it, (void*)(env ? env->getNonePrototype() : nullptr));
        return PROTO_NONE;
    }

    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* enumObj = cls->newChild(context, true);
    enumObj = enumObj->setAttribute(context, itS, it);
    enumObj = enumObj->setAttribute(context, idxS, context->fromInteger(start));
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_enumerate successfully created enumObj=%p\n", (void*)enumObj);
    return enumObj;
}

static const proto::ProtoObject* py_enumerate_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* itS = env ? env->getEnumIterString() : PythonEnvironment::getInternedString(context, "__enumerate_it__");
    const proto::ProtoString* idxS = env ? env->getEnumIdxString() : PythonEnvironment::getInternedString(context, "__enumerate_idx__");
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");

    const proto::ProtoObject* it = self->getAttribute(context, itS);
    const proto::ProtoObject* idxObj = self->getAttribute(context, idxS);
    if (!it || !idxObj) {
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_enumerate_next it=%p idxObj=%p\n", (void*)it, (void*)idxObj);
        return nullptr;
    }

    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) {
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_enumerate_next nextMethod missing or not a method\n");
        return nullptr;
    }
    
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* value = nextMethod->asMethod(context)(context, it, nullptr, emptyL, nullptr);
    if (!value) {
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_enumerate_next nextMethod returned nullptr\n");
        return nullptr;
    }

    long long idx = idxObj->asLong(context);
    self->setAttribute(context, idxS, context->fromInteger(idx + 1));
    
    const proto::ProtoList* l = context->newList();
    l = l->appendLast(context, idxObj);
    l = l->appendLast(context, value);
    return env ? env->newTuple(l) : context->newTupleFromList(l)->asObject(context);
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
    const proto::ProtoString* reversedS = env ? env->getReversedString() : PythonEnvironment::getInternedString(context, "__reversed__");
    const proto::ProtoString* lenS = env ? env->getLenString() : PythonEnvironment::getInternedString(context, "__len__");
    const proto::ProtoString* getitemS = env ? env->getGetItemString() : PythonEnvironment::getInternedString(context, "__getitem__");
    const proto::ProtoString* revProtoS = env ? env->getRevProtoString() : PythonEnvironment::getInternedString(context, "__reversed_proto__");
    const proto::ProtoString* revObjS = env ? env->getRevObjString() : PythonEnvironment::getInternedString(context, "__reversed_obj__");
    const proto::ProtoString* revIdxS = env ? env->getRevIdxString() : PythonEnvironment::getInternedString(context, "__reversed_idx__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* revMethod = env ? env->getAttribute(context, obj, reversedS, false) : (obj->hasOwnAttribute(context, reversedS) == PROTO_TRUE ? obj->getAttribute(context, reversedS) : nullptr);
    if (revMethod && revMethod != PROTO_NONE) {
        const proto::ProtoObject* r = ::protoPython::invokePythonCallable(context, revMethod, emptyL, nullptr);
        if (r) return r;
        if (env) env->clearPendingException(); // fallback if exception
    }

    const proto::ProtoObject* lenMethod = env ? env->getAttribute(context, obj, lenS, false) : (obj->hasOwnAttribute(context, lenS) == PROTO_TRUE ? obj->getAttribute(context, lenS) : nullptr);
    const proto::ProtoObject* getitemMethod = env ? env->getAttribute(context, obj, getitemS, false) : (obj->hasOwnAttribute(context, getitemS) == PROTO_TRUE ? obj->getAttribute(context, getitemS) : nullptr);
    if (!lenMethod || !getitemMethod) {
        if (env) env->raiseTypeError(context, "'" + PythonEnvironment::reprObject(context, obj) + "' object is not reversible");
        return nullptr;
    }
    
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_reversed calling lenMethod=%p\n", (void*)lenMethod);
    const proto::ProtoObject* lenObj = ::protoPython::invokePythonCallable(context, lenMethod, emptyL, nullptr);
    if (!lenObj) {
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_reversed lenMethod->call returned nullptr\n");
        return nullptr; // Exception thrown by __len__
    }
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_reversed lenObj=%p repr=%s\n", (void*)lenObj, PythonEnvironment::reprObject(context, lenObj).c_str());
    if (!lenObj->isInteger(context)) {
        if (env) env->raiseTypeError(context, "'" + PythonEnvironment::reprObject(context, lenObj) + "' returned from __len__ cannot be interpreted as an integer");
        return nullptr;
    }
    long long n = lenObj->asLong(context);

    // `self` IS the reversed prototype (we were registered as
    // reversed.__new__).  An older revision tried `self->getAttribute(
    // "__reversed_proto__")` which only exists on the builtins module, not
    // on revProto itself, so this branch always returned PROTO_NONE; the
    // outer runUserClassCall would then create an unparented stub instance
    // with no __reversed_obj__/__reversed_idx__, and the first next() crash
    // depended on the pristine attribute slot's contents.
    const proto::ProtoObject* revProto = self;
    const proto::ProtoObject* revObj = revProto->newChild(context, true);
    revObj = revObj->setAttribute(context, revObjS, obj);
    revObj = revObj->setAttribute(context, revIdxS, context->fromInteger(n - 1));
    return revObj;
}

static const proto::ProtoObject* py_reversed_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* objS = env ? env->getRevObjString() : PythonEnvironment::getInternedString(context, "__reversed_obj__");
    const proto::ProtoString* idxS = env ? env->getRevIdxString() : PythonEnvironment::getInternedString(context, "__reversed_idx__");
    const proto::ProtoString* getitemS = env ? env->getGetItemString() : PythonEnvironment::getInternedString(context, "__getitem__");

    const proto::ProtoObject* obj = self->getAttribute(context, objS);
    const proto::ProtoObject* idxObj = self->getAttribute(context, idxS);
    // Belt-and-suspenders: even after py_reversed sets these correctly,
    // never let asLong throw a C++ runtime_error and abort the process if
    // a future bug leaves either slot unset/non-integer.  Treat that as
    // exhausted iterator rather than crash.
    if (!obj || !idxObj || idxObj == PROTO_NONE || !idxObj->isInteger(context)) {
        if (env) env->raiseStopIteration(context);
        return nullptr;
    }
    long long idx = idxObj->asLong(context);
    if (idx < 0) {
        if (env) env->raiseStopIteration(context);
        return nullptr;
    }

    // env->getAttribute follows the Python attribute protocol so that a
    // class-defined __getitem__ comes back as a callable bound method (or
    // an unbound Python function we feed to invokePythonCallable below).
    // The previous implementation only handled native methods (asMethod !=
    // nullptr) and returned null for plain Python __getitem__, so
    // `reversed(c)` on a user class returned nothing on the very first
    // next() — the audit then crashed on a downstream non-int access.
    const proto::ProtoObject* getitemMethod = env
        ? env->getAttribute(context, obj, getitemS, false)
        : obj->getAttribute(context, getitemS);
    if (!getitemMethod || getitemMethod == PROTO_NONE) {
        if (env) env->raiseStopIteration(context);
        return nullptr;
    }
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(idx));
    const proto::ProtoObject* value = nullptr;
    if (getitemMethod->asMethod(context)) {
        // Native bound method (e.g. dict's MappingProxy __getitem__).
        value = getitemMethod->asMethod(context)(context, obj, nullptr, args, nullptr);
    } else {
        // Python-level method: invokePythonCallable already understands
        // bound methods (asMethodSelf carries `obj`) and unbound functions.
        value = ::protoPython::invokePythonCallable(context, getitemMethod, args, nullptr);
    }
    if (env && env->hasPendingException()) {
        // CPython contract: if obj[idx] raises IndexError, reversed yields
        // StopIteration.  Other exceptions propagate.
        const proto::ProtoObject* exc = env->peekPendingException();
        if (exc) {
            const proto::ProtoObject* cls = env->getType(context, exc);
            const proto::ProtoString* nameS = env->getNameString();
            const proto::ProtoObject* nameAttr = cls ? cls->getAttribute(context, nameS) : nullptr;
            if (nameAttr && nameAttr->isString(context)) {
                std::string clsName;
                nameAttr->asString(context)->toUTF8String(context, clsName);
                if (clsName == "IndexError") {
                    env->clearPendingException();
                    env->raiseStopIteration(context);
                }
            }
        }
        return nullptr;
    }
    // Persist the decremented index — setAttribute returns a new
    // immutable cell version so we have to assign the result back.
    const_cast<proto::ProtoObject*>(self)->setAttribute(context, idxS, context->fromInteger(idx - 1));
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
    const proto::ProtoString* iterS = env ? env->getIterString() : PythonEnvironment::getInternedString(context, "__iter__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return start;

    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return start;

    // Bignum-safe accumulator: keep the partial sum as a ProtoObject and
    // use Integer::add so values exceeding int64 are handled correctly.
    const proto::ProtoObject* acc = start->isInteger(context) ? start : context->fromInteger(0);
    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val) {
             if (env && env->handleExhaustion(context)) break;
             return nullptr; // Propagate other errors
        }
        if (val == noneObj) break;
        if (val->isInteger(context)) acc = acc->add(context, val);
    }
    return acc;
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
    const proto::ProtoString* iterS = env ? env->getIterString() : PythonEnvironment::getInternedString(context, "__iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");

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
    const proto::ProtoString* iterS = env ? env->getIterString() : PythonEnvironment::getInternedString(context, "__iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");

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
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    // None, null, or bare integers/booleans are not callable
    if (!obj || obj == PROTO_NONE) return PROTO_FALSE;
    if (obj->isInteger(context) || obj->isBoolean(context)) return PROTO_FALSE;
    // Native methods are directly callable
    if (obj->asMethod(context)) return PROTO_TRUE;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    // Python classes are always callable (they construct instances).
    if (env && env->isActuallyAClass(context, obj)) return PROTO_TRUE;
    const proto::ProtoString* callS = env ? env->getCallString() : PythonEnvironment::getInternedString(context, "__call__");
    // Search for __call__ via the full Python attribute resolution (including metaclass).
    const proto::ProtoObject* call = env ? env->getAttribute(context, obj, callS, false) : obj->getAttribute(context, callS);
    return (call && call != PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
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
        bool isInitOverridden = false;
        
        const proto::ProtoString* initS = env ? env->getInitString() : PythonEnvironment::getInternedString(context, "__init__");
        const proto::ProtoString* newS = env ? env->getNewString() : PythonEnvironment::getInternedString(context, "__new__");
        const proto::ProtoObject* objProto = env ? env->getObjectPrototype() : nullptr;
        const proto::ProtoObject* objInitAttr = objProto ? objProto->getAttribute(context, initS) : nullptr;
        const proto::ProtoObject* objNewAttr = objProto ? objProto->getAttribute(context, newS) : nullptr;

        if (env && positionalParameters->getSize(context) > 0) {
            const proto::ProtoObject* inst = positionalParameters->getAt(context, 0);
            if (inst) {
                const proto::ProtoObject* cls = inst->getAttribute(context, env->getClassString());
                if (cls) {
                    if (get_env_diag()) {
                        std::string cn = "???";
                        const proto::ProtoObject* nAttr = cls->getAttribute(context, env->getNameString());
                        if (nAttr && nAttr->isString(context)) nAttr->asString(context)->toUTF8String(context, cn);
                        fprintf(stderr, "DEBUG py_object_init: inst=%p cls=%p ('%s') objProto=%p typeProto=%p\n", (void*)inst, (void*)cls, cn.c_str(), (void*)objProto, (void*)env->getTypePrototype());
                    }
                    if (cls != objProto) {
                        const proto::ProtoObject* initAttr = cls->getAttribute(context, initS);
                        if (initAttr && initAttr->asMethod(context) != objInitAttr->asMethod(context)) {
                             isInitOverridden = true;
                        }
                        const proto::ProtoObject* newAttr = cls->getAttribute(context, newS);
                        if (newAttr && newAttr->asMethod(context) != objNewAttr->asMethod(context)) {
                             isNewOverridden = true;
                        }
                        
                        const proto::ProtoObject* nameAttr = cls->getAttribute(context, env->getNameString());
                        if (nameAttr && nameAttr->isString(context)) nameAttr->asString(context)->toUTF8String(context, clsName);

                        if (cls == env->getTypePrototype() || clsName == "type") {
                            // type objects and their subclasses used during bootstrap are allowed to have args
                            // mirroring CPython where type overrides object.__init__
                            return PROTO_NONE;
                        }
                    }
                }
            }
        }

        if (!isNewOverridden && !isInitOverridden) {
            if (positionalParameters && positionalParameters->getSize(context) > 1) {
                // During bootstrap we are permissive to avoid hangs
                return PROTO_NONE; 
            }
        } else {
            if (get_env_diag()) {
                 fprintf(stderr, "DEBUG: py_object_init IGNORING args for class '%s' (new_over=%d, init_over=%d)\n", clsName.c_str(), isNewOverridden, isInitOverridden);
            }
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
    const proto::ProtoString* key = PythonEnvironment::getInternedString(context, nameStr.c_str());
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
    const proto::ProtoString* key = PythonEnvironment::getInternedString(context, nameStr.c_str());
    if (env) {
        env->setAttribute(context, const_cast<proto::ProtoObject*>(target), key, value);
    } else {
        const_cast<proto::ProtoObject*>(target)->setAttribute(context, key, value);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_object_eq(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    auto* env = PythonEnvironment::fromContext(context);
    // Support both bound (self=a, posArgs=[b]) and unbound (self=null, posArgs=[a,b]) conventions
    const proto::ProtoObject* a = self;
    int offset = 0;
    if (!a && posArgs && posArgs->getSize(context) >= 1) {
        a = posArgs->getAt(context, 0);
        offset = 1;
    }
    const proto::ProtoObject* b = (posArgs && (int)posArgs->getSize(context) > offset) ? posArgs->getAt(context, offset) : nullptr;
    if (!a || !b) return env ? env->getNotImplementedPrototype() : PROTO_NONE;
    if (a == b) return PROTO_TRUE;
    return env ? env->getNotImplementedPrototype() : PROTO_FALSE;
}

static const proto::ProtoObject* py_object_ne(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    auto* env = PythonEnvironment::fromContext(context);
    // Support both bound (self=a, posArgs=[b]) and unbound (self=null, posArgs=[a,b]) conventions
    const proto::ProtoObject* a = self;
    int offset = 0;
    if (!a && posArgs && posArgs->getSize(context) >= 1) {
        a = posArgs->getAt(context, 0);
        offset = 1;
    }
    const proto::ProtoObject* b = (posArgs && (int)posArgs->getSize(context) > offset) ? posArgs->getAt(context, offset) : nullptr;
    if (!a || !b) return env ? env->getNotImplementedPrototype() : PROTO_NONE;
    if (a != b) return PROTO_TRUE;
    return env ? env->getNotImplementedPrototype() : PROTO_FALSE;
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
    const proto::ProtoString* key = PythonEnvironment::getInternedString(context, nameStr.c_str());

    size_t argCount = positionalParameters->getSize(context);
    if (std::getenv("PROTO_RESOLVE_DIAG")) {
        std::string s;
        key->toUTF8String(context, s);
        fprintf(stderr, "DEBUG: getattr(obj=%p, key='%s')\n", (void*)obj, s.c_str());
    } 

    const proto::ProtoObject* val = env ? env->getAttribute(context, obj, key, false) : obj->getAttribute(context, key);
    if (std::getenv("PROTO_RESOLVE_DIAG")) {
        fprintf(stderr, "DEBUG: py_getattr val=%p PROTO_NONE=%p\n", (void*)val, (void*)PROTO_NONE);
    }
    bool attrFound = val && (val != PROTO_NONE || obj->hasAttribute(context, key) == PROTO_TRUE);

    if (!attrFound && env) {
        // Mirror OP_LOAD_ATTR: try __getattr__ before raising AttributeError.
        // raiseError=false above means env->getAttribute should not raise on a
        // missing attribute, but a *descriptor* called during the lookup may
        // have raised — those should propagate, not be masked.  We treat a
        // pending AttributeError on `obj.<key>` as the "attribute is missing"
        // signal and clear it before consulting __getattr__; any other pending
        // exception we leave alone so the original error wins.
        bool wasMissing = !env->hasPendingException();
        if (!wasMissing) {
            const proto::ProtoObject* exc = env->peekPendingException();
            if (exc) {
                const proto::ProtoObject* cls = env->getType(context, exc);
                const proto::ProtoString* nameS = env->getNameString();
                const proto::ProtoObject* nameAttr = cls ? cls->getAttribute(context, nameS) : nullptr;
                if (nameAttr && nameAttr->isString(context)) {
                    std::string clsName;
                    nameAttr->asString(context)->toUTF8String(context, clsName);
                    if (clsName == "AttributeError") {
                        env->clearPendingException();
                        wasMissing = true;
                    }
                }
            }
        }
        if (wasMissing) {
            const proto::ProtoString* getattrKey = PythonEnvironment::getInternedString(context, "__getattr__");
            const proto::ProtoObject* getattrFn = nullptr;
            bool getattrIsOwn = false;
            if (obj->hasOwnAttribute(context, getattrKey) == PROTO_TRUE) {
                getattrFn = obj->getAttribute(context, getattrKey);
                getattrIsOwn = true;
            } else {
                const proto::ProtoObject* cls = env->getType(context, obj);
                if (cls && cls != PROTO_NONE) {
                    getattrFn = env->getAttribute(context, cls, getattrKey, false);
                }
            }
            if (getattrFn && getattrFn != PROTO_NONE) {
                // Module-level __getattr__(name) takes only the name.
                // Class-level __getattr__(self, name) takes both.
                std::vector<const proto::ProtoObject*> args = getattrIsOwn
                    ? std::vector<const proto::ProtoObject*>{nameObj}
                    : std::vector<const proto::ProtoObject*>{obj, nameObj};
                val = env->callObject(getattrFn, args);
                attrFound = val && !env->hasPendingException();
            }
        }
    }

    if (env && env->hasPendingException()) {
        if (argCount >= 3) env->clearPendingException();
        else return nullptr;
    }

    if (attrFound) {
        return val;
    }

    if (argCount >= 3) {
        return positionalParameters->getAt(context, 2);
    }

    if (env) env->raiseAttributeError(context, obj, nameStr);
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
    const proto::ProtoString* key = PythonEnvironment::getInternedString(context, nameStr.c_str());
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
        result = result->appendLast(context, PythonEnvironment::getInternedString(context, name.c_str())->asObject(context));
    }
    
    // Wrap in a Python list object (Step 1347 fix)
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* listPrototype = env ? env->getListPrototype() : nullptr;
    if (listPrototype && listPrototype != PROTO_NONE) {
        const proto::ProtoObject* listObj = listPrototype->newChild(context, true);
        listObj->setAttribute(context, env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__"), result->asObject(context));
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
            const proto::ProtoObject* strMethod = promptObj->getAttribute(context, env ? env->getStrString() : PythonEnvironment::getInternedString(context, "__str__"));
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
        return PythonEnvironment::getInternedString(context, line.c_str())->asObject(context);
    
    if (in && in->eof()) {
        if (env) env->raiseEOFError(context);
        return PROTO_NONE;
    }
    return PythonEnvironment::getInternedString(context, "")->asObject(context);
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
        pair = pair->appendLast(context, PythonEnvironment::getInternedString(context, t.value.c_str())->asObject(context));
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
    PythonEnvironment* cenv = PythonEnvironment::fromContext(context);
    auto raiseSE = [&](const std::string& msg, int line, int col) {
        if (!cenv) return;
        int lineno = line > 0 ? line : 1;
        size_t start = 0;
        for (int i = 1; i < lineno; ++i) {
            size_t n = source.find('\n', start);
            if (n == std::string::npos) { start = source.size(); break; }
            start = n + 1;
        }
        size_t end = source.find('\n', start);
        std::string lineText = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
        cenv->raiseSyntaxError(context, msg, lineno, col, lineText);
    };
    if (mode == "eval") {
        Parser parser(source);
        // CPython's eval mode (eval_input grammar = `testlist NEWLINE*
        // ENDMARKER`) accepts comma-separated tuples, e.g.
        // `eval('1, 0 or 1')` -> (1, 1).  Use parseTestList so the bare
        // comma form is accepted; parseExpression alone treats the comma
        // as end-of-expression and surfaces "invalid syntax".
        std::unique_ptr<ASTNode> expr = parser.parseTestList();
        if (!expr) {
            raiseSE(parser.hasError() ? parser.getLastErrorMsg() : "invalid syntax",
                    parser.getLastErrorLine(), parser.getLastErrorColumn());
            return PROTO_NONE;
        }
        if (!compiler.compileExpression(expr.get())) {
            raiseSE("invalid syntax", expr->line > 0 ? expr->line : 1, 0);
            return PROTO_NONE;
        }
    } else {
        Parser parser(source);
        std::unique_ptr<ModuleNode> mod = parser.parseModule();
        if (parser.hasError() || !mod || mod->body.empty()) {
            if (get_env_diag()) {
                fprintf(stderr, "py_compile: parser error at %d:%d: %s\n", parser.getLastErrorLine(), parser.getLastErrorColumn(), parser.getLastErrorMsg().c_str());
            }
            raiseSE(parser.hasError() ? parser.getLastErrorMsg() : "invalid syntax",
                    parser.getLastErrorLine(), parser.getLastErrorColumn());
            return PROTO_NONE;
        }
        if (!compiler.compileModule(mod.get())) {
            int line = (!mod->body.empty() && mod->body[0]) ? mod->body[0]->line : 1;
            raiseSE("invalid syntax", line > 0 ? line : 1, 0);
            return PROTO_NONE;
        }
    }
    return makeCodeObject(context, 
        compiler.getConstants(), 
        compiler.getNames(), 
        compiler.getBytecode(), 
        PythonEnvironment::getInternedString(context, filename.c_str()), 
        nullptr, 0, 0, 0, 0, false, 
        PythonEnvironment::getInternedString(context, "<module>"), 
        compiler.getFirstLine(), 
        compiler.getLnotab());
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
    // eval() uses the eval_input grammar (testlist NEWLINE* ENDMARKER), so
    // bare comma-tuples like `eval('1, 0 or 1')` -> (1, 1) must parse.
    // parseTestList accepts the comma-separated form; parseExpression alone
    // does not.
    std::unique_ptr<ASTNode> expr = parser.parseTestList();
    if (!expr || !parser.atEOF()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::string msg;
            if (parser.hasError() && !parser.getLastErrorMsg().empty()) {
                msg = parser.getLastErrorMsg();
            } else if (!expr) {
                msg = "unexpected EOF while parsing";
            } else if (!parser.atEOF()) {
                msg = "invalid syntax (likely a statement where expression was expected)";
            } else {
                msg = "invalid syntax";
            }

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
        fprintf(stderr, "DEBUG: py_eval source='%s' result=%p\n", source.c_str(), (void*)result);
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
    const proto::ProtoObject* doc = obj->getAttribute(context, env ? env->getDocString() : PythonEnvironment::getInternedString(context, "__doc__"));
    const proto::ProtoObject* nameAttr = obj->getAttribute(context, env ? env->getNameString() : PythonEnvironment::getInternedString(context, "__name__"));
    std::string typeName = "object";
    if (nameAttr && nameAttr->isString(context)) {
        nameAttr->asString(context)->toUTF8String(context, typeName);
    } else {
        const proto::ProtoObject* type = env ? env->getType(context, obj) : obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
        if (type) {
            const proto::ProtoObject* tNameAttr = type->getAttribute(context, env ? env->getNameString() : PythonEnvironment::getInternedString(context, "__name__"));
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
    const proto::ProtoString* reprS = env ? env->getReprString() : PythonEnvironment::getInternedString(context, "__repr__");
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

/** memoryview.tobytes() — returns the raw bytes stored on the instance */
static const proto::ProtoObject* py_memoryview_tobytes(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* data = self->getAttribute(context, PythonEnvironment::getInternedString(context, "__mv_data__"));
    if (!data || data == PROTO_NONE) {
        return PythonEnvironment::getInternedString(context, "")->asObject(context);
    }
    return data;
}

/** memoryview.cast(format[, shape]) — returns a new view with a different format/shape */
static const proto::ProtoObject* py_memoryview_cast_method(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* data = self->getAttribute(context, PythonEnvironment::getInternedString(context, "__mv_data__"));
    if (!data || data == PROTO_NONE) return PROTO_NONE;

    std::string newFmt = "B";
    if (args && args->getSize(context) >= 1) {
        const proto::ProtoObject* fmtObj = args->getAt(context, 0);
        if (fmtObj && fmtObj->isString(context)) {
            fmtObj->asString(context)->toUTF8String(context, newFmt);
        }
    }
    int64_t newNdim = 1;
    if (args && args->getSize(context) >= 2) {
        const proto::ProtoObject* shapeArg = args->getAt(context, 1);
        if (shapeArg && shapeArg != PROTO_NONE) {
            newNdim = 2;
        }
    }

    const proto::ProtoObject* cls = self->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
    if (!cls || cls == PROTO_NONE) return PROTO_NONE;
    proto::ProtoObject* newMv = const_cast<proto::ProtoObject*>(cls->newChild(context, true));
    int64_t totalBytes = data->isString(context) ? (int64_t)data->asString(context)->getSize(context) : 0;
    newMv = const_cast<proto::ProtoObject*>(newMv->setAttribute(context, PythonEnvironment::getInternedString(context, "__mv_data__"), data));
    newMv = const_cast<proto::ProtoObject*>(newMv->setAttribute(context, PythonEnvironment::getInternedString(context, "format"),
        PythonEnvironment::getInternedString(context, newFmt.c_str())->asObject(context)));
    newMv = const_cast<proto::ProtoObject*>(newMv->setAttribute(context, PythonEnvironment::getInternedString(context, "ndim"),
        context->fromInteger(newNdim)));
    newMv = const_cast<proto::ProtoObject*>(newMv->setAttribute(context, PythonEnvironment::getInternedString(context, "nbytes"),
        context->fromInteger(totalBytes)));
    return newMv;
}

/** memoryview(obj): create a memoryview wrapping bytes, bytearray, or array-like objects */
static const proto::ProtoObject* py_memoryview(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);

    // positionalParameters[0] = cls (the class), [1] = object to wrap
    size_t nArgs = positionalParameters ? positionalParameters->getSize(context) : 0;
    if (nArgs < 2) {
        if (env) env->raiseTypeError(context, "memoryview: argument 1 must be a read-only buffer, not nothing");
        return PROTO_NONE;
    }

    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 1);

    if (!cls || cls == PROTO_NONE || !obj) {
        if (env) env->raiseTypeError(context, "memoryview: argument must be a bytes-like object");
        return PROTO_NONE;
    }

    // Extract bytes data and format from the input object
    const proto::ProtoObject* bytesData = nullptr;
    std::string format = "B";
    int64_t ndim = 1;

    // Case 1: input is already a memoryview (has __mv_data__ attribute)
    {
        const proto::ProtoObject* mvData = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__mv_data__"));
        if (mvData && mvData != PROTO_NONE) {
            bytesData = mvData;
            const proto::ProtoObject* fmtObj = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "format"));
            if (fmtObj && fmtObj != PROTO_NONE && fmtObj->isString(context)) {
                fmtObj->asString(context)->toUTF8String(context, format);
            }
            const proto::ProtoObject* ndimObj = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "ndim"));
            if (ndimObj && ndimObj != PROTO_NONE) ndim = ndimObj->asLong(context);
        }
    }

    // Case 2: input is a bytes/string object (protoPython represents both as ProtoString)
    if (!bytesData && obj->isString(context)) {
        bytesData = obj;
        format = "B";
        ndim = 1;
    }

    // Case 3: input has _data attribute (our array stub stores bytes there)
    if (!bytesData) {
        const proto::ProtoObject* arrData = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "_data"));
        if (arrData && arrData != PROTO_NONE && arrData->isString(context)) {
            bytesData = arrData;
            const proto::ProtoObject* tcObj = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "typecode"));
            if (tcObj && tcObj != PROTO_NONE && tcObj->isString(context)) {
                tcObj->asString(context)->toUTF8String(context, format);
            }
            ndim = 1;
        }
    }

    // Case 4: input has tobytes() method (array or similar)
    if (!bytesData && env) {
        const proto::ProtoObject* tobytesMeth = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "tobytes"));
        if (tobytesMeth && tobytesMeth != PROTO_NONE) {
            const proto::ProtoList* emptyArgs = context->newList();
            const proto::ProtoObject* result = ::protoPython::invokePythonCallable(context, tobytesMeth, emptyArgs, nullptr);
            if (result && result != PROTO_NONE && result->isString(context)) {
                bytesData = result;
                ndim = 1;
            }
        }
    }

    if (!bytesData) {
        if (env) env->raiseTypeError(context, "memoryview: a bytes-like object is required");
        return PROTO_NONE;
    }

    int64_t totalBytes = bytesData->isString(context) ? (int64_t)bytesData->asString(context)->getSize(context) : 0;

    // Create the memoryview instance as a child of the class
    proto::ProtoObject* instance = const_cast<proto::ProtoObject*>(cls->newChild(context, true));
    instance = const_cast<proto::ProtoObject*>(instance->setAttribute(context,
        PythonEnvironment::getInternedString(context, "__mv_data__"), bytesData));
    instance = const_cast<proto::ProtoObject*>(instance->setAttribute(context,
        PythonEnvironment::getInternedString(context, "format"),
        PythonEnvironment::getInternedString(context, format.c_str())->asObject(context)));
    instance = const_cast<proto::ProtoObject*>(instance->setAttribute(context,
        PythonEnvironment::getInternedString(context, "ndim"), context->fromInteger(ndim)));
    instance = const_cast<proto::ProtoObject*>(instance->setAttribute(context,
        PythonEnvironment::getInternedString(context, "nbytes"), context->fromInteger(totalBytes)));
    instance = const_cast<proto::ProtoObject*>(instance->setAttribute(context,
        PythonEnvironment::getInternedString(context, "itemsize"), context->fromInteger(1)));

    return instance;
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
    const proto::ProtoObject* obj = self->getAttribute(context, PythonEnvironment::getInternedString(context, "obj"));
    const proto::ProtoObject* type = self->getAttribute(context, PythonEnvironment::getInternedString(context, "type"));
    if (get_env_diag()) {
        std::string name; nameObj->asString(context)->toUTF8String(context, name);
        fprintf(stderr, "DEBUG_SUPER: getattr '%s' self=%p obj=%p type=%p\n",
                name.c_str(), (void*)self, (void*)obj, (void*)type);
        fflush(stderr);
    }
    if (!obj || !type) return nullptr;

    // Search MRO. For Python classes, __mro__ from the `obj` is the source of truth perfectly linearized.
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    
    // In super(type, obj), 'type' is where searching starts in the MRO of 'obj'.
    // If 'obj' is a class, we use its __mro__. If 'obj' is an instance, we use type(obj).__mro__.
    bool isClass = obj->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__mro__")) != PROTO_NONE;
    const proto::ProtoObject* mroSrc = isClass ? obj : (env ? env->getType(context, obj) : nullptr);
    const proto::ProtoObject* mroAttr = mroSrc ? mroSrc->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__mro__")) : nullptr;
    
    std::vector<const proto::ProtoObject*> targets;
    if (mroAttr) {
        const proto::ProtoTuple* mro = mroAttr->asTuple(context);
        if (!mro) {
            const proto::ProtoObject* data = mroAttr->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__data__"));
            if (data) mro = data->asTuple(context);
        }
        
        if (mro) {
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG_SUPER: MRO size=%lu\n", (unsigned long)mro->getSize(context));
                for (size_t i = 0; i < mro->getSize(context); ++i) {
                     std::string cname;
                     const proto::ProtoObject* c = mro->getAt(context, i);
                     const proto::ProtoObject* n = c->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
                     if (n && n->isString(context)) n->asString(context)->toUTF8String(context, cname);
                     fprintf(stderr, "  MRO[%zu]: %p (%s)\n", i, (void*)c, cname.c_str());
                }
            }
            bool foundStart = false;
            for (size_t i = 0; i < mro->getSize(context); ++i) {
                const proto::ProtoObject* clsInMro = mro->getAt(context, static_cast<int>(i));
                if (foundStart) {
                    targets.push_back(clsInMro);
                } else {
                    // Start searching AFTER 'type'
                    if (areSameClasses(context, clsInMro, type)) {
                        foundStart = true;
                    }
                }
            }
        }
    }

    if (targets.empty()) {
        const proto::ProtoList* parents = type->getParents(context);
        if (parents) {
            for (size_t i = 0; i < parents->getSize(context); ++i) {
                targets.push_back(parents->getAt(context, i));
            }
        }
    }

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG_SUPER: targets size=%lu\n", (unsigned long)targets.size());
    }
    for (const proto::ProtoObject* target : targets) {
        if (!target || target == PROTO_NONE) continue;
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG_SUPER_TARGET: target=%p (%s)\n", (void*)target, env ? env->reprObject(context, target).c_str() : "?");
        }

        const proto::ProtoObject* val = target->proto::ProtoObject::getAttribute(context, nameObj->asString(context));
        if (!val || val == PROTO_NONE) continue;

        bool isPythonClass = (target->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__is_python_class__")) != PROTO_NONE);
        bool isObject = (env && target == env->getObjectPrototype());
        if (!isObject && env) {
            // Fallback: check by name in case objectPrototype pointer was updated after MRO construction
            const proto::ProtoObject* tName = target->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
            if (tName && tName->isString(context)) {
                std::string tNameStr;
                tName->asString(context)->toUTF8String(context, tNameStr);
                isObject = (tNameStr == "object");
            }
        }
        const proto::ProtoString* codeStr = env ? env->getCodeString() : PythonEnvironment::getInternedString(context, "__code__");
        const proto::ProtoObject* codeAttr = val->proto::ProtoObject::getAttribute(context, codeStr, false);
        if (!codeAttr || codeAttr == PROTO_NONE) {
            // Check for wrapped function in staticmethod/classmethod
            const proto::ProtoObject* func = val->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__func__"), false);
            if (func && func != PROTO_NONE) {
                codeAttr = func->proto::ProtoObject::getAttribute(context, codeStr, false);
            }
        }
        bool hasCode = (codeAttr && codeAttr != PROTO_NONE && (env ? codeAttr != env->getNonePrototype() : true));
        bool isNativeCallable = (val->asMethod(context) != nullptr);
        // A descriptor (anything with __get__) owned by `target` is a legit
        // super() result.  This covers @property, @classmethod, @staticmethod,
        // and user-defined data descriptors.  Without this branch, super().X
        // where X is a parent-class @property silently skipped past the
        // descriptor and continued searching the MRO, ultimately raising
        // AttributeError on `type`.  Visible at socket.py:527 where the
        // child's `family` @property does `return _intenum_converter(
        // super().family, AddressFamily)` and has been broken since the
        // initial protoPython bring-up of socket.
        bool isOwnedDescriptor = false;
        if (target->proto::ProtoObject::hasOwnAttribute(context, nameObj->asString(context)) == PROTO_TRUE) {
            const proto::ProtoString* getStrCheck = env ? env->getGetDunderString() : PythonEnvironment::getInternedString(context, "__get__");
            const proto::ProtoObject* dg = env ? env->getAttribute(context, val, getStrCheck, false) : val->getAttribute(context, getStrCheck);
            if (dg && dg != PROTO_NONE && dg->asMethod(context)) isOwnedDescriptor = true;
        }

        bool legit = false;
        if (hasCode) {
            legit = true;
        } else if (isObject) {
            legit = true;
        } else if (isNativeCallable && target->proto::ProtoObject::hasOwnAttribute(context, nameObj->asString(context)) == PROTO_TRUE) {
            // Native C++ method owned by a class in the MRO is always legit (e.g. object.__init__)
            legit = true;
        } else if (isOwnedDescriptor) {
            // @property / classmethod / staticmethod / user descriptor in target.
            legit = true;
        } else if (!isPythonClass) {
            if (target->proto::ProtoObject::hasOwnAttribute(context, nameObj->asString(context)) == PROTO_TRUE) legit = true;
        }

        if (get_env_diag()) {
            std::string n; nameObj->asString(context)->toUTF8String(context, n);
            fprintf(stderr, "DEBUG_SUPER_SEARCH: target=%p attr=%s legit=%d isPy=%d hasCode=%d isObj=%d\n", 
                    (void*)target, n.c_str(), (int)legit, (int)isPythonClass, (int)hasCode, (int)isObject);
        }

        if (!legit) continue;

        if (val && val != PROTO_NONE) {
            if (get_env_diag()) {
                std::string n; nameObj->asString(context)->toUTF8String(context, n);
                const proto::ProtoObject* codeAttr = val->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__code__"));
                fprintf(stderr, "DEBUG_SUPER: found '%s' in target %p val=%p asMethod=%d code=%p\n", 
                        n.c_str(), (void*)target, (void*)val, (val ? (val->asMethod(context) != nullptr) : 0), (void*)codeAttr);
                fflush(stderr);
            }
            const proto::ProtoString* getStr = env ? env->getGetDunderString() : PythonEnvironment::getInternedString(context, "__get__");
            const proto::ProtoObject* descrGet = env ? env->getAttribute(context, val, getStr, false) : val->getAttribute(context, getStr);
            
            if (descrGet && descrGet != PROTO_NONE && descrGet->asMethod(context)) {
                const proto::ProtoList* args = context->newList()->appendLast(context, obj)->appendLast(context, type);
                return descrGet->asMethod(context)(context, val, nullptr, args, nullptr);
            }
            if (val->asMethod(context)) {
                const proto::ProtoObject* bound = context->newObject(true);
                if (env && env->getMethodPrototype()) {
                    bound = const_cast<proto::ProtoObject*>(bound->addParent(context, env->getMethodPrototype()));
                    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, env->getClassString(), env->getMethodPrototype()));
                }
                bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, PythonEnvironment::getInternedString(context, "__self__"), obj));
                bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, PythonEnvironment::getInternedString(context, "__func__"), val));
                return bound;
            }
            return val;
        }
    }
    
    if (get_env_diag()) {
        std::string n; nameObj->asString(context)->toUTF8String(context, n);
        fprintf(stderr, "DEBUG_SUPER: getattr '%s' failed to find attribute in targets (count=%zu)\n", n.c_str(), targets.size());
        fflush(stderr);
    }
    
    if (env) {
        std::string n;
        nameObj->asString(context)->toUTF8String(context, n);
        env->raiseAttributeError(context, type, n);
    }
    return nullptr;
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
    const proto::ProtoObject* obj = self->getAttribute(context, PythonEnvironment::getInternedString(context, "obj"));
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;

    // Call setAttribute directly on the bound object
    return obj->setAttribute(context, nameObj->asString(context), valueObj);
}

static const proto::ProtoObject* py_super_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* type = self->getAttribute(context, PythonEnvironment::getInternedString(context, "type"));
    const proto::ProtoObject* obj = self->getAttribute(context, PythonEnvironment::getInternedString(context, "obj"));
    
    std::string typeName = "None";
    if (type && type != PROTO_NONE) {
        const proto::ProtoObject* n = type->getAttribute(context, env ? env->getNameString() : PythonEnvironment::getInternedString(context, "__name__"));
        if (n && n->isString(context)) n->asString(context)->toUTF8String(context, typeName);
    }
    
    char buf[128];
    snprintf(buf, sizeof(buf), "<super: <class '%s'>, %p>", typeName.c_str(), (void*)obj);
    return PythonEnvironment::getInternedString(context, buf)->asObject(context);
}

static const proto::ProtoObject* py_super_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // SP-B/B2 fix: previously a no-op stub.  super().__init__(args) was
    // intercepted by this method and silently dropped, so the parent
    // class's __init__ never ran.  Now: forward to the parent's __init__
    // by routing through py_super_getattr (which performs the MRO walk
    // and binds the result to self.obj), then invoke that bound method
    // with the original args/kwargs.
    (void)parentLink;
    if (!self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;

    // Build a positional list with just the attribute name "__init__" and
    // call py_super_getattr to obtain the bound parent-method.
    const proto::ProtoString* initName = PythonEnvironment::getInternedString(context, "__init__");
    const proto::ProtoObject* initNameObj = initName ? initName->asObject(context) : nullptr;
    const proto::ProtoList* getattrArgs = context->newList()->appendLast(context, initNameObj);
    const proto::ProtoObject* bound = py_super_getattr(context, self, nullptr, getattrArgs, nullptr);
    if (!bound || bound == PROTO_NONE) return PROTO_NONE;

    // Forward original args/kwargs through callObjectEx.
    std::vector<const proto::ProtoObject*> argsVec;
    if (positionalParameters) {
        long n = positionalParameters->getSize(context);
        for (long i = 0; i < n; ++i) {
            argsVec.push_back(positionalParameters->getAt(context, static_cast<int>(i)));
        }
    }
    // Reconstruct (name, value) pairs from the hash-keyed kwargs sparse
    // list using the kwNames tuple pushed by the calling site (see
    // PythonEnvironment::pushKwNames / getCurrentKwNames).  Names live
    // out-of-band; the sparse list only stores hash → value.
    std::vector<std::pair<std::string, const proto::ProtoObject*>> kwVec;
    if (keywordParameters) {
        const proto::ProtoTuple* kwNames = env->getCurrentKwNames();
        if (kwNames) {
            long n = kwNames->getSize(context);
            for (long i = 0; i < n; ++i) {
                const proto::ProtoObject* k = kwNames->getAt(context, static_cast<int>(i));
                if (!k || !k->isString(context)) continue;
                const proto::ProtoString* ks = k->asString(context);
                if (!keywordParameters->has(context, ks->getHash(context))) continue;
                const proto::ProtoObject* v = keywordParameters->getAt(context, ks->getHash(context));
                std::string s; ks->toUTF8String(context, s);
                kwVec.emplace_back(std::move(s), v);
            }
        }
    }
    return env->callObjectEx(bound, argsVec, kwVec);
}

static const proto::ProtoObject* py_super(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;

    const proto::ProtoObject* type = nullptr;
    const proto::ProtoObject* obj = nullptr;
    
    if (positionalParameters->getSize(context) == 0) {
       // 0-arg super(): deduce from frame
       PythonEnvironment* env = PythonEnvironment::fromContext(context);
       const proto::ProtoObject* frame = env ? env->getCurrentFrame() : nullptr;
       if (!frame) return PROTO_NONE;
       
       // Get first argument (self/cls) from locals
       // Check fast locals (slots) first if available
       if (context->getAutomaticLocalsCount() > 0 && context->getAutomaticLocals()) {
           obj = context->getAutomaticLocals()[0];
           if (get_env_diag()) fprintf(stderr, "DEBUG: py_super found obj in fast locals: %p\n", (void*)obj);
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
                               if (get_env_diag()) {
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
               const proto::ProtoObject* slowObj = locals->getAttribute(context, PythonEnvironment::getInternedString(context, "self"));
               if (slowObj && slowObj != PROTO_NONE) {
                   obj = slowObj;
               } else {
                   obj = locals->getAttribute(context, PythonEnvironment::getInternedString(context, "cls"));
                   if (!obj || obj == PROTO_NONE) {
                        obj = locals->getAttribute(context, PythonEnvironment::getInternedString(context, "metacls"));
                        if (!obj || obj == PROTO_NONE) {
                             obj = locals->getAttribute(context, PythonEnvironment::getInternedString(context, "mcls"));
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
                     if (get_env_diag()) fprintf(stderr, "DEBUG: py_super BFS checking locals of scope %p\n", (void*)curr);
                     // Fix: locals is a ProtoObject (dict or frame), not a SparseList directly.
                     // Use getAttribute to retrieve the value.
                     const proto::ProtoObject* val = PROTO_NONE;
                      if (locals->hasOwnAttribute(context, classStr) == PROTO_TRUE) {
                           val = locals->getAttribute(context, classStr);
                      }
                      if (val && val != PROTO_NONE) {
                          type = val;
                          if (get_env_diag()) fprintf(stderr, "DEBUG: py_super found __class__ in f_locals of scope %p\n", (void*)curr);
                          goto found_class;
                     }
               }
               
               // 2. Check closure via code object freevars
               const proto::ProtoObject* code = curr->getAttribute(context, env->getFCodeString());
               const proto::ProtoObject* closure = curr->getAttribute(context, env->getClosureString());

               if (get_env_diag()) {
                   fprintf(stderr, "DEBUG: py_super BFS scope %p: code=%p, closure=%p, closure_is_tuple=%d\n", 
                           (void*)curr, (void*)code, (void*)closure, 
                           (closure && closure->isTuple(context)));
               }
               
               if (code && code != PROTO_NONE && closure && closure->isTuple(context)) {
                     const proto::ProtoObject* freevars = code->getAttribute(context, PythonEnvironment::getInternedString(context, "co_freevars"));
                     if (freevars && freevars->isTuple(context)) {
                          const proto::ProtoTuple* freeTup = freevars->asTuple(context);
                          const proto::ProtoTuple* closureTup = closure->asTuple(context);
                          for (size_t i = 0; i < freeTup->getSize(context); ++i) {
                               const proto::ProtoObject* name = freeTup->getAt(context, i);
                               if (name->isString(context) && name->asString(context)->cmp_to_string(context, classStr) == 0) {
                                    if (get_env_diag()) fprintf(stderr, "DEBUG: py_super found __class__ in freevars at index %zu\n", i);
                                    if (i < closureTup->getSize(context)) {
                                         const proto::ProtoObject* cell = closureTup->getAt(context, i);
                                         // Use getAttribute("cell_contents")
                                         const proto::ProtoObject* val = cell->getAttribute(context, PythonEnvironment::getInternedString(context, "cell_contents"));
                                         if (val && val != PROTO_NONE) {
                                              type = val;
                                              if (get_env_diag()) fprintf(stderr, "DEBUG: py_super retrieved __class__ from closure cell\n");
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
                        const proto::ProtoObject* co_name = codeObj->getAttribute(context, PythonEnvironment::getInternedString(context, "co_name"));
                        bool isClass = obj->hasOwnAttribute(context, PythonEnvironment::getInternedString(context, "__mro__")) == PROTO_TRUE;
                        const proto::ProtoObject* mroSrc = isClass ? obj : env->getType(context, obj);
                        const proto::ProtoObject* mroObj = mroSrc ? mroSrc->getAttribute(context, PythonEnvironment::getInternedString(context, "__mro__")) : nullptr;
                        const proto::ProtoTuple* mro = (mroObj && mroObj != PROTO_NONE) ? mroObj->asTuple(context) : nullptr;
                        if (get_env_diag()) fprintf(stderr, "DEBUG_SUPER_MRO_CHECK: obj=%p isClass=%d mroSrc=%p mroObj=%p mro=%p co_name=%p co_nameStr=%d\n", (void*)obj, isClass, (void*)mroSrc, (void*)mroObj, (void*)mro, (void*)co_name, co_name ? co_name->isString(context) : 0);
                        if (mro && co_name && co_name->isString(context)) {
                            for (size_t i = 0; i < mro->getSize(context); ++i) {
                                const proto::ProtoObject* cls = mro->getAt(context, i);
                                if (get_env_diag()) fprintf(stderr, "DEBUG_SUPER_MRO: cls=%p hasOwn=%d\n", (void*)cls, cls->hasOwnAttribute(context, co_name->asString(context)) == PROTO_TRUE);
                                if (cls->hasOwnAttribute(context, co_name->asString(context)) == PROTO_TRUE) {
                                    const proto::ProtoObject* attr = cls->getAttribute(context, co_name->asString(context));
                                    const proto::ProtoObject* attrCode = attr ? attr->getAttribute(context, PythonEnvironment::getInternedString(context, "__code__")) : nullptr;
                                    if (!attrCode || attrCode == PROTO_NONE) {
                                        const proto::ProtoObject* func = attr ? attr->getAttribute(context, PythonEnvironment::getInternedString(context, "__func__")) : nullptr;
                                        if (func && func != PROTO_NONE) {
                                            attrCode = func->getAttribute(context, PythonEnvironment::getInternedString(context, "__code__"));
                                        }
                                    }
                                    if (get_env_diag()) fprintf(stderr, "DEBUG_SUPER_MRO: cls=%p attr=%p attrCode=%p codeObj=%p\n", (void*)cls, (void*)attr, (void*)attrCode, (void*)codeObj);
                                    if (attrCode == codeObj) {
                                        type = cls;
                                        if (get_env_diag()) fprintf(stderr, "DEBUG_SUPER_MRO: MATCH (PTR)! type set to %p\n", (void*)type);
                                        break;
                                    } else if (attrCode && attrCode != PROTO_NONE && codeObj) {
                                        // Robust fallback for code match (names)
                                        const proto::ProtoObject* n1 = attrCode->getAttribute(context, PythonEnvironment::getInternedString(context, "co_name"));
                                        const proto::ProtoObject* n2 = codeObj->getAttribute(context, PythonEnvironment::getInternedString(context, "co_name"));
                                        if (n1 && n2 && n1 == n2 && n1 != PROTO_NONE) {
                                            type = cls;
                                            if (get_env_diag()) fprintf(stderr, "DEBUG_SUPER_MRO: MATCH (NAME)! type set to %p\n", (void*)type);
                                            break;
                                        }
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
                        type = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
                    }
                }
                if (get_env_diag()) {
                    std::string tname = "Unknown";
                    if (type && type != PROTO_NONE) {
                        const proto::ProtoObject* n = type->getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
                        if (n && n->isString(context)) n->asString(context)->toUTF8String(context, tname);
                    }
                    fprintf(stderr, "DEBUG_SUPER_DEDUCTION: inferred type=%p (%s) from obj=%p\n", (void*)type, tname.c_str(), (void*)obj);
                }
            }
        } else {
            if (get_env_diag()) fprintf(stderr, "DEBUG: py_super 0-arg failed to find self/cls\n");
            return PROTO_NONE;
        }
    } else {
        // CPython super() supports three forms:
        //   super()              — zero-arg, deduced (handled above)
        //   super(type)          — unbound super; obj is None until bound via __get__
        //   super(type, obj)     — bound super
        // Reading positionalParameters[1] when only one arg was passed
        // walked off the end of the ProtoList, returned a wild pointer,
        // and crashed in the proxy attribute access — visible as test_descr
        // SIGSEGV in test_supers (`super(C)` line in the test body).
        type = positionalParameters->getAt(context, 0);
        if (positionalParameters->getSize(context) >= 2) {
            obj = positionalParameters->getAt(context, 1);
        } else {
            obj = PROTO_NONE;
        }
    }

    if (!type) {
        if (get_env_diag()) {
             fprintf(stderr, "DEBUG: py_super returning NONE (type=%p, obj=%p)\n", (void*)type, (void*)obj);
        }
        return PROTO_NONE;
    }
    if (!obj) obj = PROTO_NONE;

    if (get_env_diag()) {
        std::string tname = "Unknown";
        const proto::ProtoObject* n = type->getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
        if (n && n->isString(context)) n->asString(context)->toUTF8String(context, tname);
        fprintf(stderr, "DEBUG: py_super returning PROXY for type=%s(%p) obj=%p\n", tname.c_str(), (void*)type, (void*)obj);
    }
    proto::ProtoObject* proxy = const_cast<proto::ProtoObject*>(context->newObject(true));
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "type"), type);
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "obj"), obj);
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "__getattr__"), context->fromMethod(proxy, py_super_getattr));
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "__setattr__"), context->fromMethod(proxy, py_super_setattr));
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "__repr__"), context->fromMethod(proxy, py_super_repr));
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "__init__"), context->fromMethod(proxy, py_super_init));
    // Fast-path OBJ-level dispatch: hasOwnAttribute(__py_getattr_handler__) replaces
    // the old getAttribute(__is_super_proxy__) chain walk in tryFastGetAttribute.
    proxy->setAttribute(context, PythonEnvironment::getInternedString(context, "__py_getattr_handler__"), context->fromMethod(proxy, py_super_getattr));

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
        const proto::ProtoObject* hook = sys->getAttribute(context, PythonEnvironment::getInternedString(context, "breakpointhook"));
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
    const proto::ProtoString* dictS = env ? env->getDictDunderString() : PythonEnvironment::getInternedString(ctx, "__dict__");

    // PI: use env->getAttribute so the type chain (and the
    // objectPrototype.__dict__ method) is found.  The bare
    // obj->getAttribute only walks own attrs.
    const proto::ProtoObject* dict = env ? env->getAttribute(ctx, obj, dictS) : obj->getAttribute(ctx, dictS);
    // __dict__ on objectPrototype is installed as a method
    // (py_object_get_dict) — invoke it to materialise the dict, just
    // like LOAD_ATTR's auto-invoke special-case.
    if (dict && dict != PROTO_NONE && dict->asMethod(ctx) && dict->asMethodSelf(ctx) != nullptr) {
        const proto::ProtoObject* materialised = dict->asMethod(ctx)(ctx,
            const_cast<proto::ProtoObject*>(dict->asMethodSelf(ctx)),
            nullptr, ctx->newList(), nullptr);
        if (materialised) dict = materialised;
    }
    if (dict && dict != PROTO_NONE) return dict;

    return obj;
}

/** Compare two objects for sorting: int, string, else compare(). */
static int sorted_compare(proto::ProtoContext* context, const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a == b) return 0;
    if (a->isInteger(context) && b->isInteger(context)) {
        // Integer::compare is bignum-safe.
        return a->compare(context, b);
    }
    if (a->isString(context) && b->isString(context)) {
        std::string sa;
        std::string sb;
        a->asString(context)->toUTF8String(context, sa);
        b->asString(context)->toUTF8String(context, sb);
        if (sa == sb) return 0;
        return sa < sb ? -1 : 1;
    }
    // bytes/bytearray: try __lt__ if defined; this lets the
    // Y-round/Z-round py_bytes_lt run, which compares by raw octets.
    {
        ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
        const proto::ProtoObject* bytesProto = env ? env->getBytesPrototype() : nullptr;
        if (bytesProto) {
            const proto::ProtoString* dataS = ::protoPython::PythonEnvironment::getInternalString(context, "__data__");
            const proto::ProtoObject* aData = a->getAttribute(context, dataS);
            const proto::ProtoObject* bData = b->getAttribute(context, dataS);
            bool aIsBytes = aData && (aData->isByteBuffer(context) || aData->isString(context));
            bool bIsBytes = bData && (bData->isByteBuffer(context) || bData->isString(context));
            if (aIsBytes && bIsBytes) {
                const proto::ProtoString* ltS = ::protoPython::PythonEnvironment::getInternedString(context, "__lt__");
                const proto::ProtoObject* ltM = a->getAttribute(context, ltS);
                if (ltM && ltM->asMethod(context)) {
                    const proto::ProtoList* args = context->newList()->appendLast(context, b);
                    const proto::ProtoObject* res = ltM->asMethod(context)(context, a, nullptr, args, nullptr);
                    if (res == PROTO_TRUE) return -1;
                    if (res == PROTO_FALSE) {
                        // Could be equal — invoke __eq__.
                        const proto::ProtoString* eqS = ::protoPython::PythonEnvironment::getInternedString(context, "__eq__");
                        const proto::ProtoObject* eqM = a->getAttribute(context, eqS);
                        if (eqM && eqM->asMethod(context)) {
                            const proto::ProtoObject* eqr = eqM->asMethod(context)(context, a, nullptr, args, nullptr);
                            if (eqr == PROTO_TRUE) return 0;
                        }
                        return 1;
                    }
                }
            }
        }
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
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, env ? env->getIterString() : PythonEnvironment::getInternedString(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, env ? env->getEmptyList() : context->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__"));
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
    listObj->setAttribute(context, env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__"), resultList->asObject(context));
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
    const proto::ProtoObject* hashMethod = obj->getAttribute(context, env ? env->getHashString() : PythonEnvironment::getInternedString(context, "__hash__"));
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
        const proto::ProtoObject* data = (obj->hasOwnAttribute(context, PythonEnvironment::getInternedString(context, "__data__")) == PROTO_TRUE) ? obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__data__")) : nullptr;
        if (get_env_diag() && data) { fprintf(stderr, "DEBUG_HASATTR: obj has __data__=%p\n", (void*)data); fflush(stderr); }
    }

    // 1. Direct chain probe via protoCore.
    if (obj->hasAttribute(context, nameStr) == PROTO_TRUE) return PROTO_TRUE;

    // 2. CPython semantics: an instance also "has" attributes defined on its
    //    class's MRO. protoCore's chain walk follows the linearised parent
    //    chain captured at instantiation time, which doesn't track later
    //    additions to a class. Walk type(obj).__mro__ explicitly so attrs
    //    set on a parent class after the instance was created remain visible.
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (env) {
        const proto::ProtoObject* cls = env->getType(context, obj);
        const proto::ProtoString* mroS = env->getMroString();
        const proto::ProtoObject* mroObj = (cls && mroS) ? cls->getAttribute(context, mroS) : nullptr;
        const proto::ProtoTuple* mroT = (mroObj && mroObj != PROTO_NONE) ? mroObj->asTuple(context) : nullptr;
        if (mroT) {
            for (unsigned long i = 0; i < mroT->getSize(context); ++i) {
                const proto::ProtoObject* base = mroT->getAt(context, i);
                if (base && base != PROTO_NONE && base != obj) {
                    if (base->hasOwnAttribute(context, nameStr) == PROTO_TRUE) return PROTO_TRUE;
                }
            }
        }

        // 3. CPython semantics again: `hasattr(obj, name)` should be true if
        //    `getattr(obj, name)` succeeds, which includes the __getattr__
        //    fallback.  The chain probe above only sees attributes that are
        //    already materialised; objects that synthesise attributes on
        //    demand via __getattr__ (e.g. unittest's _FailedTest, super()
        //    proxies, lazy modules) would wrongly report False.
        const proto::ProtoString* getattrKey = PythonEnvironment::getInternedString(context, "__getattr__");
        const proto::ProtoObject* getattrFn = nullptr;
        bool getattrIsOwn = false;
        if (obj->hasOwnAttribute(context, getattrKey) == PROTO_TRUE) {
            getattrFn = obj->getAttribute(context, getattrKey);
            getattrIsOwn = true;
        } else {
            const proto::ProtoObject* cls = env->getType(context, obj);
            if (cls && cls != PROTO_NONE) {
                getattrFn = env->getAttribute(context, cls, getattrKey, false);
            }
        }
        if (getattrFn && getattrFn != PROTO_NONE) {
            std::vector<const proto::ProtoObject*> args = getattrIsOwn
                ? std::vector<const proto::ProtoObject*>{nameObj}
                : std::vector<const proto::ProtoObject*>{obj, nameObj};
            const proto::ProtoObject* probe = env->callObject(getattrFn, args);
            if (env->hasPendingException()) {
                // __getattr__ itself raised — by CPython contract, hasattr
                // suppresses AttributeError but lets others propagate.  We
                // still want hasattr() to return False for AttributeError.
                const proto::ProtoObject* exc = env->peekPendingException();
                if (exc) {
                    const proto::ProtoObject* cls2 = env->getType(context, exc);
                    const proto::ProtoString* nameS = env->getNameString();
                    const proto::ProtoObject* nameAttr = cls2 ? cls2->getAttribute(context, nameS) : nullptr;
                    if (nameAttr && nameAttr->isString(context)) {
                        std::string clsName;
                        nameAttr->asString(context)->toUTF8String(context, clsName);
                        if (clsName == "AttributeError") {
                            env->clearPendingException();
                            return PROTO_FALSE;
                        }
                    }
                }
                return nullptr; // propagate non-AttributeError
            }
            if (probe) return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
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

static bool areSameClasses(proto::ProtoContext* context, const proto::ProtoObject* c1, const proto::ProtoObject* c2) {
    return c1 == c2;
}

static const proto::ProtoList* computeC3MRO(proto::ProtoContext* context, const proto::ProtoObject* cls, const proto::ProtoObject* basesObj) {
    auto asTupleRaw = [&](const proto::ProtoObject* obj) -> const proto::ProtoTuple* {
        if (!obj) return nullptr;
        const proto::ProtoTuple* t = obj->asTuple(context);
        if (t) return t;
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoString* dataS = env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__");
        // Try robust first
        const proto::ProtoObject* dataAttr = env ? env->getAttribute(context, obj, dataS, false) : obj->getAttribute(context, dataS);
        if (!dataAttr || dataAttr == PROTO_NONE) dataAttr = obj->proto::ProtoObject::getAttribute(context, dataS);
        if (dataAttr) return dataAttr->asTuple(context);
        return nullptr;
    };

    const proto::ProtoTuple* bases = asTupleRaw(basesObj);
    if (!bases || bases->getSize(context) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* objProto = env ? env->getObjectPrototype() : nullptr;
        if (cls == objProto || !objProto) {
            return context->newList()->appendLast(context, cls);
        }
        const proto::ProtoList* res = context->newList()->appendLast(context, cls);
        res = res->appendLast(context, objProto);
        return res;
    }
    
    std::vector<const proto::ProtoList*> mros;
    mros.reserve(bases->getSize(context) + 1);
    for (size_t i = 0; i < bases->getSize(context); ++i) {
        const proto::ProtoObject* baseCls = bases->getAt(context, i);
        const proto::ProtoObject* mroAttr = baseCls->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__mro__"));
        const proto::ProtoTuple* tup = nullptr;
        if (mroAttr) {
            tup = mroAttr->asTuple(context);
            if (!tup) {
                PythonEnvironment* env = PythonEnvironment::fromContext(context);
                const proto::ProtoString* dataS = env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__");
                const proto::ProtoObject* dataAttr = mroAttr->proto::ProtoObject::getAttribute(context, dataS);
                if (dataAttr) {
                    tup = dataAttr->asTuple(context);
                    if (!tup && dataAttr->asList(context)) {
                        mros.push_back(dataAttr->asList(context));
                        continue;
                    }
                }
            }
        }
        if (tup) {
            mros.push_back(tup->asList(context));
        } else if (baseCls->asList(context)) {
            mros.push_back(baseCls->asList(context));
        } else {
            // Check for __mro__ in baseCls itself if it's acting as its own MRO (fallback)
            mros.push_back(context->newList()->appendLast(context, baseCls));
        }
    }
    
    const proto::ProtoList* result = context->newList()->appendLast(context, cls);
    
    // Optimized C3 merge: track cursor-style heads instead of slicing lists
    std::vector<size_t> heads(mros.size(), 0);
    
    while (true) {
        bool allEmpty = true;
        for (size_t i = 0; i < mros.size(); ++i) {
            if (heads[i] < mros[i]->getSize(context)) {
                allEmpty = false;
                break;
            }
        }
        if (allEmpty) break;
        
        const proto::ProtoObject* candidate = nullptr;
        for (size_t i = 0; i < mros.size(); ++i) {
            if (heads[i] >= mros[i]->getSize(context)) continue;
            
            const proto::ProtoObject* cand = mros[i]->getAt(context, static_cast<int>(heads[i]));
            bool foundInTail = false;
            for (size_t j = 0; j < mros.size(); ++j) {
                for (size_t k = heads[j] + 1; k < mros[j]->getSize(context); ++k) {
                    // Use pointer identity: two class objects are the same only if they are
                    // the exact same object. Name-based comparison causes false matches when
                    // a subclass has the same __name__ as its base (e.g. class Random(_random.Random)).
                    if (areSameClasses(context, mros[j]->getAt(context, static_cast<int>(k)), cand)) {
                        foundInTail = true;
                        break;
                    }
                }
                if (foundInTail) break;
            }
            if (!foundInTail) {
                candidate = cand;
                break;
            }
        }

        if (candidate) {
            // Deduplicate: only add if not already in result. Use pointer identity.
            bool alreadyIn = false;
            for (unsigned long r = 0; r < result->getSize(context); ++r) {
                if (areSameClasses(context, result->getAt(context, static_cast<int>(r)), candidate)) {
                    alreadyIn = true;
                    break;
                }
            }
            if (!alreadyIn) {
                result = result->appendLast(context, candidate);
            }

            for (size_t i = 0; i < mros.size(); ++i) {
                if (heads[i] < mros[i]->getSize(context) && mros[i]->getAt(context, static_cast<int>(heads[i])) == candidate) {
                    heads[i]++;
                }
            }
        } else {
            // C3 failure (inconsistent MRO), just break or fallback
            break; 
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

const proto::ProtoObject* py_genericalias_new(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 3) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* origin = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* args = positionalParameters->getAt(context, 2);

    const proto::ProtoObject* instance = cls->newChild(context, true);
    if (get_env_diag()) fprintf(stderr, "DEBUG py_genericalias_new cls=%p origin=%p instance=%p\n", (void*)cls, (void*)origin, (void*)instance);
    instance = instance->setAttribute(context, PythonEnvironment::getInternedString(context, "__origin__"), origin);
    instance = instance->setAttribute(context, PythonEnvironment::getInternedString(context, "__args__"), args);
    return instance;
}

const proto::ProtoObject* py_genericalias_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (get_env_diag()) fprintf(stderr, "DEBUG py_genericalias_repr entered self=%p\n", (void*)self);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* origin = self->getAttribute(context, PythonEnvironment::getInternedString(context, "__origin__"));
    const proto::ProtoObject* args = self->getAttribute(context, PythonEnvironment::getInternedString(context, "__args__"));
    
    std::string originRepr = env ? env->reprObject(context, origin) : "???";
    std::string argsRepr = env ? env->reprObject(context, args) : "???";
    
    // CPython format: origin[args]
    // If args is a tuple, repr(args) is (x, y). We want [x, y].
    if (args && args->isTuple(context)) {
        if (argsRepr.size() >= 2 && argsRepr.front() == '(' && argsRepr.back() == ')') {
            argsRepr = "[" + argsRepr.substr(1, argsRepr.size() - 2) + "]";
        }
    } else {
        argsRepr = "[" + argsRepr + "]";
    }
    
    return context->fromUTF8String((originRepr + argsRepr).c_str());
}

const proto::ProtoObject* py_uniontype_new(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* args = positionalParameters->getAt(context, 1);

    const proto::ProtoObject* instance = cls->newChild(context, true);
    instance = instance->setAttribute(context, PythonEnvironment::getInternedString(context, "__args__"), args);
    return instance;
}

const proto::ProtoObject* py_uniontype_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* args = self->getAttribute(context, PythonEnvironment::getInternedString(context, "__args__"));
    if (!args || !args->isTuple(context)) return context->fromUTF8String("??? | ???");
    
    const proto::ProtoTuple* tup = args->asTuple(context);
    std::string res;
    for (size_t i = 0; i < tup->getSize(context); ++i) {
        if (i > 0) res += " | ";
        res += env ? env->reprObject(context, tup->getAt(context, i)) : "???";
    }
    return context->fromUTF8String(res.c_str());
}

const proto::ProtoObject* py_type_class_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG py_type_class_getitem self=%p env=%p gaProto=%p\n", (void*)self, (void*)env, (void*)(env ? env->getGenericAliasProto() : nullptr));
    }
    if (!env || !env->getGenericAliasProto()) return self;
    
    if (positionalParameters->getSize(context) < 1) return self;
    const proto::ProtoObject* args = positionalParameters->getAt(context, 0);
    
    const proto::ProtoList* gaArgs = context->newList()->appendLast(context, env->getGenericAliasProto())
                                                      ->appendLast(context, self)
                                                      ->appendLast(context, args);
    return py_genericalias_new(context, nullptr, nullptr, gaArgs, nullptr);
}

const proto::ProtoObject* py_type_or(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env || !env->getUnionTypeProto()) return self;
    
    if (positionalParameters->getSize(context) < 1) return self;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    
    // Create a tuple (self, other) for UnionType.__args__
    const proto::ProtoTuple* args = context->newTuple({self, other});
    
    const proto::ProtoList* utArgs = context->newList()->appendLast(context, env->getUnionTypeProto())
                                                      ->appendLast(context, args->asObject(context));
    return py_uniontype_new(context, nullptr, nullptr, utArgs, nullptr);
}

namespace builtins {

const proto::ProtoObject* py_type_mro(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    
    if (get_env_diag()) {
    }
    
    // self can be 'type' or a metaclass, or 'cls' if called as cls.mro()
    const proto::ProtoObject* cls = self;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* firstArg = positionalParameters->getAt(context, 0);
        // If called as type.mro(cls)
        if (firstArg) cls = firstArg;
    }
    
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* mroName = PythonEnvironment::getInternedString(context, "__mro__");
    
    const_cast<proto::ProtoObject*>(cls); // ensure we can use it
    const proto::ProtoObject* mroAttr = cls->getAttribute(context, mroName);
    
    // If it's a descriptor (like the one we found), we need to get its value
    if (mroAttr && mroAttr->hasAttribute(context, env ? env->getGetDunderString() : PythonEnvironment::getInternedString(context, "__get__"))) {
        const proto::ProtoList* args = context->newList()->appendLast(context, cls)->appendLast(context, cls); // (self, instance, owner)
        const proto::ProtoObject* getter = mroAttr->getAttribute(context, env ? env->getGetDunderString() : PythonEnvironment::getInternedString(context, "__get__"));
        if (getter && getter->asMethod(context)) {
            mroAttr = getter->asMethod(context)(context, mroAttr, nullptr, args, nullptr);
        }
    }

    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    const proto::ProtoString* dataName = env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__");
    
    if (mroAttr) {
        const proto::ProtoTuple* mroTuple = mroAttr->asTuple(context);
        if (mroTuple) {
            const proto::ProtoList* mroList = context->newList();
            for (int i = 0; i < (int)mroTuple->getSize(context); ++i) {
                mroList = mroList->appendLast(context, mroTuple->getAt(context, i));
            }
            if (listProto) {
                const proto::ProtoObject* res = listProto->newChild(context, true);
                const_cast<proto::ProtoObject*>(res)->setAttribute(context, dataName, mroList->asObject(context));
                return res;
            }
            return mroList->asObject(context);
        }
    }
    
    // Fallback: return [cls, object]
    const proto::ProtoList* fallback = context->newList()->appendLast(context, cls);
    const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;
    if (objectProto && cls != objectProto) {
        fallback = fallback->appendLast(context, objectProto);
    }
    
    if (listProto) {
        const proto::ProtoObject* res = listProto->newChild(context, true);
        const_cast<proto::ProtoObject*>(res)->setAttribute(context, dataName, fallback->asObject(context));
        return res;
    }
    
    return fallback->asObject(context);
}

const proto::ProtoObject* py_type_init(
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
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_type executing early unconditional: size=%zu\n", positionalParameters ? positionalParameters->getSize(context) : 0);
    }
    
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* typeProto = env ? env->getTypePrototype() : nullptr;
    
    // Decoration: runUserClassCall moved to py_type_call
    if (!positionalParameters || positionalParameters->getSize(context) == 0) {
        return PROTO_NONE;
    }
    size_t argCount = positionalParameters ? positionalParameters->getSize(context) : 0;
    if (get_env_diag()) {
        std::string sRepr = env ? env->reprObject(context, self) : "???";
        fprintf(stderr, "DEBUG py_type: TOP self=%s argCount=%zu\n", sRepr.c_str(), argCount);
        for (size_t i = 0; i < argCount; ++i) {
             std::string aRepr = env ? env->reprObject(context, positionalParameters->getAt(context, i)) : "???";
             fprintf(stderr, "  - arg[%zu]: %s\n", i, aRepr.c_str());
        }
    }
    
    if (argCount == 1 || argCount == 2) {
        const proto::ProtoObject* obj = (argCount == 2) ? positionalParameters->getAt(context, 1) : positionalParameters->getAt(context, 0);
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_type(1/2) obj=%p\n", (void*)obj);
        }
        
        if (obj == PROTO_NONE) return env->getNoneTypePrototype();
        
        if (get_env_diag()) {
            std::string oRepr = env ? env->reprObject(context, obj) : "???";
            fprintf(stderr, "DEBUG: py_type(obj=%p repr='%s')\n", (void*)obj, oRepr.c_str());
        }
        const proto::ProtoObject* res = env ? env->getType(context, obj) : obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
        return env ? env->getType(context, obj) : obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
    }
    
    if (argCount == 3 || argCount == 4) {
        // type(name, bases, dict) (argCount == 3, self is metaclass)
        // type.__new__(cls, name, bases, dict) (argCount == 4)
        const proto::ProtoObject* cls = (argCount == 4) ? positionalParameters->getAt(context, 0) : self;
        size_t baseIdx = (argCount == 4) ? 1 : 0;
        
        if (argCount == 3) {
            const proto::ProtoString* py_new = PythonEnvironment::getInternedString(context, "__new__");
            const proto::ProtoObject* newMethod = self->getAttribute(context, py_new);
            
            bool isBaseType = (env && self == typeProto);
            if (!isBaseType && newMethod && newMethod != PROTO_NONE) {
                auto m = newMethod->asMethod(context);
                if (m && m != py_type) {
                    const proto::ProtoList* newArgs = context->newList()->appendLast(context, self);
                    for (unsigned long i = 0; i < argCount; ++i) newArgs = newArgs->appendLast(context, positionalParameters->getAt(context, i));
                    return newMethod->call(context, nullptr, py_new, self, newArgs, keywordParameters);
                }
            }
        }

        const proto::ProtoObject* name = positionalParameters->getAt(context, baseIdx + 0);
        const proto::ProtoObject* basesArg = positionalParameters->getAt(context, baseIdx + 1);
        const proto::ProtoObject* dict = positionalParameters->getAt(context, baseIdx + 2);

        const proto::ProtoObject* targetClass = context->newObject(true);
        
        // Add metaclass first so that its attributes are searched after the class MRO bases 
        if (cls && cls != targetClass) {
            targetClass = targetClass->addParent(context, cls);
            targetClass = targetClass->setAttribute(context, env ? env->getClassString() : PythonEnvironment::getInternedString(context, "__class__"), cls);
        } else if (env && env->getTypePrototype()) {
            targetClass = targetClass->addParent(context, env->getTypePrototype());
            targetClass = targetClass->setAttribute(context, env->getClassString(), env->getTypePrototype());
        }
        
        const proto::ProtoString* py_name_s = env ? env->getNameString() : PythonEnvironment::getInternedString(context, "__name__");
        targetClass = targetClass->setAttribute(context, py_name_s, name);
        targetClass = targetClass->setAttribute(context, PythonEnvironment::getInternedString(context, "__is_python_class__"), PROTO_TRUE);

        // 1. Copy dictionary attributes and handle special wrappers
        if (dict) {
            const proto::ProtoString* keysName = env ? env->getKeysString() : PythonEnvironment::getInternedString(context, "__keys__");
            const proto::ProtoObject* keysObj = nullptr;
            bool keysFromImpl = false;
            const proto::ProtoSparseList* dictOwn = dict->proto::ProtoObject::getOwnAttributes(context);
            if (dictOwn) {
                // Pointer-based lookup for native objects that stored __keys__ via initDictStorage
                const proto::ProtoObject* tmp = dictOwn->getAt(context, reinterpret_cast<uintptr_t>(keysName));
                if (tmp && tmp != PROTO_NONE) { keysObj = tmp; keysFromImpl = true; }
            }
            // Fall back to hash-based lookup for Python dicts whose __keys__ was stored via setAttribute
            if (!keysObj) {
                keysObj = dict->getAttribute(context, keysName);
                // Reject the fallback if it resolved via the prototype chain (would be a method, not a list)
                if (keysObj && !keysObj->asList(context)) {
                    keysObj = nullptr;
                }
            }
            const proto::ProtoList* keysList = (keysObj && keysObj->asList(context)) ? keysObj->asList(context) : nullptr;
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG py_type: dict=%p keysObj=%p keysList=%p\n", (void*)dict, (void*)keysObj, (void*)keysList);
            }
            if (keysList) {
                for (size_t i = 0; i < keysList->getSize(context); ++i) {
                    const proto::ProtoObject* keyObj = keysList->getAt(context, i);

                    if (keyObj && keyObj->isString(context)) {
                        const proto::ProtoString* k = keyObj->asString(context);

                        // Skip keys that py_type sets explicitly or that are internal storage
                        // primitives — these must never be overwritten from classdict, even if
                        // a polluted __keys__ list (e.g. inherited from dictPrototype) contains them.
                        {
                            std::string ks; k->toUTF8String(context, ks);
                            if (ks == "__name__" || ks == "__mro__" || ks == "__bases__" ||
                                ks == "__class__" || ks == "__is_python_class__" ||
                                ks == "__keys__" || ks == "__data__") {
                                continue;
                            }
                        }

                        // Use has() + getAt() so that attributes with value None (PROTO_NONE) are
                        // correctly carried through to the class. getAt() alone returns PROTO_NONE
                        // for both "not found" and "found with value None"; has() distinguishes them.
                        const proto::ProtoObject* val = nullptr;
                        bool valFound = false;
                        if (dictOwn && dictOwn->has(context, reinterpret_cast<uintptr_t>(k))) {
                            val = dictOwn->getAt(context, reinterpret_cast<uintptr_t>(k));
                            valFound = true;
                        }
                        // Also check __data__ sparse list (values stored via dict.__setitem__, e.g. from EnumDict).
                        // Use only the classdict's OWN __data__ to avoid inheriting dictPrototype's storage.
                        const proto::ProtoString* dataS = env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__");
                        if (!valFound) {
                            if (dictOwn) {
                                const proto::ProtoObject* dataObj = dictOwn->getAt(context, reinterpret_cast<uintptr_t>(dataS));
                                if (dataObj && dataObj != PROTO_NONE && dataObj->asSparseList(context)) {
                                    const proto::ProtoSparseList* dataSparse = dataObj->asSparseList(context);
                                    unsigned long dataHash = k->getHash(context);
                                    if (dataSparse->has(context, dataHash)) {
                                        val = dataSparse->getAt(context, dataHash);
                                        valFound = true;
                                    }
                                }
                            }
                        }
                        // Implicitly wrap special methods
                        if (val && env && val != PROTO_NONE) {
                            std::string ks; k->toUTF8String(context, ks);
                            const proto::ProtoObject* valType = env->getType(context, val);
                            const proto::ProtoObject* vName = valType ? valType->getAttribute(context, PythonEnvironment::getInternedString(context, "__name__")) : nullptr;
                            std::string tName = "";
                            if (vName && vName->isString(context)) vName->asString(context)->toUTF8String(context, tName);

                            if (k == env->getNewString() && tName != "staticmethod") {
                                const proto::ProtoObject* smCls = env->getBuiltins()->getAttribute(context, PythonEnvironment::getInternedString(context, "staticmethod"));
                                if (smCls && smCls != PROTO_NONE) {
                                    const proto::ProtoList* smArgs = context->newList()->appendLast(context, smCls)->appendLast(context, val);
                                    val = py_staticmethod(context, nullptr, nullptr, smArgs, nullptr);
                                }
                            } else if ((ks == "__init_subclass__" || ks == "__class_getitem__") && tName != "classmethod") {
                                const proto::ProtoObject* cmCls = env->getBuiltins()->getAttribute(context, PythonEnvironment::getInternedString(context, "classmethod"));
                                if (cmCls && cmCls != PROTO_NONE) {
                                    const proto::ProtoList* cmArgs = context->newList()->appendLast(context, cmCls)->appendLast(context, val);
                                    val = py_classmethod(context, nullptr, nullptr, cmArgs, nullptr);
                                }
                            }
                        }
                        
                        if (valFound) {
                    targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, k, val));
                    if (get_env_diag() && (k->toStdString(context) == "_value_repr_" || k->toStdString(context) == "_member_type_")) {
                        bool hasOwn = (targetClass->hasOwnAttribute(context, k) == PROTO_TRUE);
                        fprintf(stderr, "DEBUG py_type: attr %s persistence: %s\n", k->toStdString(context).c_str(), hasOwn ? "OK" : "FAILED");
                    }
                }
              
                        // Update targetClass.__keys__ — use OWN-only lookup to avoid
                        // inheriting typePrototype.__keys__ (which contains type built-in
                        // method names like 'mro', '__init__', etc.) as the initial list.
                        if (keysName) {
                            const proto::ProtoObject* tKeysObj = nullptr;
                            const proto::ProtoSparseList* tcOwnAttrs = targetClass->proto::ProtoObject::getOwnAttributes(context);
                            if (tcOwnAttrs) {
                                const proto::ProtoObject* tmp = tcOwnAttrs->getAt(context, reinterpret_cast<uintptr_t>(keysName));
                                if (tmp && tmp != PROTO_NONE) tKeysObj = tmp;
                            }
                            const proto::ProtoList* tKeysList = tKeysObj ? tKeysObj->asList(context) : nullptr;
                            if (tKeysList) {
                                if (!tKeysList->has(context, keyObj)) {
                                    targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, keysName, tKeysList->appendLast(context, keyObj)->asObject(context)));
                                }
                            } else {
                                const proto::ProtoList* newList = context->newList()->appendLast(context, keyObj);
                                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, keysName, newList->asObject(context)));
                            }
                        }
                    }
                }
            }
        }



        // 2. Bases and MRO Computation
        const proto::ProtoList* mroList = nullptr;
        const proto::ProtoTuple* tupleBases = nullptr;
        const proto::ProtoList* listBases = nullptr;
        
        if (basesArg && basesArg != PROTO_TRUE && basesArg != PROTO_FALSE) {
            tupleBases = basesArg->asTuple(context);
            listBases = tupleBases ? nullptr : basesArg->asList(context);
            
            if (!tupleBases && !listBases) {
                const proto::ProtoString* dataS = env ? env->getDataString() : PythonEnvironment::getInternedString(context, "__data__");
                // ALWAYS use raw lookup for __data__ during bootstrap
                const proto::ProtoObject* dataAttr = basesArg->proto::ProtoObject::getAttribute(context, dataS);
                if (dataAttr && dataAttr != PROTO_NONE) {
                    tupleBases = dataAttr->asTuple(context);
                    listBases = tupleBases ? nullptr : dataAttr->asList(context);
                }
            }
        }
        
        if (get_env_diag()) {
            std::string nStr;
            if (name && name->isString(context)) name->asString(context)->toUTF8String(context, nStr);
            fprintf(stderr, "DEBUG py_type: creating class %s (targetClass=%p)\n", nStr.c_str(), (void*)targetClass);
            fflush(stderr);
        }

        if (tupleBases) {
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG py_type: tupleBases size=%lu\n", tupleBases->getSize(context));
                for (size_t i = 0; i < tupleBases->getSize(context); ++i) {
                    const proto::ProtoObject* b = tupleBases->getAt(context, static_cast<int>(i));
                    std::string bn = "unknown";
                    const proto::ProtoObject* bName = b->proto::ProtoObject::getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
                    if (bName && bName->isString(context)) bName->asString(context)->toUTF8String(context, bn);
                    fprintf(stderr, "  base[%zu]: %s (%p)\n", i, bn.c_str(), (void*)b);
                }
                fflush(stderr);
            }
            mroList = computeC3MRO(context, targetClass, tupleBases->asObject(context));
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, PythonEnvironment::getInternedString(context, "__bases__"), basesArg));
        } else if (listBases) {
            const proto::ProtoObject* convTup = env ? env->newTuple(listBases) : context->newTupleFromList(listBases)->asObject(context);
            mroList = computeC3MRO(context, targetClass, convTup);
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, PythonEnvironment::getInternedString(context, "__bases__"), basesArg));
        } else {
            const proto::ProtoList* defaultBasesList = context->newList();
            const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;
            if (objectProto) defaultBasesList = defaultBasesList->appendLast(context, objectProto);
            const proto::ProtoObject* defaultBases = env ? env->newTuple(defaultBasesList) : context->newTupleFromList(defaultBasesList)->asObject(context);
            
            mroList = computeC3MRO(context, targetClass, defaultBases);
            if (!basesArg || basesArg == PROTO_NONE) {
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, PythonEnvironment::getInternedString(context, "__bases__"), defaultBases));
            }
        }

        if (mroList) {
            const proto::ProtoString* mroName2 = PythonEnvironment::getInternedString(context, "__mro__");
            const proto::ProtoObject* mroTupleVal = env ? env->newTuple(mroList) : context->newTupleFromList(mroList)->asObject(context);
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, mroName2, mroTupleVal));

            // Synchronize native parents
            for (unsigned long i = 1; i < mroList->getSize(context); ++i) {
                const proto::ProtoObject* p = mroList->getAt(context, i);
                if (p && p != targetClass && p != PROTO_NONE) {
                    targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(context, p));
                }
            }
        }

        
        // Set __module__ if not present
        const proto::ProtoString* py_module = PythonEnvironment::getInternedString(context, "__module__");
        if (targetClass->hasOwnAttribute(context, py_module) != PROTO_TRUE) {
            // Prefer __module__ from the dict argument (3rd arg to type()) over current globals,
            // so that classes created from a body dict already containing __module__ (e.g. via
            // enum._convert_) get the correct module name instead of the executing frame's module.
            const proto::ProtoObject* moduleFromDict = dict ? dict->getAttribute(context, py_module) : nullptr;
            if (moduleFromDict && moduleFromDict != PROTO_NONE && moduleFromDict->isString(context)) {
                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, py_module, moduleFromDict));
            } else {
                const proto::ProtoObject* globals = env ? env->getCurrentGlobals() : nullptr;
                const proto::ProtoObject* moduleName = globals ? globals->getAttribute(context, PythonEnvironment::getInternedString(context, "__name__")) : nullptr;
                if (moduleName) {
                    targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(context, py_module, moduleName));
                }
            }
        }
        
        // Final Pass: Call __set_name__ for all attributes that have it.
        const proto::ProtoString* targetKeysName = PythonEnvironment::getInternedString(context, "__keys__");
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
                        const proto::ProtoObject* setName = valType ? valType->getAttribute(context, PythonEnvironment::getInternedString(context, "__set_name__")) : nullptr;
                        if (setName && setName != PROTO_NONE) {
                            const proto::ProtoList* setNameArgs = context->newList()->appendLast(context, val)->appendLast(context, targetClass)->appendLast(context, keyObj);
                            protoPython::invokePythonCallable(context, setName, setNameArgs, nullptr);
                            if (pe && pe->hasPendingException()) { return nullptr; }
                        }
                    }
                }
            }
        }
        
        return targetClass;
    }

    return PROTO_NONE;
}

static const proto::ProtoObject* resolveClassType(protoPython::PythonEnvironment* env, const proto::ProtoObject* self, proto::ProtoContext* context, const proto::ProtoObject* cls) {
    if (!env || !self) return cls;
    const proto::ProtoObject* typeAttr = self->getAttribute(context, PythonEnvironment::getInternedString(context, "type"));
    const proto::ProtoObject* objectAttr = self->getAttribute(context, PythonEnvironment::getInternedString(context, "object"));
    const proto::ProtoObject* listAttr = self->getAttribute(context, PythonEnvironment::getInternedString(context, "list"));
    if (cls->isTuple(context) || cls->asList(context) || cls->isSet(context) || cls->asSparseList(context)) return cls;
    if (cls->isInteger(context) || cls->isString(context) || cls->isFloat(context) || cls->isBoolean(context)) return cls;
    if (cls == PROTO_NONE || cls == PROTO_TRUE || cls == PROTO_FALSE) return cls;

    const proto::ProtoObject* tupAttr = self->getAttribute(context, PythonEnvironment::getInternedString(context, "tuple"));
    const proto::ProtoObject* dictAttr = self->getAttribute(context, PythonEnvironment::getInternedString(context, "dict"));
    const proto::ProtoObject* strAttr = self->getAttribute(context, PythonEnvironment::getInternedString(context, "str"));

    if (cls == typeAttr) return env->getTypePrototype();
    if (cls == objectAttr) return env->getObjectPrototype();

    auto matchesAuthoritative = [&](const proto::ProtoObject* auth) {
        if (!auth) return false;
        if (cls == auth) return true;
        const proto::ProtoString* newS = env->getNewString();
        const proto::ProtoObject* authNew = auth->getAttribute(context, newS);
        // Only fuzzy match if it's a specific native constructor, not the generic object/type constructor
        if (!authNew || authNew == PROTO_NONE) return false;
        const proto::ProtoObject* objNew = env->getObjectPrototype()->getAttribute(context, newS);
        const proto::ProtoObject* typeNew = env->getTypePrototype()->getAttribute(context, newS);
        if (authNew == objNew || authNew == typeNew) return false;
        
        const proto::ProtoObject* clsNew = cls->getAttribute(context, newS);
        return (authNew == clsNew);
    };

    if (matchesAuthoritative(typeAttr)) return env->getTypePrototype();
    if (matchesAuthoritative(listAttr)) return env->getListPrototype();
    if (matchesAuthoritative(tupAttr)) return env->getTuplePrototype();
    if (matchesAuthoritative(dictAttr)) return env->getDictPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "int")))) return env->getIntPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "float")))) return env->getFloatPrototype();
    if (matchesAuthoritative(strAttr)) return env->getStrPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "bytes")))) return env->getBytesPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "bytearray")))) return env->getBytesPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "set")))) return env->getSetPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "frozenset")))) return env->getFrozensetPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "bool")))) return env->getBoolPrototype();
    if (matchesAuthoritative(self->getAttribute(context, PythonEnvironment::getInternedString(context, "complex")))) return env->getComplexPrototype();
    return cls;
}

static bool checkInterfaceInstanceOf(proto::ProtoContext* context, const proto::ProtoObject* obj, const proto::ProtoObject* cls) {
    if (get_env_diag()) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
        std::string objRepr = env ? env->reprObject(context, obj) : "???";
        std::string clsRepr = env ? env->reprObject(context, cls) : "???";
        const proto::ProtoObject* proto = obj->getPrototype(context);
        std::string protoRepr = env ? env->reprObject(context, proto) : "???";
        fprintf(stderr, "DEBUG checkInterfaceInstanceOf obj=%p (%s) proto=%p (%s) cls=%p (%s)\n", (void*)obj, objRepr.c_str(), (void*)proto, protoRepr.c_str(), (void*)cls, clsRepr.c_str());
    }
    if (cls->isTuple(context)) {
        const proto::ProtoTuple* tup = cls->asTuple(context);
        for (size_t i = 0; i < tup->getSize(context); ++i) {
            if (checkInterfaceInstanceOf(context, obj, tup->getAt(context, i))) return true;
        }
        return false;
    }

    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (env) {
        const proto::ProtoObject* oldCls = cls;
        cls = resolveClassType(env, env->getBuiltins(), context, cls);
        if (get_env_diag() && oldCls != cls) {
             fprintf(stderr, "DEBUG checkInterfaceInstanceOf resolved cls=%p to authoritative=%p\n", (void*)oldCls, (void*)cls);
        }
    }

    if (obj == cls) return true;
    
    if (env) {
        if (obj->isString(context) && cls == env->getStrPrototype()) {
            // bytes objects store content in __data__ (a ProtoString) so isString() is true for them,
            // but bytes is NOT str — exclude bytes objects from this check.
            if (env->getBytesPrototype()) {
                const proto::ProtoObject* objClass = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
                if (objClass == env->getBytesPrototype()) return false;
                if (obj->getPrototype(context) == env->getBytesPrototype()) return false;
            }
            return true;
        }
        if (obj->isString(context) && cls == env->getBytesPrototype()) {
            // Bytes objects with __class__ == bytesPrototype are instances of bytes.
            const proto::ProtoObject* objClass = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
            if (objClass == env->getBytesPrototype()) return true;
        }
        if (obj->isInteger(context) && (cls == env->getIntPrototype() || cls == env->getBoolPrototype())) return true;
        if (obj->isFloat(context) && cls == env->getFloatPrototype()) return true;
        if (obj->isBoolean(context) && (cls == env->getBoolPrototype() || cls == env->getIntPrototype())) return true;
        if (obj->asList(context) != nullptr && cls == env->getListPrototype()) return true;
        if (obj->isTuple(context) && cls == env->getTuplePrototype()) return true;
        if (obj->asSparseList(context) && cls == env->getDictPrototype()) return true;
        if (obj->isSet(context) && cls == env->getSetPrototype()) return true;
    }

    if (obj->isInstanceOf(context, cls) == PROTO_TRUE) return true;

    // V88: Fallback for Python-level subclasses where identity mismatch occurred during bootstrap (e.g. IntEnum inheriting from an older intPrototype)
    if (env) {
        const proto::ProtoString* classS = env->getClassString();
        const proto::ProtoObject* objClass = obj->getAttribute(context, classS);
        if (!objClass) objClass = obj->getPrototype(context);
        
        if (objClass) {
            const proto::ProtoObject* mroObj = objClass->getAttribute(context, PythonEnvironment::getInternedString(context, "__mro__"));
            if (mroObj && mroObj->isTuple(context)) {
                const proto::ProtoTuple* mro = mroObj->asTuple(context);
                const proto::ProtoObject* builtins = env->getBuiltins();
                for (size_t i = 0; i < mro->getSize(context); i++) {
                    const proto::ProtoObject* item = mro->getAt(context, i);
                    const proto::ProtoObject* resolvedItem = resolveClassType(env, builtins, context, item);
                    if (item == cls || resolvedItem == cls) return true;
                }
            }
        }
    }

    return false;
}


static bool py_issubclass_check_single(proto::ProtoContext* context, const proto::ProtoObject* cls, const proto::ProtoObject* base, int depth = 0);

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

    // PEP 604: isinstance(obj, X | Y | Z) is True iff any member matches.
    // Detect UnionType by presence of the `__args__` attribute and a
    // `__class__` whose name is "UnionType" — this avoids relying on
    // prototype-identity, which can vary across bootstrap phases.
    if (env && cls) {
        bool isUnion = false;
        // Detect UnionType via presence of __args__; faster and more robust
        // than climbing the prototype chain during bootstrap-sensitive paths.
        const proto::ProtoObject* unionArgs = cls->getAttribute(context,
            PythonEnvironment::getInternedString(context, "__args__"));
        if (unionArgs && unionArgs != PROTO_NONE &&
            (unionArgs->asList(context) || unionArgs->asTuple(context))) {
            // `cls.__class__` via getAttribute can return the typePrototype
            // shim during bootstrap-sensitive paths.  Use env->getType which
            // resolves the same way `type(cls)` does in Python.
            const proto::ProtoObject* clsClass = env->getType(context, cls);
            if (clsClass && clsClass != PROTO_NONE) {
                const proto::ProtoObject* clsName = clsClass->getAttribute(context,
                    env->getNameString());
                if (clsName && clsName->isString(context)) {
                    std::string n;
                    clsName->asString(context)->toUTF8String(context, n);
                    if (n == "UnionType") isUnion = true;
                }
            }
        }
        if (isUnion) {
            const proto::ProtoObject* argsObj = cls->getAttribute(context,
                PythonEnvironment::getInternedString(context, "__args__"));
            // `__args__` may be stored as a list or a tuple depending on
            // how the union was built.
            const proto::ProtoList* argsList = argsObj ? argsObj->asList(context) : nullptr;
            const proto::ProtoTuple* argsTuple = (!argsList && argsObj) ? argsObj->asTuple(context) : nullptr;
            unsigned long n = argsList ? argsList->getSize(context)
                              : (argsTuple ? argsTuple->getSize(context) : 0);
            for (unsigned long i = 0; i < n; ++i) {
                const proto::ProtoObject* member = argsList
                    ? argsList->getAt(context, static_cast<int>(i))
                    : argsTuple->getAt(context, static_cast<int>(i));
                // `int | None` in Python desugars to `int | type(None)`; tolerate
                // a literal None on either the object or the union member.
                if (member == PROTO_NONE && obj == PROTO_NONE) return PROTO_TRUE;
                const proto::ProtoList* one = context->newList()
                    ->appendLast(context, obj)
                    ->appendLast(context, member == PROTO_NONE ? env->getNoneTypePrototype() : member);
                const proto::ProtoObject* r = py_isinstance(context, self, parentLink, one,
                                                            keywordParameters);
                if (r == PROTO_TRUE) return PROTO_TRUE;
            }
            return PROTO_FALSE;
        }
    }

    cls = resolveClassType(env, self, context, cls);
    
    if (get_env_diag()) {
        std::string objRepr = env ? env->reprObject(context, obj) : "???";
        std::string clsRepr = env ? env->reprObject(context, cls) : "???";
        if (clsRepr.find("EnumType") != std::string::npos || clsRepr.find("Enum") != std::string::npos) {
             fprintf(stderr, "DEBUG py_isinstance: obj=%p (%s) cls=%p (%s)\n", (void*)obj, objRepr.c_str(), (void*)cls, clsRepr.c_str());
             fflush(stderr);
        }
    }

    if (obj == PROTO_TRUE || obj == PROTO_FALSE) {
        const proto::ProtoObject* boolType = env ? env->getBoolPrototype() : nullptr;
        const proto::ProtoObject* intType = env ? env->getIntPrototype() : nullptr;
        if (cls == boolType || cls == intType) return PROTO_TRUE;
        if (intType && checkInterfaceInstanceOf(context, intType, cls)) return PROTO_TRUE;
    }

    // Special case for isinstance(x, type): in Python this is True only for class objects.
    // The raw protoCore isInstanceOf traverses native prototype chains that include typePrototype
    // for ALL objects, so we must use the Python-level "is a class?" check instead.
    if (env && cls == env->getTypePrototype()) {
        return env->isActuallyAClass(context, obj) ? PROTO_TRUE : PROTO_FALSE;
    }

    if (checkInterfaceInstanceOf(context, obj, cls)) {
        return PROTO_TRUE;
    }

    // Check __class__ attribute or prototype if native parent link failed
    const proto::ProtoString* classStr = env ? env->getClassString() : PythonEnvironment::getInternedString(context, "__class__");
    const proto::ProtoObject* objClass = obj->getAttribute(context, classStr);
    if (!objClass) objClass = obj->getPrototype(context);
    
    
    if (objClass && objClass != obj) {
        if (py_issubclass_check_single(context, objClass, cls)) return PROTO_TRUE;
    }

    return PROTO_FALSE;
}

static bool py_issubclass_check_single(proto::ProtoContext* context, const proto::ProtoObject* cls, const proto::ProtoObject* base, int depth) {
    if (depth > 100) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseRuntimeError(context, "Maximum recursion depth exceeded in py_issubclass_check_single");
        return false;
    }

    if (cls == base) return true;

    if (depth == 0 && get_env_diag()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        std::string clsRepr = env ? env->reprObject(context, cls) : "???";
        std::string baseRepr = env ? env->reprObject(context, base) : "???";
        fprintf(stderr, "DEBUG py_issubclass: cls=%p (%s) base=%p (%s)\n", (void*)cls, clsRepr.c_str(), (void*)base, baseRepr.c_str());
    }

    // Fast path: use __mro__
    const proto::ProtoObject* mro = cls->getAttribute(context, PythonEnvironment::getInternedString(context, "__mro__"));
    if (depth == 0 && get_env_diag()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        std::string mroRepr = env ? env->reprObject(context, mro) : "???";
        fprintf(stderr, "DEBUG py_issubclass: mro=%p repr=%s list=%p tuple=%p\n", (void*)mro, mroRepr.c_str(), (void*)(mro?mro->asList(context):nullptr), (void*)(mro?mro->asTuple(context):nullptr));
    }
    if (mro && (mro->asList(context) || mro->asTuple(context))) {
        const proto::ProtoList* mroList = mro->asList(context);
        const proto::ProtoTuple* mroTuple = mro->asTuple(context);
        unsigned long size = mroList ? mroList->getSize(context) : (mroTuple ? mroTuple->getSize(context) : 0);
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* builtins = env ? env->getBuiltins() : nullptr;
        for (unsigned long i = 0; i < size; ++i) {
            const proto::ProtoObject* item = mroList ? mroList->getAt(context, i) : mroTuple->getAt(context, i);
            if (item == base) return true;
            if (env && builtins) {
                const proto::ProtoObject* resolvedItem = resolveClassType(env, builtins, context, item);
                if (resolvedItem == base) return true;
            }
        }
        // If __mro__ is present and valid, it contains the entire hierarchy.
        // We do not need to check __bases__ recursively.
    } else {
        // Fallback: check __bases__ recursively (only if __mro__ is absent)
        const proto::ProtoObject* bases = cls->getAttribute(context, PythonEnvironment::getInternedString(context, "__bases__"));
        if (bases) {
            const proto::ProtoList* basesList = bases->asList(context);
            const proto::ProtoTuple* basesTuple = bases->asTuple(context);
            unsigned long size = basesList ? basesList->getSize(context) : (basesTuple ? basesTuple->getSize(context) : 0);
            for (unsigned long i = 0; i < size; ++i) {
                const proto::ProtoObject* item = basesList ? basesList->getAt(context, i) : basesTuple->getAt(context, i);
                if (py_issubclass_check_single(context, item, base, depth + 1)) return true;
            }
        }
    }
    
    // ABC __subclasscheck__ hook: only trigger for non-class objects (e.g., subscripted
    // generics like list[int]) where base is an *instance* whose class defines the hook.
    // For regular class objects, CPython's protocol calls type(base).__subclasscheck__ (the
    // metaclass method), NOT base.__subclasscheck__ (which would find instance methods from
    // the class hierarchy, causing spurious TypeErrors from _GenericAlias etc.).
    PythonEnvironment* env2 = PythonEnvironment::fromContext(context);
    if (!env2 || !env2->isActuallyAClass(context, base)) {
        const proto::ProtoString* py_subclasscheck = PythonEnvironment::getInternedString(context, "__subclasscheck__");
        const proto::ProtoObject* checkMethod = base->getAttribute(context, py_subclasscheck);
        if (checkMethod && checkMethod != PROTO_NONE) {
            const proto::ProtoList* mArgs = context->newList()->appendLast(context, cls);
            const proto::ProtoObject* res = protoPython::invokePythonCallable(context, checkMethod, mArgs, nullptr);
            if (res && res != PROTO_FALSE && res != PROTO_NONE) return true;
        }
    }

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
    
    if (get_env_diag()) {
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
    const proto::ProtoObject* absM = obj->getAttribute(context, PythonEnvironment::getInternedString(context, "__abs__"));
    if (absM && absM->asMethod(context)) {
        return absM->call(context, nullptr, nullptr, obj, context->newList(), nullptr);
    }
    if (obj->isInteger(context)) {
        // Use Integer::abs which handles both SmallInteger and LargeInteger
        // without triggering long-long overflow.
        return obj->abs(context);
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
        const proto::ProtoString* keyS = PythonEnvironment::getInternedString(context, "key");
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

        // Compare via PythonEnvironment::compareObjects so strings,
        // tuples, etc. get proper ordering (op=2 is '<', op=4 is '>').
        bool better = false;
        ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
        if (env) {
            int op = isMax ? 4 : 2;
            const proto::ProtoObject* cmp = env->compareObjects(context, currentVal, bestVal, op);
            better = (cmp == PROTO_TRUE);
        } else if (currentVal->isInteger(context) && bestVal->isInteger(context)) {
            better = isMax
                ? (currentVal->asLong(context) > bestVal->asLong(context))
                : (currentVal->asLong(context) < bestVal->asLong(context));
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
    const proto::ProtoObject* baseObj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* expObj = positionalParameters->getAt(context, 1);
    bool hasMod = n >= 3;
    const proto::ProtoObject* modObj = hasMod ? positionalParameters->getAt(context, 2) : nullptr;

    // If any operand is non-integer, try __pow__ dunder on the base
    if (!baseObj->isInteger(context) || !expObj->isInteger(context) ||
        (modObj && !modObj->isInteger(context))) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* powMethod = baseObj->getAttribute(context, PythonEnvironment::getInternedString(context, "__pow__"));
        if (powMethod) {
            proto::ProtoList* args = const_cast<proto::ProtoList*>(context->newList()->appendLast(context, expObj));
            if (modObj) args = const_cast<proto::ProtoList*>(args->appendLast(context, modObj));
            if (powMethod->asMethod(context))
                return powMethod->asMethod(context)(context, baseObj, nullptr, args, nullptr);
            if (env) return ::protoPython::invokePythonCallable(context, powMethod, args, nullptr);
        }
        if (env) env->raiseTypeError(context, "unsupported operand type(s) for pow()");
        return PROTO_NONE;
    }

    // Bignum-safe: use Integer::multiply / modulo so the result can grow
    // beyond 64 bits.  Exponent must still fit a positive int64 (anything
    // larger is astronomically infeasible).
    long long exp = 0;
    try { exp = expObj->asLong(context); }
    catch (const std::overflow_error&) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context,
            PythonEnvironment::getInternedString(context, "exponent too large for pow()")->asObject(context));
        return PROTO_NONE;
    }
    if (exp < 0) {
        // Negative exponent without mod: return float; with mod: error
        if (hasMod) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseValueError(context,
                PythonEnvironment::getInternedString(context,
                    "pow() 2nd argument cannot be negative when 3rd argument specified")->asObject(context));
            return PROTO_NONE;
        }
        // pow(base, -e) = 1.0 / pow(base, e) — promote to float
        long long pe = -exp;
        const proto::ProtoObject* res = context->fromInteger(1);
        const proto::ProtoObject* b = baseObj;
        while (pe > 0) {
            if (pe & 1) res = res->multiply(context, b);
            pe >>= 1;
            if (pe > 0) b = b->multiply(context, b);
        }
        // Convert res to double via decimal string and divide.
        const proto::ProtoString* s = res->asIntegerString(context, 10);
        std::string digits;
        s->toUTF8String(context, digits);
        try { return context->fromDouble(1.0 / std::stod(digits)); }
        catch (...) { return context->fromDouble(0.0); }
    }
    if (hasMod) {
        if (modObj->integerSign(context) == 0) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseValueError(context,
                PythonEnvironment::getInternedString(context, "pow() 3rd argument cannot be 0")->asObject(context));
            return PROTO_NONE;
        }
        // Bignum-safe modular exponentiation.
        const proto::ProtoObject* result = context->fromInteger(1);
        const proto::ProtoObject* b = baseObj->modulo(context, modObj);
        long long e = exp;
        while (e > 0) {
            if (e & 1) {
                result = result->multiply(context, b);
                result = result->modulo(context, modObj);
            }
            e >>= 1;
            if (e > 0) {
                b = b->multiply(context, b);
                b = b->modulo(context, modObj);
            }
        }
        return result;
    }
    // Plain exponentiation by squaring with bignum accumulation.
    const proto::ProtoObject* result = context->fromInteger(1);
    const proto::ProtoObject* b = baseObj;
    long long e = exp;
    while (e > 0) {
        if (e & 1) result = result->multiply(context, b);
        e >>= 1;
        if (e > 0) b = b->multiply(context, b);
    }
    return result;
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
    int offset = 0;
    if (!self && positionalParameters && positionalParameters->getSize(context) >= 2) {
        // unbound call: args are positional[0] and positional[1]
    } else if (self && positionalParameters && positionalParameters->getSize(context) >= 1) {
        // bound call via __divmod__: self is first, positional[0] is second
        offset = -1; // handled below
    }
    if (!positionalParameters || positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* objA = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* objB = positionalParameters->getAt(context, 1);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    bool aIsFloat = objA->isDouble(context) || (objA->isInteger(context) && objB->isDouble(context));
    bool bIsFloat = objB->isDouble(context);
    if (aIsFloat || bIsFloat) {
        double a = objA->isDouble(context) ? objA->asDouble(context) : (double)objA->asLong(context);
        double b = objB->isDouble(context) ? objB->asDouble(context) : (double)objB->asLong(context);
        if (b == 0.0) {
            if (env) env->raiseZeroDivisionError(context);
            return PROTO_NONE;
        }
        double quot = std::floor(a / b);
        double rem = a - quot * b;
        const proto::ProtoList* pair = context->newList()
            ->appendLast(context, context->fromDouble(quot))
            ->appendLast(context, context->fromDouble(rem));
        return env ? env->newTuple(pair) : context->newTupleFromList(pair)->asObject(context);
    }
    if (!objA->isInteger(context) || !objB->isInteger(context)) {
        if (env) env->raiseTypeError(context, "divmod() argument must be a number");
        return PROTO_NONE;
    }
    if (objB->integerSign(context) == 0) {
        if (env) env->raiseZeroDivisionError(context);
        return PROTO_NONE;
    }
    // Python divmod uses floor-toward-minus-infinity division.  Integer::
    // divide/modulo follow the C convention (truncate toward zero), so we
    // adjust if the remainder has a different sign than the divisor.
    const proto::ProtoObject* quot = objA->divide(context, objB);
    const proto::ProtoObject* rem  = objA->modulo(context, objB);
    int signRem = rem->integerSign(context);
    int signB   = objB->integerSign(context);
    if (signRem != 0 && ((signRem > 0) != (signB > 0))) {
        // remainder has wrong sign — adjust toward floor.
        const proto::ProtoObject* one = context->fromInteger(1);
        quot = quot->subtract(context, one);
        rem  = rem->add(context, objB);
    }
    const proto::ProtoList* pair = context->newList()
        ->appendLast(context, quot)
        ->appendLast(context, rem);
    return env ? env->newTuple(pair) : context->newTupleFromList(pair)->asObject(context);
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
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, env ? env->getReprString() : PythonEnvironment::getInternedString(context, "__repr__"));
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
    return PythonEnvironment::getInternedString(context, out.c_str())->asObject(context);
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
    const proto::ProtoObject* fget = self->getAttribute(context, PythonEnvironment::getInternedString(context, "fget"));
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG py_property_get: fget=%p\n", (void*)fget);
    }
    if (fget && fget != PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::vector<const proto::ProtoObject*> argsVec = {obj};
            const proto::ProtoObject* res = env->callObject(fget, argsVec);
            if (get_env_diag()) {
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
    const proto::ProtoObject* fset = self->getAttribute(context, PythonEnvironment::getInternedString(context, "fset"));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (fset && fset != PROTO_NONE) {
        if (env) {
            std::vector<const proto::ProtoObject*> argsVec = {obj, val};
            env->callObject(fset, argsVec);
        }
        return PROTO_NONE;
    }
    // PI: Read-only property — raise AttributeError per CPython semantics.
    // CPython 3.13+ emits messages of the form
    //   property '<name>' of '<TypeName>' object has no setter
    // and stores the property name (not the human-readable sentence!) in the
    // AttributeError's `name` slot.  Earlier protopy code passed the sentence
    // fragment "property has no setter" as the `attr` argument to
    // raiseAttributeError, which then emitted the malformed shape
    //   '<TypeName>' object has no attribute 'property has no setter'
    // and copied that fragment into the `name` slot — surfacing as cluster B4
    // in the SP-B audit (test_sys / socket cluster-2).
    if (env) {
        // Resolve the property name.  protopy stores it on the underlying
        // fget callable; it may also have been recorded directly on the
        // property as `__name__` by descriptor-aware metaclasses.
        std::string propName;
        const proto::ProtoString* nameKey = PythonEnvironment::getInternedString(context, "__name__");
        const proto::ProtoObject* nameAttr = self->getAttribute(context, nameKey);
        bool gotName = false;
        if (nameAttr && nameAttr != PROTO_NONE && nameAttr->isString(context)) {
            nameAttr->asString(context)->toUTF8String(context, propName);
            // The property type's own __name__ is the literal "property"; that's
            // not the per-instance property identifier — fall through to fget.__name__.
            if (propName != "property") {
                gotName = true;
            }
        }
        if (!gotName) {
            // The property type itself returns 'property' for __name__; fall
            // back to fget.__name__ which is the most reliable carrier of the
            // attribute identifier in protopy.
            propName.clear();
            const proto::ProtoObject* fget = self->getAttribute(context, PythonEnvironment::getInternedString(context, "fget"));
            if (fget && fget != PROTO_NONE) {
                const proto::ProtoObject* fgetName = fget->getAttribute(context, nameKey);
                if (fgetName && fgetName != PROTO_NONE && fgetName->isString(context)) {
                    fgetName->asString(context)->toUTF8String(context, propName);
                }
            }
        }

        // Resolve the class name of `obj` for the message body.
        std::string typeName = "object";
        const proto::ProtoObject* cls = env->getType(context, obj);
        if (cls) {
            const proto::ProtoObject* clsName = cls->getAttribute(context, nameKey);
            if (clsName && clsName != PROTO_NONE && clsName->isString(context)) {
                clsName->asString(context)->toUTF8String(context, typeName);
            }
        }

        std::string message;
        if (!propName.empty()) {
            message = "property '" + propName + "' of '" + typeName + "' object has no setter";
        } else {
            message = "property of '" + typeName + "' object has no setter";
        }
        env->raiseAttributeErrorWithMessage(context, obj, message, propName);
    }
    return nullptr;
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

// Helper: create a copy of a property with one slot replaced.
static const proto::ProtoObject* py_property_copy_with(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const char* slotName,
    const proto::ProtoObject* newFn) {
    proto::ProtoObject* prop = const_cast<proto::ProtoObject*>(self->newChild(context, true));
    const proto::ProtoString* fgetKey = PythonEnvironment::getInternedString(context, "fget");
    const proto::ProtoString* fsetKey = PythonEnvironment::getInternedString(context, "fset");
    const proto::ProtoString* fdelKey = PythonEnvironment::getInternedString(context, "fdel");
    const proto::ProtoString* classKey = PythonEnvironment::getInternedString(context, "__class__");
    const proto::ProtoObject* fget = self->getAttribute(context, fgetKey);
    const proto::ProtoObject* fset = self->getAttribute(context, fsetKey);
    const proto::ProtoObject* fdel = self->getAttribute(context, fdelKey);
    const proto::ProtoObject* cls  = self->getAttribute(context, classKey);
    if (cls  && cls  != PROTO_NONE) prop->setAttribute(context, classKey, cls);
    if (fget && fget != PROTO_NONE) prop->setAttribute(context, fgetKey, fget);
    if (fset && fset != PROTO_NONE) prop->setAttribute(context, fsetKey, fset);
    if (fdel && fdel != PROTO_NONE) prop->setAttribute(context, fdelKey, fdel);
    // Override the named slot
    prop->setAttribute(context, PythonEnvironment::getInternedString(context, slotName), newFn);
    return prop;
}

// property.getter(fget) -> new property with fget replaced
static const proto::ProtoObject* py_property_getter_method(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* newFget = positionalParameters->getAt(context, 0);
    return py_property_copy_with(context, self, "fget", newFget);
}

// property.setter(fset) -> new property with fset replaced
static const proto::ProtoObject* py_property_setter_method(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* newFset = positionalParameters->getAt(context, 0);
    return py_property_copy_with(context, self, "fset", newFset);
}

// property.deleter(fdel) -> new property with fdel replaced
static const proto::ProtoObject* py_property_deleter_method(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* newFdel = positionalParameters->getAt(context, 0);
    return py_property_copy_with(context, self, "fdel", newFdel);
}

static const proto::ProtoObject* py_property(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0); // property class

    // Create new instance of cls and persist every setAttribute return — even
    // with newChild(ctx, true) for mutable creation, protoCore's attribute
    // structures may hand back a new wrapper for some shapes; without the
    // re-bind, fields silently regress to their pre-write state.  The
    // observed failure was `type(p)` reporting `object` instead of
    // `property`, breaking the descriptor protocol on namedtuple field
    // accessors (their _tuplegetter is property(_itemgetter(idx))) and
    // surfacing as `'object' object has no attribute 'split'` whenever a
    // namedtuple field carrying a string value (e.g. uname_result.release)
    // was accessed via `instance.field` instead of `instance[idx]`.
    const proto::ProtoObject* prop = cls->newChild(context, true);

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        prop = prop->setAttribute(context, env->getClassString(), cls);
    } else {
        prop = prop->setAttribute(context, PythonEnvironment::getInternedString(context, "__class__"), cls);
    }

    if (positionalParameters->getSize(context) >= 2) {
        prop = prop->setAttribute(context, PythonEnvironment::getInternedString(context, "fget"), positionalParameters->getAt(context, 1));
    }
    if (positionalParameters->getSize(context) >= 3) {
        prop = prop->setAttribute(context, PythonEnvironment::getInternedString(context, "fset"), positionalParameters->getAt(context, 2));
    }
    if (positionalParameters->getSize(context) >= 4) {
        prop = prop->setAttribute(context, PythonEnvironment::getInternedString(context, "fdel"), positionalParameters->getAt(context, 3));
    }

    if (get_env_diag()) {
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

    const proto::ProtoObject* func = self->getAttribute(context, PythonEnvironment::getInternedString(context, "func"));
    if (!func || func == PROTO_NONE) return PROTO_NONE;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* bound = context->newObject(false);
    if (env && env->getMethodPrototype()) {
        bound = const_cast<proto::ProtoObject*>(bound->addParent(context, env->getMethodPrototype()));
        bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, env->getClassString(), env->getMethodPrototype()));
    }
    
    // Set __self__ (the instance, which is the class for classmethods)
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, PythonEnvironment::getInternedString(context, "__self__"), type));
    
    // Set __func__ (the original function)
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, PythonEnvironment::getInternedString(context, "__func__"), func));
    
    // Set __call__ to delegate to runBoundMethodCall
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(context, env ? env->getCallString() : PythonEnvironment::getInternedString(context, "__call__"),
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
        cm->setAttribute(context, PythonEnvironment::getInternedString(context, "__class__"), cls);
    }
    
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* func = positionalParameters->getAt(context, 1);
        cm->setAttribute(context, PythonEnvironment::getInternedString(context, "func"), func);
        cm->setAttribute(context, PythonEnvironment::getInternedString(context, "__func__"), func);
    }
    return cm;
}

static const proto::ProtoObject* py_staticmethod_get(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return self->getAttribute(context, PythonEnvironment::getInternedString(context, "func"));
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
        sm->setAttribute(context, PythonEnvironment::getInternedString(context, "__class__"), cls);
    }
    
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* func = positionalParameters->getAt(context, 1);
        sm->setAttribute(context, PythonEnvironment::getInternedString(context, "func"), func);
        sm->setAttribute(context, PythonEnvironment::getInternedString(context, "__func__"), func);
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
    return PythonEnvironment::getInternedString(context, buf)->asObject(context);
}

// Format an integer (small or big) as `prefix + digit-string` in the
// given base.  Integer::toString already handles bignum and sign; all we
// do is splice the prefix in the right place.
static std::string format_int_with_prefix(
    proto::ProtoContext* context,
    const proto::ProtoObject* arg,
    const char* prefix,
    int base) {
    const proto::ProtoString* s = arg->asIntegerString(context, base);
    std::string digits;
    s->toUTF8String(context, digits);
    // Integer::toString returns digits with leading '-' for negatives.
    if (!digits.empty() && digits[0] == '-') {
        return "-" + std::string(prefix) + digits.substr(1);
    }
    return std::string(prefix) + digits;
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
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    std::string out = format_int_with_prefix(context, arg, "0b", 2);
    return PythonEnvironment::getInternedString(context, out.c_str())->asObject(context);
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
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    std::string out = format_int_with_prefix(context, arg, "0o", 8);
    return PythonEnvironment::getInternedString(context, out.c_str())->asObject(context);
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
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    std::string out = format_int_with_prefix(context, arg, "0x", 16);
    return PythonEnvironment::getInternedString(context, out.c_str())->asObject(context);
}

static const proto::ProtoObject* py_round(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* n = positionalParameters->getAt(context, 0);

    // Determine whether ndigits was provided (and is not None).
    bool hasNdigits = (positionalParameters->getSize(context) >= 2 &&
                       positionalParameters->getAt(context, 1) != PROTO_NONE);
    int ndigits = 0;
    if (hasNdigits) {
        ndigits = static_cast<int>(positionalParameters->getAt(context, 1)->asLong(context));
    }

    if (n->isInteger(context)) {
        // round(int) and round(int, ndigits) always return int.
        if (!hasNdigits || ndigits >= 0) return n; // return as-is
        // Negative ndigits: round to nearest 10^|ndigits|.  Use bignum
        // arithmetic so values larger than 64 bits work.
        const proto::ProtoObject* power = context->fromInteger(1);
        const proto::ProtoObject* ten = context->fromInteger(10);
        for (int i = 0; i < -ndigits; ++i) power = power->multiply(context, ten);
        const proto::ProtoObject* two = context->fromInteger(2);
        const proto::ProtoObject* half = power->divide(context, two);
        const proto::ProtoObject* rem = n->modulo(context, power);
        if (rem->integerSign(context) < 0) rem = rem->add(context, power);
        int cmpHalf = rem->compare(context, half);
        const proto::ProtoObject* base = n->subtract(context, rem);
        if (cmpHalf < 0) return base;
        if (cmpHalf > 0) return base->add(context, power);
        // banker's rounding: round half to even
        const proto::ProtoObject* quotient = base->divide(context, power);
        const proto::ProtoObject* parity = quotient->modulo(context, two);
        if (parity->integerSign(context) == 0) return base;
        return base->add(context, power);
    }

    double d = n->asDouble(context);
    if (hasNdigits) {
        // round(float, ndigits) returns float.
        if (ndigits > 0) {
            double power = std::pow(10.0, ndigits);
            d = std::round(d * power) / power;
        } else if (ndigits < 0) {
            double power = std::pow(10.0, -ndigits);
            d = std::round(d / power) * power;
        } else {
            d = std::round(d);
        }
        return context->fromDouble(d);
    } else {
        // round(float) with no ndigits returns int.
        double rounded = std::round(d);
        // Use banker's rounding (round half to even).
        double lower = std::floor(d);
        double diff = d - lower;
        if (diff == 0.5) {
            // tie-breaking: round to even
            rounded = (std::fmod(lower, 2.0) == 0.0) ? lower : lower + 1.0;
        } else if (diff == -0.5) {
            rounded = (std::fmod(lower, 2.0) == 0.0) ? lower : lower - 1.0;
        }
        return context->fromInteger(static_cast<int>(static_cast<long long>(rounded)));
    }
}

static const proto::ProtoObject* py_range_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {

    // Fast-path: native ProtoRangeIteratorImplementation (what py_range_iter now returns).
    // Direct C++ field comparison and increment — zero attribute lookups, zero allocations.
    // nullptr return signals exhaustion; OP_FOR_ITER treats null+no-exception as loop end.
    if (self->isNativeRangeIterator(context)) {
        return self->nextInNativeRange(context);
    }

    // Legacy path: Python-object-style iterators with __range_cur__/__range_stop__/__range_step__.
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* curS = env ? env->getRangeCurString() : PythonEnvironment::getInternedString(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : PythonEnvironment::getInternedString(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : PythonEnvironment::getInternedString(context, "__range_step__");

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
    const proto::ProtoString* curS = env ? env->getRangeCurString() : PythonEnvironment::getInternedString(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : PythonEnvironment::getInternedString(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : PythonEnvironment::getInternedString(context, "__range_step__");

    const proto::ProtoObject* curObj = self->getAttribute(context, curS);
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_range_iter called with self=%p, curObj=%p\n", (void*)self, (void*)curObj);
        fflush(stderr);
    }
    if (!curObj) return PROTO_NONE;
    long long start = curObj->asLong(context);
    long long stop = self->getAttribute(context, stopS)->asLong(context);
    long long step = self->getAttribute(context, stepS)->asLong(context);

    // Return a native range iterator: direct C++ fields, zero attribute
    // lookups per iteration, zero allocations per step (small integers are tagged pointers).
    return context->newRangeIterator(start, stop, step);
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
    const proto::ProtoString* curS = env ? env->getRangeCurString() : PythonEnvironment::getInternedString(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : PythonEnvironment::getInternedString(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : PythonEnvironment::getInternedString(context, "__range_step__");

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
    const proto::ProtoString* iterS = env ? env->getIterString() : PythonEnvironment::getInternedString(context, "__iter__");
    const proto::ProtoString* zipProtoS = env ? env->getZipProtoString() : PythonEnvironment::getInternedString(context, "__zip_proto__");
    const proto::ProtoString* zipItersS = env ? env->getZipItersString() : PythonEnvironment::getInternedString(context, "__zip_iters__");
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
    const proto::ProtoString* itersS = env ? env->getZipItersString() : PythonEnvironment::getInternedString(context, "__zip_iters__");
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");

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
    return env ? env->newTuple(resList) : context->newTupleFromList(resList)->asObject(context);
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
    const proto::ProtoString* callS = env ? env->getCallString() : PythonEnvironment::getInternedString(context, "__call__");
    const proto::ProtoString* iterS = env ? env->getIterString() : PythonEnvironment::getInternedString(context, "__iter__");
    const proto::ProtoString* filterProtoS = env ? env->getFilterProtoString() : PythonEnvironment::getInternedString(context, "__filter_proto__");
    const proto::ProtoString* boolTypeS = env ? env->getBoolTypeNameString() : PythonEnvironment::getInternedString(context, "bool");
    const proto::ProtoString* filterFuncS = env ? env->getFilterFuncString() : PythonEnvironment::getInternedString(context, "__filter_func__");
    const proto::ProtoString* filterIterS = env ? env->getFilterIterString() : PythonEnvironment::getInternedString(context, "__filter_iter__");
    const proto::ProtoString* filterBoolS = env ? env->getFilterBoolString() : PythonEnvironment::getInternedString(context, "__filter_bool__");
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
    const proto::ProtoString* funcS = env ? env->getFilterFuncString() : PythonEnvironment::getInternedString(context, "__filter_func__");
    const proto::ProtoString* iterS = env ? env->getFilterIterString() : PythonEnvironment::getInternedString(context, "__filter_iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");
    const proto::ProtoString* callS = env ? env->getCallString() : PythonEnvironment::getInternedString(context, "__call__");

    const proto::ProtoObject* func = self->getAttribute(context, funcS);
    const proto::ProtoObject* it = self->getAttribute(context, iterS);
    if (!func || !it) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_map_next failing: func=%p it=%p\n", (void*)func, (void*)it);
            fflush(stderr);
        }
        return nullptr;
    }
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!nextM || !nextM->asMethod(context)) {
        if (get_env_diag()) {
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
        if (result && env && env->isTrue(result)) return val;
    }
}

const proto::ProtoObject* py_python_ignore_init(
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
    const proto::ProtoString* mapProtoS = env ? env->getMapProtoString() : PythonEnvironment::getInternedString(context, "__map_proto__");
    const proto::ProtoString* mapFuncS = env ? env->getMapFuncString() : PythonEnvironment::getInternedString(context, "__map_func__");
    const proto::ProtoString* mapIterS = env ? env->getMapIterString() : PythonEnvironment::getInternedString(context, "__map_iter__");
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    const proto::ProtoObject* it = py_iter(context, nullptr, nullptr, context->newList()->appendLast(context, iterable), nullptr);
    if (!it || it == noneObj) {
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_map failing: py_iter returned None or nullptr\n");
        return PROTO_NONE;
    }
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* mapObj = cls->newChild(context, true);
    mapObj = mapObj->setAttribute(context, mapFuncS, func);
    mapObj = mapObj->setAttribute(context, mapIterS, it);
    if (get_env_diag()) {
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
    const proto::ProtoString* funcS = env ? env->getMapFuncString() : PythonEnvironment::getInternedString(context, "__map_func__");
    const proto::ProtoString* iterS = env ? env->getMapIterString() : PythonEnvironment::getInternedString(context, "__map_iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : PythonEnvironment::getInternedString(context, "__next__");

    const proto::ProtoObject* func = self->getAttribute(context, funcS);
    const proto::ProtoObject* it = self->getAttribute(context, iterS);
    if (!func || !it) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_map_next failing: func=%p it=%p\n", (void*)func, (void*)it);
            fflush(stderr);
        }
        return nullptr;
    }
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!nextM || !nextM->asMethod(context)) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: py_map_next failing: it=%p nextM=%p\n", (void*)it, (void*)nextM);
            fflush(stderr);
        }
        return nullptr;
    }
    
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
    if (!val) {
        if (get_env_diag()) {
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
    
    // Create new instance of cls natively.  newChild attaches `cls` as
    // the protoCore parent — getType() / env->getAttribute("__class__")
    // synthesise the class identity from that link, so we no longer
    // mirror it as an explicit __class__ attribute on the instance.
    const proto::ProtoObject* obj = cls->newChild(context, true);

    // Initialize properties tracking specifically dictionary
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    if (env) {
        obj = env->initDictStorage(context, obj);
    }
    
    return obj;
}

const proto::ProtoObject* py_bytearray_fallback(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_bytearray_fallback called\n");
    // If called with a bytes/string argument, return it directly (protoPython simplification:
    // bytearray is treated as immutable bytes since mutation is not widely used in stdlib).
    if (args && args->getSize(ctx) >= 1) {
        const proto::ProtoObject* initArg = args->getAt(ctx, 0);
        if (initArg && initArg != PROTO_NONE && initArg->isString(ctx)) {
            return initArg;
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env && env->getBytesPrototype()) {
        proto::ProtoObject* b = const_cast<proto::ProtoObject*>(env->getBytesPrototype()->newChild(ctx, true));
        b->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"), PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
        return b;
    }
    return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
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
                                   const proto::ProtoObject* ioModule,
                                   const proto::ProtoObject* exceptionsModule) {
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG BUILTINS INIT START: ctx=%p objectProto=%p\n", (void*)ctx, (void*)objectProto);
        fflush(stderr);
    }
    protoPython::PythonEnvironment* pEnv = protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* builtins = ctx->newObject(false);
    if (objectProto) builtins = builtins->addParent(ctx, objectProto);
    if (noneProto) {
        builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "None"), noneProto);
    }
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG IN BUILTINS INIT: 1 obInB=%p\n", (void*)builtins->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "object")));
    }
    if (ioModule && ioModule != PROTO_NONE) {
        builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__io_module__"), ioModule);
    }
    
    // Initialize dummy gc module
    const proto::ProtoObject* gcModule = ctx->newObject(false);
    if (objectProto) gcModule = gcModule->addParent(ctx, objectProto);
    gcModule = gcModule->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "gc")->asObject(ctx));
    gcModule = gcModule->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "collect"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_collect));
    gcModule = gcModule->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isenabled"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_isenabled));
    gcModule = gcModule->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "disable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_isenabled));
    gcModule = gcModule->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "enable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(gcModule), py_gc_isenabled));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "gc"), gcModule);

    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "True"), PROTO_TRUE);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "False"), PROTO_FALSE);
    
    // Initialize Ellipsis
    const proto::ProtoObject* ellipsis = ellipsisProto;
    if (!ellipsis) {
        ellipsis = ctx->newObject(false);
        if (objectProto) ellipsis = ellipsis->addParent(ctx, objectProto);
        ellipsis = ellipsis->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repr__"), PythonEnvironment::getInternedString(ctx, "Ellipsis")->asObject(ctx));
    }
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "Ellipsis"), ellipsis);

    // Initialize NotImplemented
    const proto::ProtoObject* notImpl = notImplementedProto;
    if (!notImpl) {
        notImpl = ctx->newObject(false);
        if (objectProto) notImpl = notImpl->addParent(ctx, objectProto);
        notImpl = notImpl->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repr__"), PythonEnvironment::getInternedString(ctx, "NotImplemented")->asObject(ctx));
    }
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "NotImplemented"), notImpl);

    // __debug__: True unless running with -O (optimization). Always True for protoPython.
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__debug__"), ctx->fromInteger(1));

    if (objectProto) {
        const proto::ProtoString* s_setattr = PythonEnvironment::getInternedString(ctx, "__setattr__");
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, s_setattr, ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_setattr));
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__getattribute__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_getattribute));
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, env ? env->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_init));
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__eq__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_eq));
        objectProto = const_cast<proto::ProtoObject*>(objectProto)->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__ne__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(objectProto), py_object_ne));

        if (env) env->setObjectPrototype(objectProto);
        
        // Also update the space's objectPrototype!
        ctx->space->objectPrototype = const_cast<proto::ProtoObject*>(objectProto);
    }
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "object"), objectProto);
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG IN BUILTINS INIT: 2 (AFTER SET) obInB=%p\n", (void*)builtins->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "object")));
    }
    if (typeProto) {
        // Register mro and __new__ on typePrototype natively
        typeProto = typeProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "mro"), ctx->fromMethod(const_cast<proto::ProtoObject*>(typeProto), py_type_mro));
        pEnv = PythonEnvironment::fromContext(ctx);
        if (pEnv) {
            typeProto = typeProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_type));
            if (get_env_diag()) {
                const proto::ProtoObject* tNew = typeProto->getAttribute(ctx, pEnv->getNewString());
                fprintf(stderr, "DEBUG BUILTINS INIT: typeProto=%p type.__new__=%p newStrPtr=%p\n", (void*)typeProto, (void*)tNew, (void*)pEnv->getNewString());
            }
            // Update the PythonEnvironment's typePrototype!
            pEnv->setTypePrototype(typeProto);
        }
        builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "type"), typeProto);
    } else {
        if (get_env_diag()) {
        }
        builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "type"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_type));
    }
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "int"), intProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "str"), strProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "list"), listProto);
    if (get_env_diag()) {
        const proto::ProtoObject* dNew = dictProto ? dictProto->getAttribute(ctx, pEnv->getNewString()) : nullptr;
        fprintf(stderr, "DEBUG IN BUILTINS INIT: dictProto=%p dict.__new__=%p\n", (void*)dictProto, (void*)dNew);
    }
    // Update the PythonEnvironment's dictPrototype!
    if (dictProto) {
        pEnv = PythonEnvironment::fromContext(ctx);
        if (pEnv) pEnv->setDictPrototype(dictProto);
    }
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "dict"), dictProto);
    if (get_env_diag()) {
        const proto::ProtoObject* dInB = builtins->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "dict"));
        const proto::ProtoObject* dNew = dInB ? dInB->getAttribute(ctx, pEnv->getNewString()) : nullptr;
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
    const proto::ProtoString* py_class_local = PythonEnvironment::fromContext(ctx) ? PythonEnvironment::fromContext(ctx)->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__");
    const proto::ProtoString* py_name_local = PythonEnvironment::fromContext(ctx) ? PythonEnvironment::fromContext(ctx)->getNameString() : PythonEnvironment::getInternedString(ctx, "__name__");
    if (typeProto) bytearrayClass = bytearrayClass->setAttribute(ctx, py_class_local, typeProto);
    bytearrayClass = bytearrayClass->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "bytearray")->asObject(ctx));
    bytearrayClass = bytearrayClass->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_bytearray_new));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bytearray"), bytearrayClass);

    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "tuple"), tupleProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "set"), setProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bytes"), bytesProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "slice"), sliceType);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "frozenset"), frozensetProto);
    if (floatProto) builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "float"), floatProto);
    if (boolProto) builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bool"), boolProto);
    if (complexProto) builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "complex"), complexProto);
    
    // Add functions
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "len"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_len));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "repr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_repr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "format"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_format));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "builtins")->asObject(ctx));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__package__"), PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_open));
    // print registration diagnostic removed
    if (get_env_diag()) {
    }
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "print"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_print));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "dir"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_dir));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "id"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_id));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_object_getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_getattribute));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_object_setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_setattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "iter"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_iter));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "next"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_next));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "contains"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "in"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isinstance"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_isinstance));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "issubclass"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_issubclass));
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG BUILTINS INIT: before pEnv lookup\n");
        fflush(stderr);
    }
    pEnv = PythonEnvironment::fromContext(ctx);
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG BUILTINS INIT: pEnv=%p\n", (void*)pEnv);
        fflush(stderr);
    }
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
    rangeClass = rangeClass->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "range")->asObject(ctx));
    rangeClass = rangeClass->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_range_new));
    rangeClass = rangeClass->setAttribute(ctx, pEnv ? pEnv->getIterString() : PythonEnvironment::getInternedString(ctx, "__iter__"), ctx->fromMethod(nullptr, py_range_iter));
    rangeClass = rangeClass->setAttribute(ctx, pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(nullptr, py_python_ignore_init));
    builtins = builtins->setAttribute(ctx, pEnv ? pEnv->getRangeString() : PythonEnvironment::getInternedString(ctx, "range"), rangeClass);

    const proto::ProtoObject* zipProto = ctx->newObject(false);
    if (objectProto) zipProto = zipProto->addParent(ctx, objectProto);
    if (typeProto) zipProto = zipProto->setAttribute(ctx, py_class_local, typeProto);
    zipProto = zipProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "zip")->asObject(ctx));
    zipProto = zipProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_zip));
    zipProto = zipProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(nullptr, py_python_ignore_init));
    zipProto = zipProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(zipProto), py_self_iter));
    zipProto = zipProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(zipProto), py_zip_next));
    builtins = builtins->setAttribute(ctx, pEnv->getZipProtoString(), zipProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "zip"), zipProto);
    const proto::ProtoObject* filterProto = ctx->newObject(false);
    if (objectProto) filterProto = filterProto->addParent(ctx, objectProto);
    if (typeProto) filterProto = filterProto->setAttribute(ctx, py_class_local, typeProto);
    filterProto = filterProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "filter")->asObject(ctx));
    filterProto = filterProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_filter));
    filterProto = filterProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(nullptr, py_python_ignore_init));
    filterProto = filterProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(filterProto), py_self_iter));
    filterProto = filterProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(filterProto), py_filter_next));
    builtins = builtins->setAttribute(ctx, pEnv->getFilterProtoString(), filterProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "filter"), filterProto);
    const proto::ProtoObject* mapProto = ctx->newObject(false);
    if (objectProto) mapProto = mapProto->addParent(ctx, objectProto);
    if (typeProto) mapProto = mapProto->setAttribute(ctx, py_class_local, typeProto);
    mapProto = mapProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "map")->asObject(ctx));
    mapProto = mapProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_map));
    mapProto = mapProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(nullptr, py_python_ignore_init));
    mapProto = mapProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(mapProto), py_self_iter));
    mapProto = mapProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(mapProto), py_map_next));
    builtins = builtins->setAttribute(ctx, pEnv->getMapProtoString(), mapProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "map"), mapProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "sum"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_sum));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "all"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_all));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "any"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_any));

    const proto::ProtoObject* enumProto = ctx->newObject(false);
    if (objectProto) enumProto = enumProto->addParent(ctx, objectProto);
    if (typeProto) enumProto = enumProto->setAttribute(ctx, py_class_local, typeProto);
    enumProto = enumProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "enumerate")->asObject(ctx));
    enumProto = enumProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_enumerate));
    enumProto = enumProto->setAttribute(ctx, pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(nullptr, py_python_ignore_init));
    enumProto = enumProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(enumProto), py_self_iter));
    enumProto = enumProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(enumProto), py_enumerate_next));
    builtins = builtins->setAttribute(ctx, pEnv->getEnumProtoString(), enumProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "enumerate"), enumProto);

    const proto::ProtoObject* revProto = ctx->newObject(false);
    if (objectProto) revProto = revProto->addParent(ctx, objectProto);
    if (typeProto) revProto = revProto->setAttribute(ctx, py_class_local, typeProto);
    revProto = revProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "reversed")->asObject(ctx));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "reversed"), revProto);

    // rangeProto initialization was removed as rangeClass handles instantiation now
    
    revProto = revProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_self_iter));
    revProto = revProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_reversed_next));
    revProto = revProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_reversed));
    builtins = builtins->setAttribute(ctx, pEnv->getRevProtoString(), revProto);
    // Also update the builtins dict with the fully initialized revProto
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "reversed"), revProto);

    // Register enumerate and reversed AFTER their prototypes are set, so 'builtins' has them.
    // They are already registered at lines 3617 and 3623. Do NOT overwrite them as methods here.

    // Note: rangeProto initialization was moved up

    // Initialize specialized RangeIterator prototype
    const proto::ProtoObject* rangeIterProto = ctx->newObject(false);
    if (objectProto) rangeIterProto = rangeIterProto->addParent(ctx, objectProto);
    if (typeProto) rangeIterProto = rangeIterProto->setAttribute(ctx, py_class_local, typeProto);
    rangeIterProto = rangeIterProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "range_iterator")->asObject(ctx));
    rangeIterProto = rangeIterProto->setAttribute(ctx, pEnv->getIterString(), ctx->fromMethod(nullptr, py_self_iter));
    rangeIterProto = rangeIterProto->setAttribute(ctx, pEnv->getNextString(), ctx->fromMethod(nullptr, py_range_next));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "range_iterator"), rangeIterProto);
    pEnv->setRangeIteratorProto(rangeIterProto);
    // Wire the native cell prototype so ProtoRangeIteratorImplementation tagged pointers
    // resolve attribute lookups (e.g. __iter__, __next__) via the Python-visible prototype.
    ctx->space->rangeIteratorPrototype = const_cast<proto::ProtoObject*>(rangeIterProto);

    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "abs"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_abs));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "min"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_min));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "max"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_max));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "pow"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_pow));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "round"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_round));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "divmod"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_divmod));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "ascii"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_ascii));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "ord"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_ord));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "chr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_chr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "bin"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_bin));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "oct"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_oct));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hex"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hex));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "sorted"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_sorted));

    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "callable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_callable));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_object_getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_getattribute));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_object_setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_object_setattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "delattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_delattr));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "raise"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_raise));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "dir"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_dir));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "vars"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_vars));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "input"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_input));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "breakpoint"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_breakpoint));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "globals"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_globals));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "locals"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_locals));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_tokenize_source"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py__tokenize_source));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "compile"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_compile));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "eval"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_eval));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "exec"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_exec));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hash"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hash));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "help"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_help));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_complete"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_complete));
    auto py_memoryview_new = [](proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) -> const proto::ProtoObject* {
        return py_memoryview(context, self, parentLink, args, kwargs);
    };
    const proto::ProtoObject* memoryviewClass = ctx->newObject(false);
    if (objectProto) memoryviewClass = memoryviewClass->addParent(ctx, objectProto);
    if (typeProto) memoryviewClass = memoryviewClass->setAttribute(ctx, py_class_local, typeProto);
    memoryviewClass = memoryviewClass->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "memoryview")->asObject(ctx));
    memoryviewClass = memoryviewClass->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_memoryview_new));
    memoryviewClass = memoryviewClass->setAttribute(ctx, pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__"), ctx->fromMethod(nullptr, py_python_ignore_init));
    memoryviewClass = memoryviewClass->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "tobytes"), ctx->fromMethod(nullptr, py_memoryview_tobytes));
    memoryviewClass = memoryviewClass->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "cast"), ctx->fromMethod(nullptr, py_memoryview_cast_method));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "memoryview"), memoryviewClass);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "super"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_super));
    const proto::ProtoObject* propertyProto = ctx->newObject(false);
    if (objectProto) propertyProto = propertyProto->addParent(ctx, objectProto);
    if (typeProto) propertyProto = propertyProto->setAttribute(ctx, py_class_local, typeProto);
    propertyProto = propertyProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "property")->asObject(ctx));
    propertyProto = propertyProto->setAttribute(ctx, pEnv->getGetDunderString(), ctx->fromMethod(nullptr, py_property_get));
    propertyProto = propertyProto->setAttribute(ctx, pEnv->getSetDunderString(), ctx->fromMethod(nullptr, py_property_set));
    propertyProto = propertyProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_property));
    const proto::ProtoString* initStr = pEnv ? pEnv->getInitString() : PythonEnvironment::getInternedString(ctx, "__init__");
    propertyProto = propertyProto->setAttribute(ctx, initStr, ctx->fromMethod(nullptr, py_property_init));
    propertyProto = propertyProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "getter"),  ctx->fromMethod(nullptr, py_property_getter_method));
    propertyProto = propertyProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "setter"),  ctx->fromMethod(nullptr, py_property_setter_method));
    propertyProto = propertyProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "deleter"), ctx->fromMethod(nullptr, py_property_deleter_method));
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "property"), propertyProto);
    
    const proto::ProtoObject* staticmethodProto = ctx->newObject(true);
    staticmethodProto = staticmethodProto->setAttribute(ctx, pEnv->getClassString(), typeProto);
    staticmethodProto = staticmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "staticmethod")->asObject(ctx));
    
    // Add MRO so that py_type_getattribute can find descriptor methods
    const proto::ProtoList* smMroList = ctx->newList()->appendLast(ctx, staticmethodProto)->appendLast(ctx, objectProto);
    staticmethodProto = staticmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mro__"), smMroList->asObject(ctx));
    staticmethodProto = staticmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__bases__"), ctx->newList()->appendLast(ctx, objectProto)->asObject(ctx));
    staticmethodProto = staticmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__is_python_class__"), PROTO_TRUE);

    const proto::ProtoObject* classmethodProto = ctx->newObject(true);
    classmethodProto = classmethodProto->setAttribute(ctx, pEnv->getClassString(), typeProto);
    classmethodProto = classmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "classmethod")->asObject(ctx));
    
    // Add MRO so that py_type_getattribute can find descriptor methods
    const proto::ProtoList* cmMroList = ctx->newList()->appendLast(ctx, classmethodProto)->appendLast(ctx, objectProto);
    classmethodProto = classmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mro__"), cmMroList->asObject(ctx));
    classmethodProto = classmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__bases__"), ctx->newList()->appendLast(ctx, objectProto)->asObject(ctx));
    classmethodProto = classmethodProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__is_python_class__"), PROTO_TRUE);

    classmethodProto = classmethodProto->setAttribute(ctx, pEnv->getGetDunderString(), ctx->fromMethod(nullptr, py_classmethod_get));
    classmethodProto = classmethodProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_classmethod));
    
    staticmethodProto = staticmethodProto->setAttribute(ctx, pEnv->getGetDunderString(), ctx->fromMethod(nullptr, py_staticmethod_get));
    staticmethodProto = staticmethodProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_staticmethod));

    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "staticmethod"), staticmethodProto);
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "classmethod"), classmethodProto);
    
    builtins = builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__import__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_import));
    
    // V105: Export exceptions to builtins
    if (exceptionsModule) {
        const proto::ProtoSparseList* attrs = exceptionsModule->getAttributes(ctx);
        if (attrs) {
            const proto::ProtoSparseListIterator* it = attrs->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                unsigned long keyHash = it->nextKey(ctx);
                const proto::ProtoObject* val = it->nextValue(ctx);
                const proto::ProtoString* keyS = reinterpret_cast<const proto::ProtoString*>(keyHash);
                if (keyS) {
                    builtins = builtins->setAttribute(ctx, keyS, val);
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            }
        }
    }

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG IN BUILTINS INIT: END obInB=%p\n", (void*)builtins->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "object")));
        fflush(stderr);
    }
    const proto::ProtoObject* classGetItem = ctx->fromMethod(nullptr, py_type_class_getitem);
    const proto::ProtoObject* typeOr = ctx->fromMethod(nullptr, py_type_or);
    const proto::ProtoString* classGetItemS = PythonEnvironment::getInternedString(ctx, "__class_getitem__");
    const proto::ProtoString* orS = PythonEnvironment::getInternedString(ctx, "__or__");
    const proto::ProtoString* rorS = PythonEnvironment::getInternedString(ctx, "__ror__");

    auto registerGenericSupport = [&](const proto::ProtoObject* proto) {
        if (!proto) return;
        const_cast<proto::ProtoObject*>(proto)->setAttribute(ctx, classGetItemS, classGetItem);
    };

    registerGenericSupport(typeProto);
    registerGenericSupport(listProto);
    registerGenericSupport(dictProto);
    registerGenericSupport(tupleProto);
    registerGenericSupport(setProto);
    registerGenericSupport(frozensetProto);

    const_cast<proto::ProtoObject*>(typeProto)->setAttribute(ctx, orS, typeOr);
    const_cast<proto::ProtoObject*>(typeProto)->setAttribute(ctx, rorS, typeOr);

    // Initialize GenericAlias prototype
    const proto::ProtoObject* genericAliasProto = ctx->newObject(false);
    if (objectProto) genericAliasProto = genericAliasProto->addParent(ctx, objectProto);
    if (typeProto) genericAliasProto = genericAliasProto->setAttribute(ctx, py_class_local, typeProto);
    genericAliasProto = genericAliasProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "GenericAlias")->asObject(ctx));
    genericAliasProto = genericAliasProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_genericalias_new));
    genericAliasProto = genericAliasProto->setAttribute(ctx, pEnv->getReprString(), ctx->fromMethod(nullptr, py_genericalias_repr));
    pEnv->setGenericAliasProto(genericAliasProto);

    // Initialize UnionType prototype
    const proto::ProtoObject* unionTypeProto = ctx->newObject(false);
    if (objectProto) unionTypeProto = unionTypeProto->addParent(ctx, objectProto);
    if (typeProto) unionTypeProto = unionTypeProto->setAttribute(ctx, py_class_local, typeProto);
    unionTypeProto = unionTypeProto->setAttribute(ctx, py_name_local, PythonEnvironment::getInternedString(ctx, "UnionType")->asObject(ctx));
    unionTypeProto = unionTypeProto->setAttribute(ctx, pEnv->getNewString(), ctx->fromMethod(nullptr, py_uniontype_new));
    unionTypeProto = unionTypeProto->setAttribute(ctx, pEnv->getReprString(), ctx->fromMethod(nullptr, py_uniontype_repr));
    pEnv->setUnionTypeProto(unionTypeProto);

    return builtins;
}

} // namespace builtins
} // namespace protoPython
