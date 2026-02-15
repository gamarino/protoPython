#include <protoPython/ReModule.h>
#include <regex>
#include <string>

namespace protoPython {
namespace re {

static const proto::ProtoObject* py_compile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1 || !posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    std::string pat;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, pat);
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__pattern_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* p = proto->newChild(ctx, true);
    p = p->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__re_pattern__"), ctx->fromUTF8String(pat.c_str()));
    return p;
}

static const proto::ProtoObject* py_match(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    if (!patObj || !strObj || !patObj->isString(ctx) && !patObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__re_pattern__")))
        return PROTO_NONE;
    std::string pat;
    const proto::ProtoObject* patAttr = patObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__re_pattern__"));
    if (patAttr && patAttr->isString(ctx))
        patAttr->asString(ctx)->toUTF8String(ctx, pat);
    else if (patObj->isString(ctx))
        patObj->asString(ctx)->toUTF8String(ctx, pat);
    else
        return PROTO_NONE;
    std::string s;
    if (!strObj->isString(ctx)) return PROTO_NONE;
    strObj->asString(ctx)->toUTF8String(ctx, s);

    std::regex re;
    try {
        re = std::regex(pat);
    } catch (...) {
        return PROTO_NONE;
    }
    std::smatch m;
    if (!std::regex_search(s, m, re) || m.position() != 0) return PROTO_NONE;

    const proto::ProtoObject* matchProto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__match_proto__"));
    if (!matchProto) return PROTO_NONE;
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);
    mo = mo->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__re_match_str__"), ctx->fromUTF8String(m.str().c_str()));
    return mo;
}

static const proto::ProtoObject* py_search(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    if (!patObj || !strObj) return PROTO_NONE;
    std::string pat;
    const proto::ProtoObject* patAttr = patObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__re_pattern__"));
    if (patAttr && patAttr->isString(ctx))
        patAttr->asString(ctx)->toUTF8String(ctx, pat);
    else if (patObj->isString(ctx))
        patObj->asString(ctx)->toUTF8String(ctx, pat);
    else
        return PROTO_NONE;
    std::string s;
    if (!strObj->isString(ctx)) return PROTO_NONE;
    strObj->asString(ctx)->toUTF8String(ctx, s);

    std::regex re;
    try {
        re = std::regex(pat);
    } catch (...) {
        return PROTO_NONE;
    }
    std::smatch m;
    if (!std::regex_search(s, m, re)) return PROTO_NONE;

    const proto::ProtoObject* matchProto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__match_proto__"));
    if (!matchProto) return PROTO_NONE;
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);
    mo = mo->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__re_match_str__"), ctx->fromUTF8String(m.str().c_str()));
    return mo;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    const proto::ProtoObject* patternProto = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__pattern_proto__"), patternProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "compile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_compile));

    const proto::ProtoObject* matchProto = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__match_proto__"), matchProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "match"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_match));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "search"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_search));

    // Regex flags
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "IGNORECASE"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "I"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "LOCALE"), ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "L"), ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "MULTILINE"), ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "M"), ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "DOTALL"), ctx->fromInteger(16));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S"), ctx->fromInteger(16));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "UNICODE"), ctx->fromInteger(32));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "U"), ctx->fromInteger(32));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "VERBOSE"), ctx->fromInteger(64));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "X"), ctx->fromInteger(64));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "DEBUG"), ctx->fromInteger(128));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ASCII"), ctx->fromInteger(256));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "A"), ctx->fromInteger(256));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "NOFLAG"), ctx->fromInteger(0));

    return mod;
}

} // namespace re
} // namespace protoPython
