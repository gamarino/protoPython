#include <protoPython/JsonModule.h>
#include <sstream>
#include <string>
#include <cctype>

namespace protoPython {
namespace json {

static void jsonSkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

static const proto::ProtoObject* jsonParse(proto::ProtoContext* ctx, const std::string& s, size_t& i);

static const proto::ProtoObject* jsonParse(proto::ProtoContext* ctx, const std::string& s, size_t& i) {
    jsonSkipWs(s, i);
    if (i >= s.size()) return PROTO_NONE;
    if (s.compare(i, 4, "null") == 0) { i += 4; return PROTO_NONE; }
    if (s.compare(i, 4, "true") == 0) { i += 4; return PROTO_TRUE; }
    if (s.compare(i, 5, "false") == 0) { i += 5; return PROTO_FALSE; }
    if (s[i] == '"') {
        ++i;
        std::string t;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\') { ++i; if (i < s.size()) t += s[i++]; }
            else t += s[i++];
        }
        if (i < s.size()) ++i;
        return ctx->fromUTF8String(t.c_str());
    }
    if (s[i] == '[') {
        ++i;
        const proto::ProtoList* list = ctx->newList();
        jsonSkipWs(s, i);
        if (i < s.size() && s[i] == ']') { ++i; return list->asObject(ctx); }
        for (;;) {
            const proto::ProtoObject* v = jsonParse(ctx, s, i);
            if (!v) break;
            list = list->appendLast(ctx, v);
            jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != ',') break;
            ++i;
        }
        jsonSkipWs(s, i);
        if (i < s.size() && s[i] == ']') ++i;
        return list->asObject(ctx);
    }
    if (s[i] == '{') {
        ++i;
        const proto::ProtoList* keys = ctx->newList();
        const proto::ProtoSparseList* data = ctx->newSparseList();
        jsonSkipWs(s, i);
        if (i < s.size() && s[i] == '}') {
            ++i;
            const proto::ProtoObject* emptyObj = ctx->newObject(true);
            emptyObj = emptyObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"), keys->asObject(ctx));
            emptyObj = emptyObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx));
            return emptyObj;
        }
        for (;;) {
            const proto::ProtoObject* k = jsonParse(ctx, s, i);
            if (!k || !k->isString(ctx)) break;
            jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != ':') break;
            ++i;
            const proto::ProtoObject* v = jsonParse(ctx, s, i);
            if (!v) break;
            keys = keys->appendLast(ctx, k);
            data = data->setAt(ctx, k->getHash(ctx), v);
            jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != ',') break;
            ++i;
        }
        jsonSkipWs(s, i);
        if (i < s.size() && s[i] == '}') ++i;
        const proto::ProtoObject* obj = ctx->newObject(true);
        obj = obj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"), keys->asObject(ctx));
        obj = obj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx));
        return obj;
    }
    if (std::isdigit(static_cast<unsigned char>(s[i])) || (s[i] == '-' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i+1])))) {
        size_t j = i;
        if (s[j] == '-') ++j;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if (j < s.size() && s[j] == '.') {
            ++j;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            std::string num = s.substr(i, j - i);
            i = j;
            return ctx->fromDouble(std::stod(num));
        }
        std::string num = s.substr(i, j - i);
        i = j;
        return ctx->fromInteger(std::stoll(num));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_loads(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1 || !posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    std::string s;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);
    size_t i = 0;
    return jsonParse(ctx, s, i);
}

static void dumpValue(proto::ProtoContext* ctx, std::ostream& out, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) { out << "null"; return; }
    if (obj == PROTO_TRUE) { out << "true"; return; }
    if (obj == PROTO_FALSE) { out << "false"; return; }
    if (obj->isInteger(ctx)) { out << obj->asLong(ctx); return; }
    if (obj->isDouble(ctx)) { out << obj->asDouble(ctx); return; }
    if (obj->isString(ctx)) {
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        out << '"';
        for (char c : s) {
            if (c == '"' || c == '\\') out << '\\';
            out << c;
        }
        out << '"';
        return;
    }
    if (obj->asList(ctx)) {
        const proto::ProtoList* list = obj->asList(ctx);
        out << '[';
        for (unsigned long i = 0; i < list->getSize(ctx); ++i) {
            if (i > 0) out << ',';
            dumpValue(ctx, out, list->getAt(ctx, static_cast<int>(i)));
        }
        out << ']';
        return;
    }
    const proto::ProtoObject* keysObj = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"));
    const proto::ProtoObject* dataObj = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    if (keysObj && keysObj->asList(ctx) && dataObj && dataObj->asSparseList(ctx)) {
        const proto::ProtoList* keys = keysObj->asList(ctx);
        const proto::ProtoSparseList* data = dataObj->asSparseList(ctx);
        out << '{';
        for (unsigned long i = 0; i < keys->getSize(ctx); ++i) {
            if (i > 0) out << ',';
            dumpValue(ctx, out, keys->getAt(ctx, static_cast<int>(i)));
            out << ':';
            const proto::ProtoObject* v = data->getAt(ctx, keys->getAt(ctx, static_cast<int>(i))->getHash(ctx));
            dumpValue(ctx, out, v);
        }
        out << '}';
        return;
    }
    out << "null";
}

static const proto::ProtoObject* py_dumps(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return ctx->fromUTF8String("null");
    std::ostringstream out;
    dumpValue(ctx, out, posArgs->getAt(ctx, 0));
    return ctx->fromUTF8String(out.str().c_str());
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "loads"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_loads));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dumps"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_dumps));
    return mod;
}

} // namespace json
} // namespace protoPython
