// StringModule.cpp — Native implementation of CPython's _string C extension.
//
// Provides the two functions used by string.Formatter:
//   formatter_parser(format_string)      → iterator of (literal, field_name, format_spec, conversion)
//   formatter_field_name_split(field_name) → (first, rest_iterator)

#include <protoCore.h>
#include <protoPython/PythonEnvironment.h>
#include <string>
#include <vector>

namespace protoPython {
namespace string_module {

// ---------------------------------------------------------------------------
// formatter_parser — parse a PEP-3101 format string.
//
// Yields tuples (literal_text, field_name, format_spec, conversion) where
// conversion is None or a one-character string ('r', 's', 'a').
// ---------------------------------------------------------------------------

struct ParsedField {
    std::string literal;
    std::string field_name;     // empty → no replacement field
    std::string format_spec;
    std::string conversion;     // empty → None
    bool has_field;
};

static std::vector<ParsedField> parse_format_string(const std::string& fmt) {
    std::vector<ParsedField> result;
    size_t n = fmt.size();
    size_t pos = 0;

    while (pos <= n) {
        ParsedField f{};
        // Collect literal text up to next '{' or end of string
        std::string lit;
        while (pos < n) {
            char c = fmt[pos];
            if (c == '{') {
                if (pos + 1 < n && fmt[pos + 1] == '{') { lit += '{'; pos += 2; continue; }
                break;
            }
            if (c == '}') {
                if (pos + 1 < n && fmt[pos + 1] == '}') { lit += '}'; pos += 2; continue; }
                break; // unmatched '}' treated as literal in CPython (actually raises)
            }
            lit += c; ++pos;
        }
        f.literal = lit;

        if (pos >= n) {
            // End of string — emit final literal (may be empty)
            f.has_field = false;
            result.push_back(f);
            break;
        }

        if (fmt[pos] == '{') {
            ++pos; // skip '{'
            f.has_field = true;
            // Collect field_name (and nested format_spec) up to matching '}'
            // We need balanced braces because format_spec can contain nested fields.
            std::string field_and_spec;
            int depth = 1;
            while (pos < n && depth > 0) {
                char c = fmt[pos];
                if (c == '{') ++depth;
                if (c == '}') { --depth; if (depth == 0) { ++pos; break; } }
                field_and_spec += c;
                ++pos;
            }
            // Split at '!' for conversion, then ':' for format_spec
            // Conversion: field_name!r, field_name!s, field_name!a
            size_t colon = field_and_spec.find(':');
            size_t bang   = field_and_spec.find('!');
            if (bang != std::string::npos && (colon == std::string::npos || bang < colon)) {
                f.field_name  = field_and_spec.substr(0, bang);
                f.conversion  = field_and_spec.substr(bang + 1, 1);
                f.format_spec = (colon != std::string::npos && colon > bang)
                                ? field_and_spec.substr(bang + 2)
                                : "";
            } else if (colon != std::string::npos) {
                f.field_name  = field_and_spec.substr(0, colon);
                f.format_spec = field_and_spec.substr(colon + 1);
            } else {
                f.field_name  = field_and_spec;
                f.format_spec = "";
            }
            result.push_back(f);
        } else {
            // Unmatched '}' at pos — emit as literal
            f.has_field = false;
            result.push_back(f);
            ++pos;
        }
    }
    return result;
}

static const proto::ProtoObject* py_formatter_parser(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return PythonEnvironment::wrapList(ctx, ctx->newList());
    const proto::ProtoObject* fmtObj = posArgs->getAt(ctx, 0);
    if (!fmtObj || !fmtObj->isString(ctx)) return PythonEnvironment::wrapList(ctx, ctx->newList());

    std::string fmt;
    fmtObj->asString(ctx)->toUTF8String(ctx, fmt);

    auto fields = parse_format_string(fmt);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* result = ctx->newList();

    for (auto& f : fields) {
        // Build a 4-tuple: (literal, field_name|None, format_spec|None, conversion|None)
        const proto::ProtoObject* lit = PythonEnvironment::getInternedString(ctx, f.literal.c_str())->asObject(ctx);
        const proto::ProtoObject* field = f.has_field
            ? PythonEnvironment::getInternedString(ctx, f.field_name.c_str())->asObject(ctx)
            : PROTO_NONE;
        const proto::ProtoObject* spec = (f.has_field && !f.format_spec.empty())
            ? PythonEnvironment::getInternedString(ctx, f.format_spec.c_str())->asObject(ctx)
            : (f.has_field ? PythonEnvironment::getInternedString(ctx, "")->asObject(ctx) : PROTO_NONE);
        const proto::ProtoObject* conv = (!f.conversion.empty())
            ? PythonEnvironment::getInternedString(ctx, f.conversion.c_str())->asObject(ctx)
            : PROTO_NONE;

        const proto::ProtoList* tup = ctx->newList();
        tup = tup->appendLast(ctx, lit);
        tup = tup->appendLast(ctx, field);
        tup = tup->appendLast(ctx, spec);
        tup = tup->appendLast(ctx, conv);

        // Wrap as a Python tuple object
        const proto::ProtoObject* tupObj = ctx->newTupleFromList(tup)->asObject(ctx);
        if (env && env->getTuplePrototype()) {
            proto::ProtoObject* t = const_cast<proto::ProtoObject*>(ctx->newObject(false));
            t = const_cast<proto::ProtoObject*>(t->addParent(ctx, env->getTuplePrototype()));
            t = const_cast<proto::ProtoObject*>(t->setAttribute(ctx, env->getDataString(), tupObj));
            tupObj = t;
        }

        result = result->appendLast(ctx, tupObj);
    }

    // Return as an iterator (list) — CPython returns an iterator, but a list works for callers
    proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    if (env && env->getListPrototype()) {
        listObj = const_cast<proto::ProtoObject*>(listObj->addParent(ctx, env->getListPrototype()));
    }
    listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__"),
        result->asObject(ctx)));
    return listObj;
}

