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
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__pattern_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* p = proto->newChild(ctx, true);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"), proto::ProtoString::fromUTF8(ctx, pat.c_str())->asObject(ctx));
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
    if (!patObj || !strObj || !patObj->isString(ctx) && !patObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__")))
        return PROTO_NONE;
    std::string pat;
    const proto::ProtoObject* patAttr = patObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"));
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

    const proto::ProtoObject* matchProto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    if (!matchProto) return PROTO_NONE;
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_match_str__"), proto::ProtoString::fromUTF8(ctx, m.str().c_str())->asObject(ctx));
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
    const proto::ProtoObject* patAttr = patObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"));
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

    const proto::ProtoObject* matchProto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    if (!matchProto) return PROTO_NONE;
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_match_str__"), proto::ProtoString::fromUTF8(ctx, m.str().c_str())->asObject(ctx));
    return mo;
}

static const proto::ProtoObject* py_escape(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    if (!patObj->isString(ctx)) return patObj; // Or raise TypeError

    std::string s;
    patObj->asString(ctx)->toUTF8String(ctx, s);
    
    std::string escaped;
    for (char c : s) {
        if (!isalnum((unsigned char)c) && c != '_') {
            escaped += '\\';
        }
        escaped += c;
    }
    return proto::ProtoString::fromUTF8(ctx, escaped.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_pattern_match(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 0);
    
    std::string pat;
    const proto::ProtoObject* patAttr = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"));
    if (patAttr && patAttr->isString(ctx))
        patAttr->asString(ctx)->toUTF8String(ctx, pat);
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

    const proto::ProtoObject* matchProto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    // Fallback if the pattern doesn't have it (though it should)
    if (!matchProto) return PROTO_NONE;
    
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_match_str__"), proto::ProtoString::fromUTF8(ctx, m.str().c_str())->asObject(ctx));
    return mo;
}

static const proto::ProtoObject* py_pattern_search(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 0);
    
    std::string pat;
    const proto::ProtoObject* patAttr = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"));
    if (patAttr && patAttr->isString(ctx))
        patAttr->asString(ctx)->toUTF8String(ctx, pat);
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

    const proto::ProtoObject* matchProto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    if (!matchProto) return PROTO_NONE;
    
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_match_str__"), proto::ProtoString::fromUTF8(ctx, m.str().c_str())->asObject(ctx));
    return mo;
}

static const proto::ProtoObject* py_pattern_sub(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* replObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    
    std::string pat;
    const proto::ProtoObject* patAttr = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"));
    if (patAttr && patAttr->isString(ctx))
        patAttr->asString(ctx)->toUTF8String(ctx, pat);
    else
        return PROTO_NONE;
        
    std::string replStr;
    if (replObj->isString(ctx)) {
        replObj->asString(ctx)->toUTF8String(ctx, replStr);
    } else {
        // Can't handle callable repl right now, just fallback or ignore
        return strObj; 
    }
    
    std::string s;
    if (!strObj->isString(ctx)) return PROTO_NONE;
    strObj->asString(ctx)->toUTF8String(ctx, s);

    std::regex re;
    try {
        re = std::regex(pat);
    } catch (...) {
        return strObj;
    }
    
    std::string res;
    try {
        res = std::regex_replace(s, re, replStr);
    } catch (...) {
        return strObj;
    }

    return proto::ProtoString::fromUTF8(ctx, res.c_str())->asObject(ctx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    const proto::ProtoObject* patternProto = ctx->newObject(false);
    
    const proto::ProtoObject* matchProto = ctx->newObject(false);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), matchProto);
    
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), matchProto);
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "match"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_match));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "search"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_search));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sub"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_sub));
        
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__pattern_proto__"), patternProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "compile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_compile));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "escape"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_escape));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "match"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_match));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "search"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_search));

    // Regex flags
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "IGNORECASE"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "I"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCALE"), ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "L"), ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "MULTILINE"), ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "M"), ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DOTALL"), ctx->fromInteger(16));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "S"), ctx->fromInteger(16));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "UNICODE"), ctx->fromInteger(32));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "U"), ctx->fromInteger(32));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "VERBOSE"), ctx->fromInteger(64));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "X"), ctx->fromInteger(64));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DEBUG"), ctx->fromInteger(128));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ASCII"), ctx->fromInteger(256));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "A"), ctx->fromInteger(256));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "NOFLAG"), ctx->fromInteger(0));

    return mod;
}

} // namespace re
} // namespace protoPython
