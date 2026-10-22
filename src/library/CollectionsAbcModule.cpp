#include <protoPython/CollectionsAbcModule.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/PythonEnvironment.h>
#include <array>
#include <atomic>
#include <string>
#include <utility>
#include <vector>
#include <protoCore.h>

namespace protoPython {

// Implemented in ExecutionEngine.cpp.
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kwargs);
// The class whose __init_subclass__ chain runs on this thread.
extern thread_local const proto::ProtoObject* g_initSubclassClass;

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
// in the package lib/python3.14/_collections_abc_mixins, loaded lazily (see
// "Lazy methods" below).  What an import needs at once is computed here: each
// ABC's __abstractmethods__, those of every class derived from one (a
// native __init_subclass__ on the root ABCs, in place of ABCMeta.__new__),
// the attributes CPython's class bodies set to None, the names, and the
// registrations of the built-in types.
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
    const char* bases[3];     // direct ABC bases (CPython's hierarchy)
    const char* hook[6];      // methods of a one-trick __subclasshook__, if any
    const char* abstract[3];  // the methods its class body declares abstract
};

// The hierarchy, subclass hooks and @abstractmethod declarations of
// Lib/_collections_abc.py.
const AbcSpec kAbcSpecs[] = {
    {"Hashable", {}, {"__hash__"}, {"__hash__"}},
    {"Awaitable", {}, {"__await__"}, {"__await__"}},
    {"Coroutine", {"Awaitable"}, {"__await__", "send", "throw", "close"}, {"send", "throw"}},
    {"AsyncIterable", {}, {"__aiter__"}, {"__aiter__"}},
    {"AsyncIterator", {"AsyncIterable"}, {"__anext__", "__aiter__"}, {"__anext__"}},
    {"AsyncGenerator", {"AsyncIterator"}, {"__aiter__", "__anext__", "asend", "athrow", "aclose"}, {"asend", "athrow"}},
    {"Iterable", {}, {"__iter__"}, {"__iter__"}},
    {"Iterator", {"Iterable"}, {"__iter__", "__next__"}, {"__next__"}},
    {"Reversible", {"Iterable"}, {"__reversed__", "__iter__"}, {"__reversed__"}},
    {"Generator", {"Iterator"}, {"__iter__", "__next__", "send", "throw", "close"}, {"send", "throw"}},
    {"Sized", {}, {"__len__"}, {"__len__"}},
    {"Container", {}, {"__contains__"}, {"__contains__"}},
    {"Collection", {"Sized", "Iterable", "Container"}, {"__len__", "__iter__", "__contains__"}, {}},
    {"Buffer", {}, {"__buffer__"}, {"__buffer__"}},
    {"Callable", {}, {"__call__"}, {"__call__"}},
    {"Set", {"Collection"}, {}, {}},
    {"MutableSet", {"Set"}, {}, {"add", "discard"}},
    {"Mapping", {"Collection"}, {}, {"__getitem__"}},
    {"MutableMapping", {"Mapping"}, {}, {"__setitem__", "__delitem__"}},
    {"MappingView", {"Sized"}, {}, {}},
    {"KeysView", {"MappingView", "Set"}, {}, {}},
    {"ItemsView", {"MappingView", "Set"}, {}, {}},
    {"ValuesView", {"MappingView", "Collection"}, {}, {}},
    {"Sequence", {"Reversible", "Collection"}, {}, {"__getitem__"}},
    {"MutableSequence", {"Sequence"}, {}, {"__setitem__", "__delitem__", "insert"}},
    {"ByteString", {"Sequence"}, {}, {}},
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
    } catch (const proto::ProtoException&) {
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
    } catch (const proto::ProtoException&) {
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

// ---------------------------------------------------------------------------
// Lazy methods
//
// Compiling and running CPython's method bodies costs several times the rest
// of an import of this module, and most programs that import it (os does,
// for os._Environ) call few of them or none.  So every method CPython's class
// bodies define starts as a native trampoline, one C++ function per (ABC,
// name) entry of kMethods.  The first call of one imports the module of its
// ABC's family (kFamilies), whose install() replaces that family's
// trampolines with CPython's functions, and then calls the real function for
// its receiver: os.environ.get(...) compiles Mapping's methods only.
//
// Threads reaching trampolines at once install once (installFamily).  A
// trampoline someone kept a reference to (f = Mapping.get) keeps
// forwarding.
//
// A trampoline has no __isabstractmethod__; the abstract-method computation
// below answers from kAbcSpecs instead, so it is right before and after the
// install.
// ---------------------------------------------------------------------------

namespace {

struct MethodSpec {
    const char* abc;
    const char* name;
    bool classMethod;
};

// Every attribute Lib/_collections_abc.py defines with `def` in a class body.
const MethodSpec kMethods[] = {
    {"Hashable", "__hash__", false},
    {"Awaitable", "__await__", false},
    {"Coroutine", "send", false}, {"Coroutine", "throw", false}, {"Coroutine", "close", false},
    {"AsyncIterable", "__aiter__", false},
    {"AsyncIterator", "__anext__", false}, {"AsyncIterator", "__aiter__", false},
    {"AsyncGenerator", "__anext__", false}, {"AsyncGenerator", "asend", false},
    {"AsyncGenerator", "athrow", false}, {"AsyncGenerator", "aclose", false},
    {"Iterable", "__iter__", false},
    {"Iterator", "__next__", false}, {"Iterator", "__iter__", false},
    {"Reversible", "__reversed__", false},
    {"Generator", "__next__", false}, {"Generator", "send", false},
    {"Generator", "throw", false}, {"Generator", "close", false},
    {"Sized", "__len__", false},
    {"Container", "__contains__", false},
    {"Buffer", "__buffer__", false},
    {"Callable", "__call__", false},
    {"Set", "__le__", false}, {"Set", "__lt__", false}, {"Set", "__gt__", false},
    {"Set", "__ge__", false}, {"Set", "__eq__", false}, {"Set", "_from_iterable", true},
    {"Set", "__and__", false}, {"Set", "__rand__", false}, {"Set", "isdisjoint", false},
    {"Set", "__or__", false}, {"Set", "__ror__", false}, {"Set", "__sub__", false},
    {"Set", "__rsub__", false}, {"Set", "__xor__", false}, {"Set", "__rxor__", false},
    {"Set", "_hash", false},
    {"MutableSet", "add", false}, {"MutableSet", "discard", false},
    {"MutableSet", "remove", false}, {"MutableSet", "pop", false},
    {"MutableSet", "clear", false}, {"MutableSet", "__ior__", false},
    {"MutableSet", "__iand__", false}, {"MutableSet", "__ixor__", false},
    {"MutableSet", "__isub__", false},
    {"Mapping", "__getitem__", false}, {"Mapping", "get", false},
    {"Mapping", "__contains__", false}, {"Mapping", "keys", false},
    {"Mapping", "items", false}, {"Mapping", "values", false}, {"Mapping", "__eq__", false},
    {"MappingView", "__init__", false}, {"MappingView", "__len__", false},
    {"MappingView", "__repr__", false},
    {"KeysView", "_from_iterable", true}, {"KeysView", "__contains__", false},
    {"KeysView", "__iter__", false},
    {"ItemsView", "_from_iterable", true}, {"ItemsView", "__contains__", false},
    {"ItemsView", "__iter__", false},
    {"ValuesView", "__contains__", false}, {"ValuesView", "__iter__", false},
    {"MutableMapping", "__setitem__", false}, {"MutableMapping", "__delitem__", false},
    {"MutableMapping", "pop", false}, {"MutableMapping", "popitem", false},
    {"MutableMapping", "clear", false}, {"MutableMapping", "update", false},
    {"MutableMapping", "setdefault", false},
    {"Sequence", "__getitem__", false}, {"Sequence", "__iter__", false},
    {"Sequence", "__contains__", false}, {"Sequence", "__reversed__", false},
    {"Sequence", "index", false}, {"Sequence", "count", false},
    {"MutableSequence", "__setitem__", false}, {"MutableSequence", "__delitem__", false},
    {"MutableSequence", "insert", false}, {"MutableSequence", "append", false},
    {"MutableSequence", "clear", false}, {"MutableSequence", "reverse", false},
    {"MutableSequence", "extend", false}, {"MutableSequence", "pop", false},
    {"MutableSequence", "remove", false}, {"MutableSequence", "__iadd__", false},
};
constexpr size_t kMethodCount = sizeof(kMethods) / sizeof(kMethods[0]);

const proto::ProtoObject* callMethodOf(proto::ProtoContext* ctx, size_t index,
                                       const proto::ProtoObject* self,
                                       const proto::ProtoArguments* args,
                                       const proto::ProtoKeywordArguments* kwargs);

template <size_t I>
const proto::ProtoObject* trampoline(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                     const proto::ParentLink*, const proto::ProtoArguments* args,
                                     const proto::ProtoKeywordArguments* kwargs) {
    return callMethodOf(ctx, I, self, args, kwargs);
}

template <size_t... Is>
constexpr std::array<proto::ProtoMethod, sizeof...(Is)> makeTrampolines(std::index_sequence<Is...>) {
    return {{&trampoline<Is>...}};
}
constexpr std::array<proto::ProtoMethod, kMethodCount> kTrampolines =
    makeTrampolines(std::make_index_sequence<kMethodCount>{});

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* s) {
    return PythonEnvironment::getInternedString(ctx, s);
}

// `value` is a trampoline, bare or (for _from_iterable) in a classmethod.
bool isTrampoline(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    if (!value || value == PROTO_NONE) return false;
    proto::ProtoMethod m = value->asMethod(ctx);
    if (!m) {
        const proto::ProtoObject* func = value->getAttribute(ctx, sym(ctx, "__func__"));
        m = func && func != PROTO_NONE ? func->asMethod(ctx) : nullptr;
    }
    if (!m) return false;
    for (proto::ProtoMethod t : kTrampolines)
        if (t == m) return true;
    return false;
}

const proto::ProtoObject* ownValue(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                   const proto::ProtoString* key) {
    return obj->hasOwnAttribute(ctx, key) == PROTO_TRUE ? obj->getOwnAttributeDirect(ctx, key) : nullptr;
}

// The module of lib/python3.14/_collections_abc_mixins that defines each
// ABC's methods.  A family needs no other loaded: its methods reach other
// ABCs' methods through the classes, as trampolines until those load.
struct FamilySpec {
    const char* module;
    const char* abcs[6];
};
const FamilySpec kFamilies[] = {
    {"_collections_abc_mixins.one_trick", {"Hashable", "Sized", "Container", "Buffer", "Callable"}},
    {"_collections_abc_mixins.iterators", {"Iterable", "Iterator", "Reversible", "Generator"}},
    {"_collections_abc_mixins.async_abcs",
     {"Awaitable", "Coroutine", "AsyncIterable", "AsyncIterator", "AsyncGenerator"}},
    {"_collections_abc_mixins.sets", {"Set", "MutableSet"}},
    {"_collections_abc_mixins.mapping", {"Mapping"}},
    {"_collections_abc_mixins.mapping_views", {"MappingView", "KeysView", "ItemsView", "ValuesView"}},
    {"_collections_abc_mixins.mutable_mapping", {"MutableMapping"}},
    {"_collections_abc_mixins.sequences", {"Sequence", "MutableSequence"}},
};

const FamilySpec* familyOf(const char* abc) {
    for (const FamilySpec& f : kFamilies)
        for (const char* a : f.abcs)
            if (a && std::string(a) == abc) return &f;
    return nullptr;
}

// One thread at a time imports a family's module and runs its install():
// an import does not wait for another thread's import of the same module
// (or of the package) to finish, so a second importer could find a module
// without install().  The others wait at a GC safepoint (the installer
// allocates, and a collection needs every thread parked), holding nothing,
// and then find their method in place or install its family themselves.
// A failed install releases the flag; the next call retries.
std::atomic<bool> g_installing{false};
thread_local bool t_installing = false;

void installFamily(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* mod,
                   const FamilySpec& family, const proto::ProtoObject* abc, const proto::ProtoString* key) {
    if (t_installing) {
        env->raiseRuntimeError(ctx, "_collections_abc_mixins called a method of an ABC while it was installed");
    }
    for (;;) {
        const proto::ProtoObject* value = ownValue(ctx, abc, key);
        if (value && !isTrampoline(ctx, value)) return;
        bool expected = false;
        if (g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) break;
        while (g_installing.load(std::memory_order_acquire)) ctx->safepoint();
    }
    struct Release {
        ~Release() {
            t_installing = false;
            g_installing.store(false, std::memory_order_release);
        }
    } release;
    t_installing = true;
    const proto::ProtoObject* mixins = env->importModule(family.module, 0, {"install"});
    env->callMethod(mixins, "install", {mod});
}

const proto::ProtoObject* callMethodOf(proto::ProtoContext* ctx, size_t index,
                                       const proto::ProtoObject* self,
                                       const proto::ProtoArguments* args,
                                       const proto::ProtoKeywordArguments* kwargs) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const MethodSpec& spec = kMethods[index];
    const proto::ProtoObject* mod = env->importModule("_collections_abc");
    const proto::ProtoObject* abc = mod->getAttribute(ctx, sym(ctx, spec.abc));
    const proto::ProtoString* key = sym(ctx, spec.name);
    const proto::ProtoObject* real = ownValue(ctx, abc, key);
    if (!real || isTrampoline(ctx, real)) {
        if (const FamilySpec* family = familyOf(spec.abc)) installFamily(ctx, env, mod, *family, abc, key);
        real = ownValue(ctx, abc, key);
    }
    if (!real || isTrampoline(ctx, real)) {
        env->raiseRuntimeError(ctx, std::string("_collections_abc_mixins did not define ")
            + spec.abc + "." + spec.name);
    }
    // A classmethod receives its class as the first argument already.
    if (spec.classMethod) {
        const proto::ProtoObject* func = real->getAttribute(ctx, sym(ctx, "__func__"));
        if (func && func != PROTO_NONE) real = func;
    }
    // Called on an instance the receiver is `self`; through the class
    // (Mapping.get(d, k)) it is the first argument.
    const proto::ProtoList* callArgs = ctx->newList();
    if (self && self != PROTO_NONE) callArgs = callArgs->appendLast(ctx, self);
    const proto::proto_ulong n = args ? args->getSize(ctx) : 0;
    for (proto::proto_ulong i = 0; i < n; ++i)
        callArgs = callArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
    return invokePythonCallable(ctx, real, argsView(proto::ProtoListArguments(ctx, callArgs)), kwargs);
}