// ---------------------------------------------------------------------------
// formatter_field_name_split — split a field name into (first, rest_iter).
//
// "first" is an integer (if purely numeric) or a string.
// "rest" is an iterator over (is_attr, key) pairs.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* py_formatter_field_name_split(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* fnObj = posArgs->getAt(ctx, 0);
    if (!fnObj || !fnObj->isString(ctx)) return PROTO_NONE;

    std::string field_name;
    fnObj->asString(ctx)->toUTF8String(ctx, field_name);

    // Split at first '.' or '['
    size_t dot = field_name.find('.');
    size_t bracket = field_name.find('[');
    size_t split = std::min(dot, bracket);

    std::string first_str = (split != std::string::npos) ? field_name.substr(0, split) : field_name;
    std::string rest_str  = (split != std::string::npos) ? field_name.substr(split)    : "";

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // Determine if first is an integer index
    const proto::ProtoObject* first;
    bool is_int = !first_str.empty();
    for (char c : first_str) if (!isdigit(c)) { is_int = false; break; }
    if (is_int && !first_str.empty()) {
        first = ctx->fromInteger(std::stoll(first_str));
    } else {
        first = PythonEnvironment::getInternedString(ctx, first_str.c_str())->asObject(ctx);
    }

    // Build rest as a list of (is_attr, key) tuples
    const proto::ProtoList* rest_parts = ctx->newList();
    size_t pos = 0;
    const std::string& r = rest_str;
    while (pos < r.size()) {
        if (r[pos] == '.') {
            ++pos;
            size_t end = pos;
            while (end < r.size() && r[end] != '.' && r[end] != '[') ++end;
            std::string attr = r.substr(pos, end - pos);
            pos = end;
            // (True, attr_name)
            const proto::ProtoList* pair = ctx->newList();
            pair = pair->appendLast(ctx, PROTO_TRUE);
            pair = pair->appendLast(ctx, PythonEnvironment::getInternedString(ctx, attr.c_str())->asObject(ctx));
            rest_parts = rest_parts->appendLast(ctx, ctx->newTupleFromList(pair)->asObject(ctx));
        } else if (r[pos] == '[') {
            ++pos;
            size_t end = r.find(']', pos);
            if (end == std::string::npos) end = r.size();
            std::string key_str = r.substr(pos, end - pos);
            pos = end + 1;
            // Determine if key is integer
            bool key_is_int = !key_str.empty();
            for (char c : key_str) if (!isdigit(c)) { key_is_int = false; break; }
            const proto::ProtoObject* key = key_is_int
                ? ctx->fromInteger(std::stoll(key_str))
                : PythonEnvironment::getInternedString(ctx, key_str.c_str())->asObject(ctx);
            // (False, key)
            const proto::ProtoList* pair = ctx->newList();
            pair = pair->appendLast(ctx, PROTO_FALSE);
            pair = pair->appendLast(ctx, key);
            rest_parts = rest_parts->appendLast(ctx, ctx->newTupleFromList(pair)->asObject(ctx));
        } else {
            ++pos;
        }
    }

    // Wrap rest_parts into a list object (iterator)
    proto::ProtoObject* restListObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    if (env && env->getListPrototype()) {
        restListObj = const_cast<proto::ProtoObject*>(restListObj->addParent(ctx, env->getListPrototype()));
    }
    restListObj = const_cast<proto::ProtoObject*>(restListObj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__"),
        rest_parts->asObject(ctx)));

    // Return (first, rest_iterator) as a 2-tuple
    const proto::ProtoList* result_pair = ctx->newList();
    result_pair = result_pair->appendLast(ctx, first);
    result_pair = result_pair->appendLast(ctx, restListObj);
    return ctx->newTupleFromList(result_pair)->asObject(ctx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "formatter_parser"),
        ctx->fromMethod(mod, py_formatter_parser)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "formatter_field_name_split"),
        ctx->fromMethod(mod, py_formatter_field_name_split)));
    return mod;
}

} // namespace string_module
} // namespace protoPython
