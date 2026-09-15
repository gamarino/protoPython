/**
 * HPyContext.cpp — protoPython HPy runtime shim implementation (Phase 1).
 * Maps HPy handles to ProtoObject*; core ABI for handle lifecycle, attr, call, type.
 */

#include <protoPython/HPyContext.h>
#include <protoPython/HPyABI.h>
#include <protoPython/PythonEnvironment.h>
#include <iostream>
#include <cstring>
#include <utility>
#include <vector>

namespace protoPython {

HPyContext::HPyContext(proto::ProtoContext* c)
    : ctx(c), space(c ? c->space : nullptr) {
    if (space) roots = space->createRootSet("hpy-handles");
    /** Step 1301: reserve handles to avoid early reallocations. */
    handles.reserve(1024);
}

HPyContext::~HPyContext() {
    // Destroying the root set releases every handle still open.
    if (space && roots) space->destroyRootSet(roots);
}

HPy HPyContext::fromProtoObject(const proto::ProtoObject* obj) {
    if (!obj || !ctx || !roots) return 0;
    const proto::ProtoRootSet::Handle pin = roots->add(obj);
    if (pin == proto::ProtoRootSet::kNullHandle) return 0;
    if (!freeList.empty()) {
        size_t idx = freeList.back();
        freeList.pop_back();
        handles[idx] = pin;
        return static_cast<HPy>(idx + 1);
    }
    handles.push_back(pin);
    return static_cast<HPy>(handles.size());
}

const proto::ProtoObject* HPyContext::asProtoObject(HPy h) const {
    if (h == 0 || h > handles.size() || !roots) return nullptr;
    const proto::ProtoRootSet::Handle pin = handles[h - 1];
    if (pin == proto::ProtoRootSet::kNullHandle) return nullptr;
    return roots->resolve(pin);
}

HPy HPyContext::dup(HPy h) {
    const proto::ProtoObject* obj = asProtoObject(h);
    if (!obj) return 0;
    return fromProtoObject(obj);
}

void HPyContext::close(HPy h) {
    if (h == 0 || h > handles.size() || !roots) return;
    proto::ProtoRootSet::Handle& pin = handles[h - 1];
    if (pin == proto::ProtoRootSet::kNullHandle) return;  // already closed
    roots->remove(pin);
    pin = proto::ProtoRootSet::kNullHandle;
    freeList.push_back(h - 1);
}

// --- Core ABI ---

HPy HPy_FromPyObject(HPyContext* hctx, const proto::ProtoObject* obj) {
    if (!hctx) return 0;
    return hctx->fromProtoObject(obj);
}

const proto::ProtoObject* HPy_AsPyObject(HPyContext* hctx, HPy h) {
    if (!hctx) return nullptr;
    return hctx->asProtoObject(h);
}

HPy HPy_Dup(HPyContext* hctx, HPy h) {
    if (!hctx) return 0;
    return hctx->dup(h);
}

void HPy_Close(HPyContext* hctx, HPy h) {
    if (hctx) hctx->close(h);
}

HPy HPy_GetAttr(HPyContext* hctx, HPy obj, const char* name) {
    if (!hctx || !hctx->ctx || !name) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    if (!o) return 0;

    const proto::ProtoString* nameStr;
    auto it = hctx->stringCache.find(name);
    if (it != hctx->stringCache.end()) {
        nameStr = it->second;
    } else {
        nameStr = PythonEnvironment::getInternedString(hctx->ctx, name);
        if (nameStr) hctx->stringCache[name] = nameStr;
    }

    if (!nameStr) return 0;
    const proto::ProtoObject* attr = o->getAttribute(hctx->ctx, nameStr);
    if (!attr || attr->isNone(hctx->ctx)) return 0;
    return hctx->fromProtoObject(attr);
}

int HPy_SetAttr(HPyContext* hctx, HPy obj, const char* name, HPy value) {
    if (!hctx || !hctx->ctx || !name) return -1;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    const proto::ProtoObject* v = hctx->asProtoObject(value);
    if (!o) return -1;
    if (!v) return -1;
    const proto::ProtoString* nameStr = PythonEnvironment::getInternedString(hctx->ctx, name);
    if (!nameStr) return -1;
    // A mutable object is updated in place and setAttribute returns it. On an
    // immutable object it returns a modified copy and the object behind the
    // handle is unchanged, which is reported as a failure.
    const proto::ProtoObject* result = o->setAttribute(hctx->ctx, nameStr, v);
    return result == o ? 0 : -1;
}

HPy HPy_Call(HPyContext* hctx, HPy callable, const HPy* args, size_t nargs) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* callee = hctx->asProtoObject(callable);
    if (!callee) return 0;
    const proto::ProtoString* callName = PythonEnvironment::getInternedString(hctx->ctx, "__call__");
    if (!callName) return 0;
    const proto::ProtoList* argList = hctx->ctx->newList();
    if (!argList) return 0;
    for (size_t i = 0; i < nargs; ++i) {
        const proto::ProtoObject* a = hctx->asProtoObject(args[i]);
        argList = argList->appendLast(hctx->ctx, a ? a : nullptr);
    }
    const proto::ProtoObject* result = callee->call(hctx->ctx, nullptr, callName, callee,
        argList, nullptr);
    if (!result || result == nullptr) return 0;
    return hctx->fromProtoObject(result);
}