// ---------------------------------------------------------------------------
// Abstract methods
//
// ABCMeta.__new__ computes __abstractmethods__ for every class it creates,
// and the abstract-instantiation check reads the class's own value.  The
// native ABCs have no ABCMeta: their values are computed by initialize(),
// those of a class derived from one by the __init_subclass__ the root ABCs
// carry, with CPython's rule: the names the class defines as abstract, and
// the abstract names of its bases it does not override with a concrete
// attribute.
// ---------------------------------------------------------------------------

const char* const kRoots[] = {
    "Hashable", "Awaitable", "AsyncIterable", "Iterable", "Sized", "Container", "Buffer", "Callable",
};
constexpr size_t kRootCount = sizeof(kRoots) / sizeof(kRoots[0]);

const AbcSpec* specOf(proto::ProtoContext* ctx, const proto::ProtoObject* abc) {
    const proto::ProtoObject* st = ownState(ctx, abc);
    if (!st) return nullptr;
    const proto::ProtoObject* nameObj = st->getAttribute(ctx, abcNameKey(ctx));
    if (!nameObj || !nameObj->isString(ctx)) return nullptr;
    std::string name;
    nameObj->asString(ctx)->toUTF8String(ctx, name);
    return specByName(name);
}

bool declaresAbstract(const AbcSpec* spec, const std::string& name) {
    for (const char* a : spec->abstract)
        if (a && name == a) return true;
    return false;
}

