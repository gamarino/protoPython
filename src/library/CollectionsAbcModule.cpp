#include <protoPython/CollectionsAbcModule.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/PythonEnvironment.h>
#include <string>
#include <vector>
#include <protoCore.h>

namespace protoPython {

static const proto::ProtoObject* py_check_methods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* sparseArgs) {


    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_FALSE;
    const proto::ProtoObject* C = posArgs->getAt(ctx, 0);
    // STRUCT-132: descriptor-aware __mro__ read.
    PythonEnvironment* envABC = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* mroABCS = proto::ProtoString::createSymbol(ctx, "__mro__");
    const proto::ProtoObject* mro = envABC
        ? envABC->getAttribute(ctx, C, mroABCS, false)
        : C->getAttribute(ctx, mroABCS);
    if (!mro) return PROTO_FALSE;
    const proto::ProtoList* mroList = mro->asList(ctx);
    if (!mroList) return PROTO_FALSE;

    for (size_t i = 1; i < posArgs->getSize(ctx); ++i) {
        const proto::ProtoObject* methodObj = posArgs->getAt(ctx, static_cast<int>(i));
        const proto::ProtoString* methodName = methodObj ? methodObj->asString(ctx) : nullptr;
        if (!methodName) continue;

        bool found = false;
        for (size_t j = 0; j < mroList->getSize(ctx); ++j) {
            const proto::ProtoObject* base = mroList->getAt(ctx, static_cast<int>(j));
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

// ---------------------------------------------------------------------------
// Module layout
//
// Lib/_collections_abc.py cannot run as is here, so this module creates the
// ABC class objects natively, with CPython's hierarchy (__bases__, a C3
// __mro__ and the protoCore parent chain in that order), and keeps their
// registries of virtual subclasses and subclass hooks (below).  The methods
// the ABCs define, mixins and abstract defaults alike, are CPython's own,
// in lib/python3.14/_collections_abc_mixins.py: initialize() imports that
// module and calls its install() with this module once the classes exist.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Virtual subclasses
//
// These ABCs are native stand-ins for Lib/_collections_abc.py, so the
// register() calls that file makes (MutableMapping.register(dict), ...) and
// ABCMeta's __subclasshook__ never ran: isinstance({}, Mapping) was False,
// and collections.Counter, which tests isinstance(x, Mapping), counted the
// keys of a mapping or of **kwargs instead of adding their values.
//
// Each ABC owns an `__abc_state__` object (mutable) holding its registry of
// virtual subclasses.  isVirtualSubclass() is called by issubclass/isinstance
// only after the ordinary MRO check failed, so the common path pays nothing.
// ---------------------------------------------------------------------------

namespace {

struct AbcSpec {
    const char* name;
    const char* bases[3];   // direct ABC bases (CPython's hierarchy)
    const char* hook[6];    // methods of a one-trick __subclasshook__, if any
};

// The hierarchy and subclass hooks of Lib/_collections_abc.py.
const AbcSpec kAbcSpecs[] = {
    {"Hashable", {}, {"__hash__"}},
    {"Awaitable", {}, {"__await__"}},
    {"Coroutine", {"Awaitable"}, {"__await__", "send", "throw", "close"}},
    {"AsyncIterable", {}, {"__aiter__"}},
    {"AsyncIterator", {"AsyncIterable"}, {"__anext__", "__aiter__"}},
    {"AsyncGenerator", {"AsyncIterator"}, {"__aiter__", "__anext__", "asend", "athrow", "aclose"}},
    {"Iterable", {}, {"__iter__"}},
    {"Iterator", {"Iterable"}, {"__iter__", "__next__"}},
    {"Reversible", {"Iterable"}, {"__reversed__", "__iter__"}},
    {"Generator", {"Iterator"}, {"__iter__", "__next__", "send", "throw", "close"}},
    {"Sized", {}, {"__len__"}},
    {"Container", {}, {"__contains__"}},
    {"Collection", {"Sized", "Iterable", "Container"}, {"__len__", "__iter__", "__contains__"}},
    {"Buffer", {}, {"__buffer__"}},
    {"Callable", {}, {"__call__"}},
    {"Set", {"Collection"}, {}},
    {"MutableSet", {"Set"}, {}},
    {"Mapping", {"Collection"}, {}},
    {"MutableMapping", {"Mapping"}, {}},
    {"MappingView", {"Sized"}, {}},
    {"KeysView", {"MappingView", "Set"}, {}},
    {"ItemsView", {"MappingView", "Set"}, {}},
    {"ValuesView", {"MappingView", "Collection"}, {}},
    {"Sequence", {"Reversible", "Collection"}, {}},
    {"MutableSequence", {"Sequence"}, {}},
    {"ByteString", {"Sequence"}, {}},
};

const AbcSpec* specByName(const std::string& name) {
    for (const AbcSpec& s : kAbcSpecs)
        if (name == s.name) return &s;
    return nullptr;
}

// The C3 linearization of an ABC's name and those of its ABC bases, as
// type.__new__ computes __mro__ for the CPython classes (object excluded).
std::vector<std::string> abcMro(const std::string& name) {
    std::vector<std::vector<std::string>> seqs;
    std::vector<std::string> bases;
    if (const AbcSpec* spec = specByName(name)) {
        for (const char* b : spec->bases) {
            if (!b) continue;
            seqs.push_back(abcMro(b));
            bases.push_back(b);
        }
    }
    seqs.push_back(bases);
    std::vector<std::string> result{name};
    for (;;) {
        std::string head;
        for (const auto& s : seqs) {
            if (s.empty()) continue;
            bool inTail = false;
            for (const auto& t : seqs)
                for (size_t i = 1; i < t.size() && !inTail; ++i)
                    if (t[i] == s[0]) inTail = true;
            if (!inTail) { head = s[0]; break; }
        }
        // The hierarchy is CPython's and consistent: no head means done.
        if (head.empty()) break;
        result.push_back(head);
        for (auto& s : seqs)
            if (!s.empty() && s[0] == head) s.erase(s.begin());
    }
    return result;
}

const proto::ProtoString* stateKey(proto::ProtoContext* ctx) {
    return PythonEnvironment::getInternedString(ctx, "__abc_state__");
}
const proto::ProtoString* registryKey(proto::ProtoContext* ctx) {
    return PythonEnvironment::getInternedString(ctx, "registry");
}
const proto::ProtoString* abcNameKey(proto::ProtoContext* ctx) {
    return PythonEnvironment::getInternedString(ctx, "name");
}
const proto::ProtoString* allKey(proto::ProtoContext* ctx) {
    return PythonEnvironment::getInternedString(ctx, "all");
}

// The ABC's own state object, or nullptr for anything that is not one of
// this module's ABCs (a subclass inherits the attribute; it does not own it).
const proto::ProtoObject* ownState(proto::ProtoContext* ctx, const proto::ProtoObject* abc) {
    if (!abc || abc == PROTO_NONE || !abc->isCell(ctx)) return nullptr;
    if (abc->hasOwnAttribute(ctx, stateKey(ctx)) != PROTO_TRUE) return nullptr;
    const proto::ProtoObject* st = abc->getOwnAttributeDirect(ctx, stateKey(ctx));
    return st && st != PROTO_NONE ? st : nullptr;
}

// `cls` is `base` or has it in its __mro__.
bool mroContains(proto::ProtoContext* ctx, PythonEnvironment* env,
                 const proto::ProtoObject* cls, const proto::ProtoObject* base) {
    if (cls == base) return true;
    const proto::ProtoObject* mro = nullptr;
    try {
        mro = env->getAttribute(ctx, cls, env->getMroString(), false);
    } catch (const PyException&) {
        // An unreadable __mro__ reads as no __mro__.
    }
    if (!mro || mro == PROTO_NONE) return false;
    if (::protoPython::TupleView t = ::protoPython::PythonEnvironment::tupleView(ctx, mro)) {
        for (proto::proto_ulong i = 0; i < t->getSize(ctx); ++i)
            if (t->getAt(ctx, static_cast<int>(i)) == base) return true;
    } else if (const proto::ProtoList* l = mro->asList(ctx)) {
        for (proto::proto_ulong i = 0; i < l->getSize(ctx); ++i)
            if (l->getAt(ctx, static_cast<int>(i)) == base) return true;
    }
    return false;
}

// CPython's _check_methods: every method is defined, and not None, in the
// namespace of a class of cls.__mro__.  Reading the attribute off the class
// would also find the metaclass's (type.__call__ made every class Callable).
bool definesAll(proto::ProtoContext* ctx, PythonEnvironment* env,
                const proto::ProtoObject* cls, const char* const* methods) {
    if (!methods[0]) return false;
    const proto::ProtoObject* mro = nullptr;
    try {
        mro = env->getAttribute(ctx, cls, env->getMroString(), false);
    } catch (const PyException&) {
        // An unreadable __mro__ reads as no __mro__.
    }
    ::protoPython::TupleView mroT = mro && mro != PROTO_NONE ? ::protoPython::PythonEnvironment::tupleView(ctx, mro) : nullptr;
    const proto::ProtoList* mroL = mro && mro != PROTO_NONE && !mroT ? mro->asList(ctx) : nullptr;
    const proto::proto_ulong n = mroT ? mroT->getSize(ctx) : (mroL ? mroL->getSize(ctx) : 0);
    for (int i = 0; i < 6 && methods[i]; ++i) {
        const proto::ProtoString* key = PythonEnvironment::getInternedString(ctx, methods[i]);
        bool found = false;
        // cls itself first: some native types (list_iterator) leave
        // themselves out of their own __mro__.
        for (proto::proto_ulong j = 0; j <= n && !found; ++j) {
            const proto::ProtoObject* base = j == 0 ? cls
                : (mroT ? mroT->getAt(ctx, static_cast<int>(j - 1)) : mroL->getAt(ctx, static_cast<int>(j - 1)));
            if (!base || base == PROTO_NONE || base->hasOwnAttribute(ctx, key) != PROTO_TRUE) continue;
            const proto::ProtoObject* m = base->getOwnAttributeDirect(ctx, key);
            if (!m || m == PROTO_NONE) return false;
            found = true;
        }
        if (!found) return false;
    }
    return true;
}

bool virtualSubclass(proto::ProtoContext* ctx, PythonEnvironment* env,
                     const proto::ProtoObject* cls, const proto::ProtoObject* abc, int depth) {
    if (depth > 16) return false;
    const proto::ProtoObject* st = ownState(ctx, abc);
    if (!st) return false;
    if (mroContains(ctx, env, cls, abc)) return true;

    const proto::ProtoObject* regObj = st->getAttribute(ctx, registryKey(ctx));
    if (const proto::ProtoList* reg = regObj ? regObj->asList(ctx) : nullptr) {
        for (proto::proto_ulong i = 0; i < reg->getSize(ctx); ++i)
            if (mroContains(ctx, env, cls, reg->getAt(ctx, static_cast<int>(i)))) return true;
    }

    std::string name;
    const proto::ProtoObject* nameObj = st->getAttribute(ctx, abcNameKey(ctx));
    if (nameObj && nameObj->isString(ctx)) nameObj->asString(ctx)->toUTF8String(ctx, name);
    const AbcSpec* spec = specByName(name);
    if (!spec) return false;
    if (definesAll(ctx, env, cls, spec->hook)) return true;

    // A virtual subclass of a derived ABC is one of this ABC too
    // (dict is registered with MutableMapping, and so is a Mapping).
    const proto::ProtoObject* all = st->getAttribute(ctx, allKey(ctx));
    if (!all || all == PROTO_NONE) return false;
    for (const AbcSpec& sub : kAbcSpecs) {
        for (const char* b : sub.bases) {
            if (!b || name != b) continue;
            const proto::ProtoObject* subAbc = all->getAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, sub.name));
            if (subAbc && virtualSubclass(ctx, env, cls, subAbc, depth + 1)) return true;
        }
    }
    return false;
}

void addToRegistry(proto::ProtoContext* ctx, const proto::ProtoObject* st,
                   const proto::ProtoObject* subclass) {
    const proto::ProtoObject* regObj = st->getAttribute(ctx, registryKey(ctx));
    const proto::ProtoList* reg = regObj ? regObj->asList(ctx) : nullptr;
    if (!reg) reg = ctx->newList();
    reg = reg->appendLast(ctx, subclass);
    const_cast<proto::ProtoObject*>(st)->setAttribute(ctx, registryKey(ctx), reg->asObject(ctx));
}

} // namespace

/** ABC.register(subclass): record a virtual subclass; returns the subclass. */
static const proto::ProtoObject* py_abc_register(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return self;
    // Called through the class (Mapping.register(x)) self is the ABC; as a
    // plain function (register(Mapping, x)) the ABC comes first.
    const proto::ProtoObject* abc = self;
    const proto::ProtoObject* subclass = posArgs->getAt(ctx, 0);
    if (!ownState(ctx, abc) && posArgs->getSize(ctx) >= 2 && ownState(ctx, subclass)) {
        abc = subclass;
        subclass = posArgs->getAt(ctx, 1);
    }
    if (const proto::ProtoObject* st = ownState(ctx, abc)) addToRegistry(ctx, st, subclass);
    return subclass;
}

/** _check_methods(C, *methods) implementation for internal use by os.py/io.py etc. */
static const proto::ProtoObject* py_abc_check_methods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    
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
    for (proto::proto_ulong i = 1; i < args->getSize(ctx); ++i) {
        const proto::ProtoObject* methodObj = args->getAt(ctx, static_cast<int>(i));
        if (!methodObj->isString(ctx)) continue;
        
        proto::proto_ulong methodHash = methodObj->getHash(ctx);
        bool found = false;

        for (proto::proto_ulong j = 0; j < mro->getSize(ctx); ++j) {
            const proto::ProtoObject* B = mro->getAt(ctx, static_cast<int>(j));
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

bool isVirtualSubclass(proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                       const proto::ProtoObject* abc) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !cls || cls == PROTO_NONE) return false;
    return virtualSubclass(ctx, env, cls, abc, 0);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    auto createAbc = [&](const char* name) {
        // Mutable, so the object keeps one identity: an immutable one is
        // replaced by every setAttribute, and the __mro__ setHierarchy builds
        // below named an earlier copy, which then appeared in the MRO of
        // every class deriving from the ABC instead of the ABC itself.
        proto::ProtoObject* abc = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        
        const proto::ProtoString* s_name = env ? env->getNameString() : PythonEnvironment::getInternedString(ctx, "__name__");
        const proto::ProtoString* s_class = env ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__");
        const proto::ProtoString* s_register = PythonEnvironment::getInternedString(ctx, "register");
        const proto::ProtoString* s_is_py_class = PythonEnvironment::getInternedString(ctx, "__is_python_class__");

        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_register, ctx->fromMethod(abc, py_abc_register)));
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_name, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx)));
        
        // Set __class__ to type Prototype
        if (env) {
            abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_class, env->getTypePrototype()));
        }

        // Explicitly mark as a Python class for getType heuristic
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_is_py_class, PROTO_TRUE));

        // Registry of virtual subclasses (see isVirtualSubclass).
        const proto::ProtoObject* state = ctx->newObject(true);
        state = state->setAttribute(ctx, abcNameKey(ctx), PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
        state = state->setAttribute(ctx, registryKey(ctx), ctx->newList()->asObject(ctx));
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, stateKey(ctx), state));

        return abc;
    };

    const proto::ProtoObject* mod = ctx->newObject(false);
    const char* names[] = {
        "Hashable", "Iterable", "Iterator", "Reversible", "Sized", "Container", "Collection",
        "Mapping", "MutableMapping", "Sequence", "MutableSequence",
        "Set", "MutableSet", "Callable", "Awaitable", "Coroutine",
        "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Generator",
        "KeysView", "ValuesView", "ItemsView", "MappingView", "ByteString",
        // Buffer was added to _collections_abc in Python 3.12 (PEP 688).
        // test_collections.py imports it directly: `from _collections_abc
        // import Buffer`. Expose the same minimal ABC stub the others use.
        "Buffer"
    };

    // `all` maps each ABC name to its object, for the walk over derived ABCs.
    const proto::ProtoObject* all = ctx->newObject(true);
    for (const char* name : names) {
        const proto::ProtoString* sName = PythonEnvironment::getInternedString(ctx, name);
        const proto::ProtoObject* abc = createAbc(name);
        mod = mod->setAttribute(ctx, sName, abc);
        all = all->setAttribute(ctx, sName, abc);
    }
    for (const char* name : names) {
        const proto::ProtoObject* abc = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, name));
        if (const proto::ProtoObject* st = ownState(ctx, abc))
            const_cast<proto::ProtoObject*>(st)->setAttribute(ctx, allKey(ctx), all);
    }

    // CPython's hierarchy: __bases__, the C3 __mro__, and the protoCore
    // parent chain equal to __mro__[1:] (what class creation installs for a
    // Python class), so a subclass of MutableSet finds Set's mixins.
    auto setHierarchy = [&](proto::ProtoObject* abc, const char* name) {
        const proto::ProtoObject* objProto = env ? env->getObjectPrototype() : nullptr;
        if (!objProto) return;
        auto abcNamed = [&](const std::string& n) {
            return all->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, n.c_str()));
        };
        const proto::ProtoList* bases = ctx->newList();
        if (const AbcSpec* spec = specByName(name)) {
            for (const char* b : spec->bases)
                if (b) bases = bases->appendLast(ctx, abcNamed(b));
        }
        if (bases->getSize(ctx) == 0) bases = bases->appendLast(ctx, objProto);
        const proto::ProtoList* mro = ctx->newList()->appendLast(ctx, abc);
        const proto::ProtoList* chain = ctx->newList();
        const std::vector<std::string> mroNames = abcMro(name);
        for (size_t i = 1; i < mroNames.size(); ++i) {
            const proto::ProtoObject* base = abcNamed(mroNames[i]);
            mro = mro->appendLast(ctx, base);
            chain = chain->appendLast(ctx, base);
        }
        mro = mro->appendLast(ctx, objProto);
        chain = chain->appendLast(ctx, objProto);
        abc->setParents(ctx, chain);
        abc->setAttribute(ctx, env->getBasesString(), env->newTuple(bases));
        abc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__base__"), bases->getAt(ctx, 0));
        abc->setAttribute(ctx, env->getMroString(), env->newTuple(mro));
    };
    for (const char* name : names) {
        setHierarchy(const_cast<proto::ProtoObject*>(
            all->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, name))), name);
    }
    // The registrations Lib/_collections_abc.py makes for the built-in types.
    if (env) {
        auto reg = [&](const char* abcName, const proto::ProtoObject* type) {
            if (!type || type == PROTO_NONE) return;
            const proto::ProtoObject* abc = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, abcName));
            if (const proto::ProtoObject* st = ownState(ctx, abc)) addToRegistry(ctx, st, type);
        };
        auto builtin = [&](const char* typeName) -> const proto::ProtoObject* {
            const proto::ProtoObject* b = env->getBuiltins();
            return b ? b->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, typeName)) : nullptr;
        };
        reg("Mapping", env->getMappingProxyPrototype());
        reg("MutableMapping", env->getDictPrototype());
        reg("Sequence", env->getTuplePrototype());
        reg("Sequence", env->getStrPrototype());
        reg("Sequence", env->getBytesPrototype());
        reg("Sequence", builtin("range"));
        reg("Sequence", builtin("memoryview"));
        reg("MutableSequence", env->getListPrototype());
        reg("MutableSequence", builtin("bytearray"));
        reg("ByteString", env->getBytesPrototype());
        reg("ByteString", builtin("bytearray"));
        reg("Set", env->getFrozensetPrototype());
        reg("MutableSet", env->getSetPrototype());
        reg("Generator", env->getGeneratorPrototype());
        reg("Coroutine", env->getCoroutinePrototype());
        reg("AsyncGenerator", env->getAsyncGeneratorPrototype());
    }

    const proto::ProtoObject* checkMethodsFunc = ctx->fromMethod(nullptr, py_check_methods);
    const proto::ProtoString* sCheck = PythonEnvironment::getInternedString(ctx, "_check_methods");
    mod = mod->setAttribute(ctx, sCheck, checkMethodsFunc);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "check_methods"), checkMethodsFunc);

    proto::ProtoObject* abcMeta = const_cast<proto::ProtoObject*>(createAbc("ABCMeta"));
    setHierarchy(abcMeta, "ABCMeta");
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "ABCMeta"), abcMeta);

    static const auto py_abstractmethod = [](proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
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

    // CPython's methods of the ABCs.  A failure propagates: an import of
    // _collections_abc without them would hand out unusable classes.
    if (env) {
        PythonEnvironment::TransientPin pinMod(env, mod);
        const proto::ProtoObject* mixins = env->importModule("_collections_abc_mixins");
        if (!mixins || mixins == PROTO_NONE) {
            env->raiseImportError("cannot import _collections_abc_mixins", "_collections_abc_mixins");
        }
        env->callMethod(mixins, "install", {mod});
    }

    return mod;
}

} // namespace collections_abc
} // namespace protoPython