HPy HPy_Type(HPyContext* hctx, HPy obj) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    if (!o) return 0;
    const proto::ProtoObject* typeObj = o->getPrototype(hctx->ctx);
    if (!typeObj) return 0;
    return hctx->fromProtoObject(typeObj);
}

// --- Method Wrapper (Step 1213) ---

namespace {

const proto::ProtoString* internedKey(proto::ProtoContext* ctx, const char* name) {
    return PythonEnvironment::getInternedString(ctx, name);
}

// Native entry point of every HPy function. `self` is the wrapper object built
// by makeHPyFunction: it holds the HPyCFunction pointer (__hpy_meth__) and the
// object passed to the extension as `self` (__hpy_self__: the module for module
// functions, None otherwise). The argument list is pinned by the caller for the
// duration of the call.
const proto::ProtoObject* hpy_method_wrapper(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/) {
    const proto::ProtoObject* methObj = self ? self->getAttribute(context, internedKey(context, "__hpy_meth__")) : nullptr;
    const proto::ProtoExternalPointer* ext =
        (methObj && methObj != PROTO_NONE) ? methObj->asExternalPointer(context) : nullptr;
    if (!ext || !ext->getPointer(context)) return PROTO_NONE;
    const HPyCFunction meth = reinterpret_cast<HPyCFunction>(ext->getPointer(context));

    const proto::ProtoObject* hpySelf = self->getAttribute(context, internedKey(context, "__hpy_self__"));
    if (!hpySelf) hpySelf = PROTO_NONE;

    // Handles opened for this call stay pinned until hctx is destroyed on return;
    // the result is resolved to a ProtoObject* before that.
    HPyContext hctx(context);
    const HPy hSelf = hctx.fromProtoObject(hpySelf);
    const size_t nargs = positionalParameters ? positionalParameters->getSize(context) : 0;
    std::vector<HPy> hArgs(nargs);
    for (size_t i = 0; i < nargs; ++i) {
        hArgs[i] = hctx.fromProtoObject(positionalParameters->getAt(context, static_cast<int>(i)));
    }
    const HPy hResult = meth(&hctx, hSelf, hArgs.data(), nargs);
    if (hResult == HPy_NULL) return PROTO_NONE;
    const proto::ProtoObject* result = hctx.asProtoObject(hResult);
    return result ? result : PROTO_NONE;
}

// Builds the callable for an HPyCFunction: a method cell whose self is a wrapper
// object holding the function pointer and `hpySelf`. The collector traces a
// method cell's self, so the callable keeps the wrapper alive. The wrapper is
// pinned through a handle while it is being built.
const proto::ProtoObject* makeHPyFunction(HPyContext* hctx, const char* name, HPyCFunction meth,
                                          const proto::ProtoObject* hpySelf) {
    proto::ProtoContext* ctx = hctx->ctx;
    const proto::ProtoObject* wrap = ctx->newObject(true);
    const HPy hWrap = hctx->fromProtoObject(wrap);
    wrap = wrap->setAttribute(ctx, internedKey(ctx, "__name__"), internedKey(ctx, name)->asObject(ctx));
    wrap = wrap->setAttribute(ctx, internedKey(ctx, "__hpy_meth__"),
                              ctx->fromExternalPointer(reinterpret_cast<void*>(meth)));
    wrap = wrap->setAttribute(ctx, internedKey(ctx, "__hpy_self__"), hpySelf ? hpySelf : PROTO_NONE);
    const proto::ProtoObject* callable = ctx->fromMethod(const_cast<proto::ProtoObject*>(wrap), hpy_method_wrapper);
    hctx->close(hWrap);
    return callable;
}

}  // namespace