bool isAbstractValue(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* value) {
    if (!value || value == PROTO_NONE) return false;
    const proto::ProtoObject* flag = env->loadAttr(ctx, value, sym(ctx, "__isabstractmethod__"), nullptr, false);
    return flag && flag != PROTO_NONE && env->isTrue(flag);
}

TupleView mroOf(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* cls) {
    const proto::ProtoObject* mro = env->getAttribute(ctx, cls, env->getMroString(), false);
    return mro && mro != PROTO_NONE ? PythonEnvironment::tupleView(ctx, mro) : nullptr;
}

// getattr(cls, name).__isabstractmethod__, where an ABC's own method counts
// as abstract when its class body declares it so (trampolines included).
bool resolvesAbstract(proto::ProtoContext* ctx, PythonEnvironment* env,
                      const proto::ProtoObject* cls, TupleView mro, const std::string& name) {
    const proto::ProtoString* key = sym(ctx, name.c_str());
    const proto::proto_ulong n = mro ? mro->getSize(ctx) : 0;
    for (proto::proto_ulong i = 0; i <= n; ++i) {
        // cls first: not every class lists itself in its own __mro__.
        const proto::ProtoObject* c = i == 0 ? cls : mro->getAt(ctx, static_cast<int>(i - 1));
        if (!c || c == PROTO_NONE) continue;
        const proto::ProtoObject* value = ownValue(ctx, c, key);
        if (!value) continue;
        if (const AbcSpec* spec = specOf(ctx, c)) return declaresAbstract(spec, name);
        return isAbstractValue(ctx, env, value);
    }
    return false;
}

