/**
 * HPyContext.cpp — protoPython HPy runtime shim implementation (Phase 1).
 * Maps HPy handles to ProtoObject*; core ABI for handle lifecycle, attr, call, type.
 */

#include <protoPython/HPyContext.h>
#include <cstring>

namespace protoPython {

HPy HPyContext::fromProtoObject(const proto::ProtoObject* obj) {
    if (!obj || !ctx) return 0;
    handles.push_back(obj);
    return static_cast<HPy>(handles.size());
}

const proto::ProtoObject* HPyContext::asProtoObject(HPy h) const {
    if (h == 0 || h > handles.size()) return nullptr;
    return handles[h - 1];
}

HPy HPyContext::dup(HPy h) {
    const proto::ProtoObject* obj = asProtoObject(h);
    if (!obj) return 0;
    return fromProtoObject(obj);
}

void HPyContext::close(HPy h) {
    if (h == 0 || h > handles.size()) return;
    handles[h - 1] = nullptr;
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
    const proto::ProtoString* nameStr = reinterpret_cast<const proto::ProtoString*>(
        hctx->ctx->fromUTF8String(name));
    if (!nameStr) return 0;
    const proto::ProtoObject* attr = o->getAttribute(hctx->ctx, nameStr);
    if (!attr || attr == nullptr) return 0;
    return hctx->fromProtoObject(attr);
}

int HPy_SetAttr(HPyContext* hctx, HPy obj, const char* name, HPy value) {
    if (!hctx || !hctx->ctx || !name) return -1;
    const proto::ProtoObject* o = hctx->asProtoObject(obj);
    const proto::ProtoObject* v = hctx->asProtoObject(value);
    if (!o) return -1;
    const proto::ProtoString* nameStr = reinterpret_cast<const proto::ProtoString*>(
        hctx->ctx->fromUTF8String(name));
    if (!nameStr) return -1;
    const proto::ProtoObject* result = o->setAttribute(hctx->ctx, nameStr, v ? v : nullptr);
    return (result != nullptr) ? 0 : -1;
}

HPy HPy_Call(HPyContext* hctx, HPy callable, const HPy* args, size_t nargs) {
    if (!hctx || !hctx->ctx) return 0;
    const proto::ProtoObject* callee = hctx->asProtoObject(callable);
    if (!callee) return 0;
    const proto::ProtoString* callName = reinterpret_cast<const proto::ProtoString*>(
        hctx->ctx->fromUTF8String("__call__"));
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

} // namespace protoPython