const proto::ProtoObject* HPy_WrapMethod(HPyContext* hctx, const char* name, HPyCFunction meth) {
    if (!hctx || !hctx->ctx || !name || !meth) return nullptr;
    return makeHPyFunction(hctx, name, meth, nullptr);
}

HPy HPyModule_Create(HPyContext* hctx, HPyModuleDef* def) {
    if (!hctx || !hctx->ctx || !def || !def->m_name) return 0;
    proto::ProtoContext* ctx = hctx->ctx;

    // The module is mutable, so attribute writes (HPyModule_AddObject, the
    // loader's __file__ and __name__) update it in place. Inside a
    // PythonEnvironment it also gets the module type, like modules loaded from
    // Python source.
    const proto::ProtoObject* mod = ctx->newObject(true);
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) {
        if (const proto::ProtoObject* moduleType = env->getModulePrototype()) {
            mod = mod->addParent(ctx, moduleType);
            if (env->getClassString()) mod = mod->setAttribute(ctx, env->getClassString(), moduleType);
        }
    }
    const HPy hMod = hctx->fromProtoObject(mod);  // pinned while the module is built
    if (hMod == HPy_NULL) return 0;

    mod = mod->setAttribute(ctx, internedKey(ctx, "__name__"), internedKey(ctx, def->m_name)->asObject(ctx));
    if (def->m_doc) {
        mod = mod->setAttribute(ctx, internedKey(ctx, "__doc__"), internedKey(ctx, def->m_doc)->asObject(ctx));
    }

    // Populate functions from the HPyMethodDef array (Step 1215); each receives
    // the module as `self`.
    if (def->m_methods) {
        const auto* methods = static_cast<const HPyMethodDef*>(def->m_methods);
        for (int i = 0; methods[i].ml_name != nullptr; ++i) {
            if (!methods[i].ml_meth) continue;
            const proto::ProtoObject* fn = makeHPyFunction(hctx, methods[i].ml_name, methods[i].ml_meth, mod);
            mod = mod->setAttribute(ctx, internedKey(ctx, methods[i].ml_name), fn);
        }
    }
    return hMod;
}

// --- Phase 3: API Coverage (v65) ---

HPy HPy_New(HPyContext* hctx, HPy type, void** data) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* t = hctx->asProtoObject(type);
    if (!t) return 0;
    const proto::ProtoObject* obj = t->newChild(hctx->ctx, true);
    if (data) *data = nullptr; // protoCore handles internals differently
    return hctx->fromProtoObject(obj);
}