void updateAbstractMethods(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* cls) {
    std::vector<std::string> names;
    auto add = [&](const std::string& n) {
        for (const auto& x : names)
            if (x == n) return;
        names.push_back(n);
    };
    if (const AbcSpec* spec = specOf(ctx, cls)) {
        for (const char* a : spec->abstract)
            if (a) add(a);
    } else if (const proto::ProtoSparseList* own = cls->getOwnAttributes(ctx)) {
        auto* it = const_cast<proto::ProtoSparseListIterator*>(own->getIterator(ctx));
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* k = reinterpret_cast<const proto::ProtoObject*>(it->nextKey(ctx));
            if (k && k->isString(ctx)) {
                const proto::ProtoObject* value = cls->getOwnAttributeDirect(ctx, k->asString(ctx));
                if (isAbstractValue(ctx, env, value)) {
                    std::string n;
                    k->asString(ctx)->toUTF8String(ctx, n);
                    add(n);
                }
            }
            it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
        }
    }
    TupleView mro = mroOf(ctx, env, cls);
    const proto::ProtoObject* basesObj = env->getAttribute(ctx, cls, env->getBasesString(), false);
    TupleView bases = basesObj && basesObj != PROTO_NONE ? PythonEnvironment::tupleView(ctx, basesObj) : nullptr;
    const proto::proto_ulong nb = bases ? bases->getSize(ctx) : 0;
    for (proto::proto_ulong b = 0; b < nb; ++b) {
        const proto::ProtoObject* inherited = env->loadAttr(ctx, bases->getAt(ctx, static_cast<int>(b)),
            sym(ctx, "__abstractmethods__"), nullptr, false);
        if (!inherited || inherited == PROTO_NONE) continue;
        const proto::ProtoObject* it = env->iter(inherited);
        PythonEnvironment::TransientPin pinIt(env, it);
        while (const proto::ProtoObject* n = it ? env->next(it) : nullptr) {
            if (!n->isString(ctx)) continue;
            std::string name;
            n->asString(ctx)->toUTF8String(ctx, name);
            if (resolvesAbstract(ctx, env, cls, mro, name)) add(name);
        }
    }
    const proto::ProtoList* list = ctx->newList();
    for (const auto& n : names) list = list->appendLast(ctx, sym(ctx, n.c_str())->asObject(ctx));
    const proto::ProtoObject* frozen = env->callObject(env->getFrozensetPrototype(), {env->newList(list)});
    env->storeAttr(ctx, cls, sym(ctx, "__abstractmethods__"), frozen);
}