HPy HPy_FromLong(HPyContext* hctx, long long v) {
    if (!hctx || !hctx->ctx) return 0;
    return hctx->fromProtoObject(hctx->ctx->fromInteger(v));
}

HPy HPy_FromDouble(HPyContext* hctx, double v) {
    if (!hctx || !hctx->ctx) return 0;
    return hctx->fromProtoObject(hctx->ctx->fromDouble(v));
}

HPy HPy_FromUTF8(HPyContext* hctx, const char* utf8) {
    if (!hctx || !hctx->ctx || !utf8) return 0;
    return hctx->fromProtoObject(PythonEnvironment::getInternedString(hctx->ctx, utf8)->asObject(hctx->ctx));
}

long long HPy_AsLong(HPyContext* hctx, HPy h) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(h);
    if (!o) return 0;
    if (o->isInteger(hctx->ctx)) return o->asLong(hctx->ctx);
    if (o->isDouble(hctx->ctx)) return static_cast<long long>(o->asDouble(hctx->ctx));
    if (o->isBoolean(hctx->ctx)) return o->asBoolean(hctx->ctx) ? 1 : 0;
    return 0;
}

double HPy_AsDouble(HPyContext* hctx, HPy h) {
    if (!hctx || !hctx->ctx) return 0.0;
    const proto::ProtoObject* o = hctx->asProtoObject(h);
    if (!o) return 0.0;
    if (o->isDouble(hctx->ctx)) return o->asDouble(hctx->ctx);
    if (o->isInteger(hctx->ctx)) return static_cast<double>(o->asLong(hctx->ctx));
    if (o->isBoolean(hctx->ctx)) return o->asBoolean(hctx->ctx) ? 1.0 : 0.0;
    return 0.0;
}

const char* HPy_AsUTF8(HPyContext* hctx, HPy h) {
    if (!hctx) return nullptr;
    const proto::ProtoObject* o = hctx->asProtoObject(h);
    if (!o || !o->isString(hctx->ctx)) return nullptr;
    // Note: This returns a temporary pointer. HPy expects it to be valid until handle closed.
    // In protoCore, strings are immutably stored in the space.
    static std::string lastStr; // Simple shim for single-thread use; in full prod we'd need handle-linked storage
    o->asString(hctx->ctx)->toUTF8String(hctx->ctx, lastStr);
    return lastStr.c_str();
}

void HPyErr_SetString(HPyContext* hctx, HPy type, const char* msg) {
    if (!hctx || !hctx->ctx) return;
    const proto::ProtoObject* t = hctx->asProtoObject(type);
    if (!t) return;
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, PythonEnvironment::getInternedString(hctx->ctx, msg)->asObject(hctx->ctx));
    const proto::ProtoObject* exc = t->call(hctx->ctx, nullptr, proto::ProtoString::createSymbol(hctx->ctx, "__call__"), t, args, nullptr);
    if (exc) {
        // protoPython-specific exception setter would be called here if available via HPyContext
        // For now we just log it or set it if we have access to the PythonEnvironment (Phase 4 integration)
    }
}

int HPyErr_Occurred(HPyContext* hctx) {
    return 0; // Stub for Phase 3
}

void HPyErr_Clear(HPyContext* hctx) {
    // Stub for Phase 3
}

HPy HPy_Add(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    if (!o1 || !o2) return 0;
    const proto::ProtoString* addName = proto::ProtoString::createSymbol(hctx->ctx, "__add__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    const proto::ProtoObject* res = o1->call(hctx->ctx, nullptr, addName, o1, args, nullptr);
    return hctx->fromProtoObject(res);
}

HPy HPy_Sub(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* subName = proto::ProtoString::createSymbol(hctx->ctx, "__sub__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    const proto::ProtoObject* res = o1->call(hctx->ctx, nullptr, subName, o1, args, nullptr);
    return hctx->fromProtoObject(res);
}

HPy HPy_Mul(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* mulName = proto::ProtoString::createSymbol(hctx->ctx, "__mul__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    const proto::ProtoObject* res = o1->call(hctx->ctx, nullptr, mulName, o1, args, nullptr);
    return hctx->fromProtoObject(res);
}

HPy HPy_Div(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* divName = proto::ProtoString::createSymbol(hctx->ctx, "__truediv__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    const proto::ProtoObject* res = o1->call(hctx->ctx, nullptr, divName, o1, args, nullptr);
    return hctx->fromProtoObject(res);
}

HPy HPy_GetItem(HPyContext* hctx, HPy obj, HPy key) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    const proto::ProtoObject* k = hctx->asProtoObject(key);
    if (!o || !k) return 0;
    const proto::ProtoString* giName = proto::ProtoString::createSymbol(hctx->ctx, "__getitem__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, k);
    const proto::ProtoObject* res = o->call(hctx->ctx, nullptr, giName, o, args, nullptr);
    return hctx->fromProtoObject(res);
}

int HPy_SetItem(HPyContext* hctx, HPy obj, HPy key, HPy value) {
    if (!hctx || !hctx->ctx) return -1;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    const proto::ProtoObject* k = hctx->asProtoObject(key);
    const proto::ProtoObject* v = hctx->asProtoObject(value);
    if (!o || !k) return -1;
    const proto::ProtoString* siName = proto::ProtoString::createSymbol(hctx->ctx, "__setitem__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, k)->appendLast(hctx->ctx, v);
    const proto::ProtoObject* res = o->call(hctx->ctx, nullptr, siName, o, args, nullptr);
    return (res != nullptr) ? 0 : -1;
}

size_t HPy_Length(HPyContext* hctx, HPy obj) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    if (!o) return 0;
    const proto::ProtoString* lenName = proto::ProtoString::createSymbol(hctx->ctx, "__len__");
    const proto::ProtoObject* res = o->call(hctx->ctx, nullptr, lenName, o, nullptr, nullptr);
    return (res && res->isInteger(hctx->ctx)) ? static_cast<size_t>(res->asLong(hctx->ctx)) : 0;
}

int HPy_Contains(HPyContext* hctx, HPy container, HPy item) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* c = hctx->asProtoObject(container);
    const proto::ProtoObject* i = hctx->asProtoObject(item);
    if (!c || !i) return 0;
    const proto::ProtoString* hasName = proto::ProtoString::createSymbol(hctx->ctx, "__contains__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, i);
    const proto::ProtoObject* res = c->call(hctx->ctx, nullptr, hasName, c, args, nullptr);
    return (res && res->isBoolean(hctx->ctx)) ? (res->asBoolean(hctx->ctx) ? 1 : 0) : 0;
}

int HPy_IsTrue(HPyContext* hctx, HPy h) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(h);
    if (!o) return 0;
    if (o->isBoolean(hctx->ctx)) return o->asBoolean(hctx->ctx) ? 1 : 0;
    if (o->isInteger(hctx->ctx)) return o->asLong(hctx->ctx) != 0 ? 1 : 0;
    return 1;
}

HPy HPy_GetAttr_s(HPyContext* hctx, HPy obj, const char* name) {
    return HPy_GetAttr(hctx, obj, name);
}

int HPy_SetAttr_s(HPyContext* hctx, HPy obj, const char* name, HPy value) {
    return HPy_SetAttr(hctx, obj, name, value);
}

HPy HPyErr_NewException(HPyContext* hctx, const char* name, HPy base, HPy dict) {
    if (!hctx || !hctx->ctx || !name) return 0;
    const proto::ProtoObject* baseObj = hctx->asProtoObject(base);
    // In protoCore, we can just create a child of the base exception (or Exception if base is 0)
    if (!baseObj) {
        // Find 'Exception' in the context or use a default
        // For Phase 3/4, we assume HPy modules likely provide their own bases or use 0
        baseObj = hctx->ctx->newObject(false); // Default to a new object if base unidentified
    }
    const proto::ProtoObject* exc = baseObj->newChild(hctx->ctx, true);
    const proto::ProtoString* py_name = proto::ProtoString::createSymbol(hctx->ctx, "__name__");
    exc = exc->setAttribute(hctx->ctx, py_name, PythonEnvironment::getInternedString(hctx->ctx, name)->asObject(hctx->ctx));
    
    if (dict) {
        const proto::ProtoObject* d = hctx->asProtoObject(dict);
        if (d) {
            // Merge dict into exc attributes (simplified for now)
        }
    }
    return hctx->fromProtoObject(exc);
}

int HPyModule_AddObject(HPyContext* hctx, HPy mod, const char* name, HPy obj) {
    if (!hctx || !hctx->ctx || !name) return -1;
    const proto::ProtoObject* m = hctx->asProtoObject(mod);
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    if (!m || !o) return -1;
    
    // Step 1292: If it's a type (prototype), we might want to ensure it has a back-link to the module
    // For now, setting it as an attribute is the primary way protoPython handles it.
    return HPy_SetAttr_s(hctx, mod, name, obj);
}

// --- Phase 3.2: Advanced API (v66) ---

HPy HPy_And(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__and__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    return hctx->fromProtoObject(o1->call(hctx->ctx, nullptr, name, o1, args, nullptr));
}

HPy HPy_Or(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__or__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    return hctx->fromProtoObject(o1->call(hctx->ctx, nullptr, name, o1, args, nullptr));
}

HPy HPy_Xor(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__xor__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    return hctx->fromProtoObject(o1->call(hctx->ctx, nullptr, name, o1, args, nullptr));
}

HPy HPy_LShift(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__lshift__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    return hctx->fromProtoObject(o1->call(hctx->ctx, nullptr, name, o1, args, nullptr));
}

HPy HPy_RShift(HPyContext* hctx, HPy h1, HPy h2) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__rshift__");
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    return hctx->fromProtoObject(o1->call(hctx->ctx, nullptr, name, o1, args, nullptr));
}

HPy HPy_RichCompare(HPyContext* hctx, HPy h1, HPy h2, int op) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o1 = hctx->asProtoObject(h1);
    const proto::ProtoObject* o2 = hctx->asProtoObject(h2);
    const char* method = "";
    switch (op) {
        case HPy_LT: method = "__lt__"; break;
        case HPy_LE: method = "__le__"; break;
        case HPy_EQ: method = "__eq__"; break;
        case HPy_NE: method = "__ne__"; break;
        case HPy_GT: method = "__gt__"; break;
        case HPy_GE: method = "__ge__"; break;
        default: return 0;
    }
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, method);
    const proto::ProtoList* args = hctx->ctx->newList()->appendLast(hctx->ctx, o2);
    return hctx->fromProtoObject(o1->call(hctx->ctx, nullptr, name, o1, args, nullptr));
}