const proto::ProtoObject* initSubclass(proto::ProtoContext* ctx, size_t root,
                                       const proto::ProtoObject* self,
                                       const proto::ProtoArguments* args,
                                       const proto::ProtoKeywordArguments* kwargs);

template <size_t R>
const proto::ProtoObject* initSubclassHook(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                           const proto::ParentLink*, const proto::ProtoArguments* args,
                                           const proto::ProtoKeywordArguments* kwargs) {
    return initSubclass(ctx, R, self, args, kwargs);
}

template <size_t... Rs>
constexpr std::array<proto::ProtoMethod, sizeof...(Rs)> makeHooks(std::index_sequence<Rs...>) {
    return {{&initSubclassHook<Rs>...}};
}
constexpr std::array<proto::ProtoMethod, kRootCount> kHooks =
    makeHooks(std::make_index_sequence<kRootCount>{});

bool isHook(proto::ProtoContext* ctx, const proto::ProtoObject* cls) {
    const proto::ProtoObject* v = ownValue(ctx, cls, sym(ctx, "__init_subclass__"));
    proto::ProtoMethod m = v ? v->asMethod(ctx) : nullptr;
    if (!m) return false;
    for (proto::ProtoMethod h : kHooks)
        if (h == m) return true;
    return false;
}

// The root ABC `root`'s __init_subclass__: computes the new class's
// __abstractmethods__ (once, in the first of these hooks its MRO reaches)
// and continues the cooperative chain: super(root, cls).__init_subclass__.
const proto::ProtoObject* initSubclass(proto::ProtoContext* ctx, size_t root,
                                       const proto::ProtoObject* self,
                                       const proto::ProtoArguments* args,
                                       const proto::ProtoKeywordArguments* kwargs) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    // Implicitly a classmethod: the class arrives first or as the receiver;
    // through super().__init_subclass__() from a hook of a class in front
    // of this root it arrives as neither, and is the class being created.
    const proto::ProtoObject* cls = self && self != PROTO_NONE ? self : g_initSubclassClass;
    if (args && args->getSize(ctx) > 0 && env->isActuallyAClass(ctx, args->getAt(ctx, 0)))
        cls = args->getAt(ctx, 0);
    if (!cls || cls == PROTO_NONE) return PROTO_NONE;
    TupleView mro = mroOf(ctx, env, cls);
    const proto::ProtoObject* owner = nullptr;
    bool first = true;
    const proto::proto_ulong n = mro ? mro->getSize(ctx) : 0;
    for (proto::proto_ulong i = 1; i < n && !owner; ++i) {
        const proto::ProtoObject* c = mro->getAt(ctx, static_cast<int>(i));
        const AbcSpec* spec = specOf(ctx, c);
        if (spec && std::string(spec->name) == kRoots[root]) owner = c;
        else if (isHook(ctx, c)) first = false;
    }
    if (first) updateAbstractMethods(ctx, env, cls);
    if (!owner) return PROTO_NONE;
    const proto::ProtoObject* superType = env->getBuiltins()->getAttribute(ctx, sym(ctx, "super"));
    const proto::ProtoObject* bound = env->callObject(superType, {owner, cls});
    const proto::ProtoObject* next = env->loadAttr(ctx, bound, sym(ctx, "__init_subclass__"));
    invokePythonCallable(ctx, next, argsView(proto::ProtoListArguments(ctx, ctx->newList())), kwargs);
    return PROTO_NONE;
}