HPy HPyList_New(HPyContext* hctx, size_t len) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoList* list = hctx->ctx->newList();
    // Pre-filling with None if needed? Usually HPyList_New returns empty list if size 0, 
    // but in Python it's fixed size. protoCore lists are dynamic.
    return hctx->fromProtoObject(list->asObject(hctx->ctx));
}

int HPyList_Append(HPyContext* hctx, HPy list, HPy item) {
    if (!hctx || !hctx->ctx) return -1;
    const proto::ProtoObject* l = hctx->asProtoObject(list);
    const proto::ProtoObject* i = hctx->asProtoObject(item);
    if (!l || !l->asList(hctx->ctx)) return -1;
    const_cast<proto::ProtoList*>(l->asList(hctx->ctx))->appendLast(hctx->ctx, i);
    return 0;
}

HPy HPyDict_New(HPyContext* hctx) {
    if (!hctx || !hctx->ctx) return 0;
    return hctx->fromProtoObject(hctx->ctx->newSparseList()->asObject(hctx->ctx)); // Dicts are SparseLists in protoCore
}

int HPyDict_SetItem(HPyContext* hctx, HPy dict, HPy key, HPy value) {
    return HPy_SetItem(hctx, dict, key, value);
}

HPy HPyDict_GetItem(HPyContext* hctx, HPy dict, HPy key) {
    return HPy_GetItem(hctx, dict, key);
}

HPy HPyTuple_New(HPyContext* hctx, size_t len) {
    return HPyList_New(hctx, len); // Stubs for Phase 3
}

HPy HPy_GetIter(HPyContext* hctx, HPy obj) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__iter__");
    return hctx->fromProtoObject(o->call(hctx->ctx, nullptr, name, o, nullptr, nullptr));
}

HPy HPy_Next(HPyContext* hctx, HPy iter) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* i = hctx->asProtoObject(iter);
    const proto::ProtoString* name = proto::ProtoString::createSymbol(hctx->ctx, "__next__");
    return hctx->fromProtoObject(i->call(hctx->ctx, nullptr, name, i, nullptr, nullptr));
}

HPy HPySlice_New(HPyContext* hctx, HPy start, HPy stop, HPy step) {
    if (!hctx || !hctx->ctx) return 0;
    // Mock slice object with start, stop and step attributes.
    proto::ProtoContext* ctx = hctx->ctx;
    const proto::ProtoObject* slice = ctx->newObject(true);
    const HPy hSlice = hctx->fromProtoObject(slice);
    for (const auto& field : {std::make_pair("start", start), std::make_pair("stop", stop), std::make_pair("step", step)}) {
        const proto::ProtoObject* v = hctx->asProtoObject(field.second);
        slice = slice->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, field.first), v ? v : PROTO_NONE);
    }
    return hSlice;
}

HPy HPyType_FromSpec(HPyContext* hctx, HPyType_Spec* spec) {
    if (!hctx || !hctx->ctx || !spec) return 0;
    const proto::ProtoObject* typeObj = hctx->ctx->newObject(true);
    const proto::ProtoString* py_name = proto::ProtoString::createSymbol(hctx->ctx, "__name__");
    typeObj = typeObj->setAttribute(hctx->ctx, py_name, PythonEnvironment::getInternedString(hctx->ctx, spec->name)->asObject(hctx->ctx));
    // Implementation of slots would go here (Step 1256)
    return hctx->fromProtoObject(typeObj);
}

int HPyModule_AddStringConstant(HPyContext* hctx, HPy mod, const char* name, const char* value) {
    return HPy_SetAttr_s(hctx, mod, name, HPy_FromUTF8(hctx, value));
}

int HPyModule_AddIntConstant(HPyContext* hctx, HPy mod, const char* name, long long value) {
    return HPy_SetAttr_s(hctx, mod, name, HPy_FromLong(hctx, value));
}

HPy HPy_CallMethod(HPyContext* hctx, HPy obj, const char* name, const HPy* args, size_t nargs) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    const proto::ProtoString* attrName = proto::ProtoString::createSymbol(hctx->ctx, name);
    const proto::ProtoList* pyArgs = hctx->ctx->newList();
    for (size_t i = 0; i < nargs; ++i) {
        const proto::ProtoObject* a = hctx->asProtoObject(args[i]);
        pyArgs = pyArgs->appendLast(hctx->ctx, a ? a : PROTO_NONE);
    }
    return hctx->fromProtoObject(o->call(hctx->ctx, nullptr, attrName, o, pyArgs, nullptr));
}

void HPy_Dump(HPyContext* hctx, HPy h) {
    if (!hctx || !hctx->ctx) return;
    const proto::ProtoObject* o = hctx->asProtoObject(h);
    if (!o) {
        // log removed
        return;
    }
    // log removed
}

} // namespace protoPython