// The class-body attributes initialize() sets: trampolines, the None
// attributes, the names, the root hooks, __abstractmethods__.
void installNativeAttributes(proto::ProtoContext* ctx, PythonEnvironment* env,
                             const proto::ProtoObject* all, const char* const* names, size_t count) {
    auto abcNamed = [&](const char* n) {
        return all->getAttribute(ctx, sym(ctx, n));
    };
    const proto::ProtoObject* classmethodType = env->getBuiltins()->getAttribute(ctx, sym(ctx, "classmethod"));
    for (size_t i = 0; i < kMethodCount; ++i) {
        proto::ProtoObject* abc = const_cast<proto::ProtoObject*>(abcNamed(kMethods[i].abc));
        const proto::ProtoObject* method = ctx->fromMethod(nullptr, kTrampolines[i]);
        if (kMethods[i].classMethod) method = env->callObject(classmethodType, {method});
        abc->setAttribute(ctx, sym(ctx, kMethods[i].name), method);
    }
    // A class body defining __eq__ without __hash__ gets __hash__ = None;
    // Mapping says `__reversed__ = None`.
    const proto::ProtoObject* none = PROTO_NONE;
    const_cast<proto::ProtoObject*>(abcNamed("Set"))->setAttribute(ctx, sym(ctx, "__hash__"), none);
    const_cast<proto::ProtoObject*>(abcNamed("Mapping"))->setAttribute(ctx, sym(ctx, "__hash__"), none);
    const_cast<proto::ProtoObject*>(abcNamed("Mapping"))->setAttribute(ctx, sym(ctx, "__reversed__"), none);
    for (size_t r = 0; r < kRootCount; ++r) {
        const_cast<proto::ProtoObject*>(abcNamed(kRoots[r]))->setAttribute(ctx,
            sym(ctx, "__init_subclass__"), ctx->fromMethod(nullptr, kHooks[r]));
    }
    // Lib/_collections_abc.py runs as `collections.abc`.
    const proto::ProtoObject* moduleName = sym(ctx, "collections.abc")->asObject(ctx);
    for (size_t i = 0; i < count; ++i) {
        proto::ProtoObject* abc = const_cast<proto::ProtoObject*>(abcNamed(names[i]));
        abc->setAttribute(ctx, sym(ctx, "__module__"), moduleName);
        abc->setAttribute(ctx, sym(ctx, "__qualname__"), sym(ctx, names[i])->asObject(ctx));
    }
    // kAbcSpecs lists every base before the classes deriving from it.
    for (const AbcSpec& spec : kAbcSpecs) updateAbstractMethods(ctx, env, abcNamed(spec.name));
}

} // namespace

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
        // The dict views.  The built-in iterator types CPython registers with
        // Iterator pass its subclass hook already.
        const proto::ProtoObject* emptyDict = env->callObject(env->getDictPrototype(), {});
        PythonEnvironment::TransientPin pinDict(env, emptyDict);
        reg("KeysView", env->getType(ctx, env->callMethod(emptyDict, "keys", {})));
        reg("ValuesView", env->getType(ctx, env->callMethod(emptyDict, "values", {})));
        reg("ItemsView", env->getType(ctx, env->callMethod(emptyDict, "items", {})));
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

    // The class bodies' attributes; CPython's methods arrive on first use.
    if (env) {
        PythonEnvironment::TransientPin pinMod(env, mod);
        installNativeAttributes(ctx, env, all, names, sizeof(names) / sizeof(names[0]));
    }

    return mod;
}

} // namespace collections_abc
} // namespace protoPython
