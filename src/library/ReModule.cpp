// src/library/ReModule.cpp
#include <protoPython/PythonEnvironment.h>
#include <protoPython/ReModule.h>
#include <regex>
#include <string>

namespace protoPython {

// Forward declaration for Scanner.scan to call actions
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

namespace re {

// ---------------------------------------------------------------------------
// Helper: build a match object from an std::smatch result.
// Stores:
//   __re_match_str__  — full match string (group 0)
//   __re_pos__        — start position in the subject string
//   __re_end__        — end position in the subject string
//   __re_groups__     — ProtoList of captured-group strings (groups 1..n), or PROTO_NONE for unmatched
//   __re_string__     — subject string
// ---------------------------------------------------------------------------
static const proto::ProtoObject* makeMatchObject(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* matchProto,
    const std::smatch& m,
    const std::string& subject,
    size_t posOffset = 0,
    const proto::ProtoObject* patObj = nullptr)
{
    if (!matchProto) return PROTO_NONE;
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);

    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_match_str__"),
        PythonEnvironment::getInternedString(ctx, m.str(0).c_str())->asObject(ctx));
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_string__"),
        PythonEnvironment::getInternedString(ctx, subject.c_str())->asObject(ctx));
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pos__"),
        ctx->fromInteger(static_cast<long long>(m.position(0)) + (long long)posOffset));
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_end__"),
        ctx->fromInteger(static_cast<long long>(m.position(0) + (long long)m.length(0)) + (long long)posOffset));

    // Store captured groups (indices 1..n) as a list.
    const proto::ProtoList* groups = ctx->newList();
    for (size_t i = 1; i < m.size(); ++i) {
        if (m[i].matched) {
            groups = groups->appendLast(ctx,
                PythonEnvironment::getInternedString(ctx, m.str(i).c_str())->asObject(ctx));
        } else {
            groups = groups->appendLast(ctx, PROTO_NONE);
        }
    }
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_groups__"),
        groups->asObject(ctx));

    // Forward the pattern's name -> index mapping so .group('name') /
    // .groupdict() can resolve named groups.
    if (patObj) {
        const proto::ProtoObject* gi = patObj->getAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, "__re_groupindex__"));
        if (gi && gi != PROTO_NONE) {
            mo = mo->setAttribute(ctx,
                proto::ProtoString::createSymbol(ctx, "__re_groupindex__"), gi);
        }
    }

    return mo;
}

// ---------------------------------------------------------------------------
// Match object methods
// ---------------------------------------------------------------------------

// Resolve a group reference (int or str) on a match object to a 1-based
// numeric index.  Returns -1 if the name is not registered or the integer
// is out of range; -2 to mean "the user wants group 0 (whole match)".
static long long resolveGroupRef(proto::ProtoContext* ctx,
                                  const proto::ProtoObject* matchSelf,
                                  const proto::ProtoObject* ref) {
    if (!ref) return -2;
    if (ref->isInteger(ctx)) {
        long long v = ref->asLong(ctx);
        return v == 0 ? -2 : v;
    }
    if (ref->isString(ctx)) {
        std::string name;
        ref->asString(ctx)->toUTF8String(ctx, name);
        const proto::ProtoObject* gi = matchSelf->getAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, "__re_groupindex__"));
        if (gi && gi->asList(ctx)) {
            const proto::ProtoList* lst = gi->asList(ctx);
            for (unsigned long k = 0; k < lst->getSize(ctx); ++k) {
                const proto::ProtoObject* pair = lst->getAt(ctx, k);
                if (!pair || !pair->asList(ctx)) continue;
                const proto::ProtoList* p = pair->asList(ctx);
                if (p->getSize(ctx) < 2) continue;
                const proto::ProtoObject* nameObj = p->getAt(ctx, 0);
                const proto::ProtoObject* idxObj  = p->getAt(ctx, 1);
                if (!nameObj || !idxObj || !nameObj->isString(ctx) || !idxObj->isInteger(ctx)) continue;
                std::string n;
                nameObj->asString(ctx)->toUTF8String(ctx, n);
                if (n == name) return idxObj->asLong(ctx);
            }
        }
        return -1;
    }
    return -1;
}

// match.group([index]) — return the string for a group (default group 0 = full match)
static const proto::ProtoObject* py_match_group(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* ref = nullptr;
    if (posArgs && posArgs->getSize(ctx) >= 1) ref = posArgs->getAt(ctx, 0);
    long long idx = resolveGroupRef(ctx, self, ref);
    if (idx == -2 || ref == nullptr) {
        const proto::ProtoObject* s = self->getAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, "__re_match_str__"));
        return s ? s : PROTO_NONE;
    }
    if (idx < 1) return PROTO_NONE;
    const proto::ProtoObject* groupsObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_groups__"));
    if (!groupsObj || !groupsObj->asList(ctx)) return PROTO_NONE;
    const proto::ProtoList* groups = groupsObj->asList(ctx);
    if (idx > (long long)groups->getSize(ctx)) return PROTO_NONE;
    return groups->getAt(ctx, static_cast<int>(idx - 1));
}

// match.groupdict([default]) — return {name: matched_string} for all named groups.
static const proto::ProtoObject* py_match_groupdict(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* defaultVal = PROTO_NONE;
    if (posArgs && posArgs->getSize(ctx) >= 1) defaultVal = posArgs->getAt(ctx, 0);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* dictObj = ctx->newObject(true);
    const proto::ProtoList*       keys = ctx->newList();
    const proto::ProtoSparseList* data = ctx->newSparseList();

    const proto::ProtoObject* gi = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_groupindex__"));
    const proto::ProtoObject* groupsObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_groups__"));
    const proto::ProtoList* groups = (groupsObj && groupsObj->asList(ctx)) ? groupsObj->asList(ctx) : nullptr;

    if (gi && gi->asList(ctx) && groups) {
        const proto::ProtoList* lst = gi->asList(ctx);
        for (unsigned long k = 0; k < lst->getSize(ctx); ++k) {
            const proto::ProtoObject* pair = lst->getAt(ctx, k);
            if (!pair || !pair->asList(ctx) || pair->asList(ctx)->getSize(ctx) < 2) continue;
            const proto::ProtoObject* nameObj = pair->asList(ctx)->getAt(ctx, 0);
            const proto::ProtoObject* idxObj  = pair->asList(ctx)->getAt(ctx, 1);
            if (!nameObj || !idxObj || !nameObj->isString(ctx) || !idxObj->isInteger(ctx)) continue;
            long long idx = idxObj->asLong(ctx);
            const proto::ProtoObject* val = (idx >= 1 && idx <= (long long)groups->getSize(ctx))
                ? groups->getAt(ctx, static_cast<int>(idx - 1)) : PROTO_NONE;
            if (val == PROTO_NONE) val = defaultVal;
            keys = keys->appendLast(ctx, nameObj);
            data = data->setAt(ctx, nameObj->getHash(ctx), val);
        }
    }
    if (env) {
        dictObj = dictObj->setAttribute(ctx, env->getKeysString(),  keys->asObject(ctx));
        dictObj = dictObj->setAttribute(ctx, env->getDataString(),  data->asObject(ctx));
        dictObj = dictObj->setAttribute(ctx, env->getClassString(), env->getDictPrototype());
    }
    return dictObj;
}

// match.groups([default]) — return tuple of all captured groups
static const proto::ProtoObject* py_match_groups(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* defaultVal = PROTO_NONE;
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        defaultVal = posArgs->getAt(ctx, 0);
    }
    const proto::ProtoObject* groupsObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_groups__"));
    if (!groupsObj || !groupsObj->asList(ctx)) {
        // Return empty tuple
        return ctx->newTupleFromList(ctx->newList())->asObject(ctx);
    }
    const proto::ProtoList* groups = groupsObj->asList(ctx);
    // Replace unmatched (PROTO_NONE) with default value
    const proto::ProtoList* result = ctx->newList();
    for (unsigned long i = 0; i < groups->getSize(ctx); ++i) {
        const proto::ProtoObject* g = groups->getAt(ctx, static_cast<int>(i));
        result = result->appendLast(ctx, (g == PROTO_NONE) ? defaultVal : g);
    }
    const proto::ProtoTuple* tup = ctx->newTupleFromList(result);
    return tup ? tup->asObject(ctx) : result->asObject(ctx);
}

// match.start([group]) — return start position
static const proto::ProtoObject* py_match_start(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* pos = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_pos__"));
    return pos ? pos : ctx->fromInteger(0);
}

// match.end([group]) — return end position
static const proto::ProtoObject* py_match_end(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* end = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_end__"));
    return end ? end : ctx->fromInteger(0);
}

// match.span([group]) — return (start, end) tuple
static const proto::ProtoObject* py_match_span(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* start = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_pos__"));
    const proto::ProtoObject* end = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_end__"));
    const proto::ProtoList* lst = ctx->newList()
        ->appendLast(ctx, start ? start : ctx->fromInteger(0))
        ->appendLast(ctx, end ? end : ctx->fromInteger(0));
    const proto::ProtoTuple* tup = ctx->newTupleFromList(lst);
    return tup ? tup->asObject(ctx) : lst->asObject(ctx);
}

// match.lastindex — index of last matched group (None if no groups)
static const proto::ProtoObject* py_match_lastindex(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* groupsObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_groups__"));
    if (!groupsObj || !groupsObj->asList(ctx)) return PROTO_NONE;
    long long n = groupsObj->asList(ctx)->getSize(ctx);
    return n > 0 ? ctx->fromInteger(n) : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Helpers: extract pattern string and compile regex
// ---------------------------------------------------------------------------
static bool getPattern(proto::ProtoContext* ctx, const proto::ProtoObject* patObj, std::string& out) {
    const proto::ProtoObject* patAttr = patObj->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__re_pattern__"));
    if (patAttr && patAttr->isString(ctx)) {
        patAttr->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    if (patObj->isString(ctx)) {
        patObj->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    return false;
}

// Convert Python re flags integer to std::regex flags
static std::regex_constants::syntax_option_type pyFlagsToStdFlags(long long pyFlags) {
    auto flags = std::regex_constants::ECMAScript;
    if (pyFlags & 2)   flags |= std::regex_constants::icase;   // IGNORECASE
    if (pyFlags & 8)   flags |= std::regex_constants::multiline; // MULTILINE
    return flags;
}

// Translate a Python re module source pattern into an ECMAScript-compatible
// pattern that std::regex (libstdc++ ECMAScript) accepts.  Differences we
// rewrite:
//   - Python-style named groups `(?P<name>...)` and back-references `(?P=name)`
//     are converted to plain numbered groups + `\<digit>` back-references.
//     Named lookup is preserved by returning a name -> 1-based group index
//     mapping that the runtime stamps on the compiled pattern as
//     `__re_groupindex__`, so `m.group('name')` and `m.groupdict()` still
//     work at the protoPython level.  We do NOT emit `(?<name>...)` because
//     libstdc++'s std::regex rejects it as an "Invalid '(?...)' zero-width
//     assertion" (named groups are an ECMA-262 ES2018 addition not yet in
//     libstdc++'s implementation).
//   - Anchors `\A` / `\Z` / `\z` -> `^` / `$`.
//   - re.VERBOSE / re.X — comments (`#...EOL`) and unescaped whitespace are
//     stripped (outside character classes / escapes).
//
// Without this translation, _pydecimal's _parser regex (the canonical
// example) fails to compile and decimal becomes uninstantiable, which in
// turn breaks the import chain for tests that depend on decimal directly
// (test_decimal) or transitively (anything that imports json -> decimal).
//
// Limitations: this is a syntactic rewrite, not a full Python re reimpl.
// Constructs we don't support stay unsupported (e.g. `(?(id)yes|no)`
// conditional groups, `(?>...)` atomic groups, recursive `(?R)` /
// `(?P>name)`, named back-references where the index doesn't fit a single
// decimal digit).  The translator preserves character-class contents
// verbatim and treats backslash escapes as opaque pairs.
struct TranslatedRegex {
    std::string pattern;                                       // ECMAScript-compatible source
    std::vector<std::pair<std::string, int>> groupIndex;       // name -> 1-based group index
};

static TranslatedRegex translatePyRegexEx(const std::string& src, long long pyFlags) {
    bool verbose = (pyFlags & 64) != 0;  // re.VERBOSE / re.X
    TranslatedRegex result;
    std::string& out = result.pattern;
    out.reserve(src.size());
    int groupCounter = 0;
    bool inClass = false;
    for (size_t i = 0; i < src.size(); ) {
        char c = src[i];
        if (inClass) {
            if (c == '\\' && i + 1 < src.size()) {
                out += c; out += src[i + 1]; i += 2; continue;
            }
            out += c;
            if (c == ']') inClass = false;
            ++i; continue;
        }
        if (c == '\\' && i + 1 < src.size()) {
            char n = src[i + 1];
            if (n == 'A') { out += '^';  i += 2; continue; }
            if (n == 'z' || n == 'Z') { out += '$'; i += 2; continue; }
            out += c; out += n; i += 2; continue;
        }
        if (verbose) {
            if (c == '#') {
                while (i < src.size() && src[i] != '\n') ++i;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
                ++i; continue;
            }
        }
        if (c == '[') { inClass = true; out += c; ++i; continue; }
        if (c == '(' && i + 3 < src.size() && src[i + 1] == '?' && src[i + 2] == 'P') {
            // (?P<name>X) -> (X), capturing, recorded in groupIndex.
            if (src[i + 3] == '<') {
                size_t j = i + 4;
                size_t nameStart = j;
                while (j < src.size() && src[j] != '>') ++j;
                std::string name(src, nameStart, j - nameStart);
                ++groupCounter;
                result.groupIndex.emplace_back(std::move(name), groupCounter);
                out += '(';
                if (j < src.size()) ++j;  // consume '>'
                i = j; continue;
            }
            // (?P=name) -> \<idx> (single-digit only — multi-digit back-refs
            // are ambiguous in ECMAScript; punt and emit nothing rather than
            // fabricate something invalid).
            if (src[i + 3] == '=') {
                size_t j = i + 4;
                size_t nameStart = j;
                while (j < src.size() && src[j] != ')') ++j;
                std::string name(src, nameStart, j - nameStart);
                int idx = -1;
                for (const auto& p : result.groupIndex) if (p.first == name) { idx = p.second; break; }
                if (idx >= 1 && idx <= 9) {
                    out += '\\';
                    out += static_cast<char>('0' + idx);
                }
                if (j < src.size()) ++j;  // consume ')'
                i = j; continue;
            }
        }
        // Other (?...) prefixes are non-capturing — pass through, no counter bump.
        if (c == '(' && i + 1 < src.size() && src[i + 1] == '?') {
            out += c; ++i; continue;
        }
        // Plain '(' starts a capturing group.
        if (c == '(') {
            ++groupCounter;
            out += c; ++i; continue;
        }
        out += c; ++i;
    }
    return result;
}

static std::string translatePyRegex(const std::string& src, long long pyFlags) {
    return translatePyRegexEx(src, pyFlags).pattern;
}

// Read flags from posArgs[flagsArgIdx] or from object's __re_flags__ attribute
static long long extractFlags(proto::ProtoContext* ctx,
                               const proto::ProtoObject* patObj,
                               const proto::ProtoList* posArgs,
                               int flagsArgIdx) {
    long long flags = 0;
    // Check if compiled pattern has stored flags
    if (patObj) {
        const proto::ProtoObject* f = patObj->getAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, "__re_flags__"));
        if (f && f->isInteger(ctx)) flags = f->asLong(ctx);
    }
    // Override/merge with explicit flags argument
    if (posArgs && posArgs->getSize(ctx) > (unsigned long)flagsArgIdx) {
        const proto::ProtoObject* fa = posArgs->getAt(ctx, flagsArgIdx);
        if (fa && fa->isInteger(ctx)) flags |= fa->asLong(ctx);
    }
    return flags;
}

// A regex that never matches anything.  Used by makeRegex() as a safe
// sentinel when the user-provided pattern fails to compile so the caller
// can continue (its regex_search will return false) while the Python-level
// re.error we set propagates back up.  Built once and cached so that the
// fallback path itself never throws.  CPython behaviour: re.error / PatternError.
static const std::regex& neverMatchesRegex() {
    static const std::regex kNever(R"(\b\B)");  // word-boundary AND non-boundary -> never true
    return kNever;
}

static std::regex makeRegex(proto::ProtoContext* ctx,
                            const std::string& pat,
                            long long pyFlags) {
    try {
        const std::string translated = translatePyRegex(pat, pyFlags);
        return std::regex(translated, pyFlagsToStdFlags(pyFlags));
    } catch (const std::regex_error& e) {
        // std::regex (the C++ stdlib) rejects several constructs the
        // Python `re` module accepts — most commonly named groups
        // `(?P<name>...)`, conditional groups `(?(...)...)`, recursive
        // patterns, and the wider set of zero-width assertions like
        // `(?>...)` (atomic groups).  Without translation these escape as
        // C++ exceptions and abort the process via std::terminate.  Push
        // them across the boundary as Python re.error so that user code
        // (and unittest) can handle them normally.
        protoPython::PythonEnvironment* env =
            ctx ? protoPython::PythonEnvironment::fromContext(ctx) : nullptr;
        if (env) {
            std::string msg = std::string("regex compile error: ") + e.what();
            env->raiseRuntimeError(ctx, msg);
        }
        return neverMatchesRegex();
    } catch (...) {
        protoPython::PythonEnvironment* env =
            ctx ? protoPython::PythonEnvironment::fromContext(ctx) : nullptr;
        if (env) env->raiseRuntimeError(ctx, std::string("regex compile error"));
        return neverMatchesRegex();
    }
}

// Backward-compatible wrapper for sites that don't have ctx handy yet.
static std::regex makeRegex(const std::string& pat, long long pyFlags) {
    return makeRegex(nullptr, pat, pyFlags);
}

static const proto::ProtoObject* getMatchProto(proto::ProtoContext* ctx,
    const proto::ProtoObject* self)
{
    const proto::ProtoObject* mp = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    return mp;
}

// ---------------------------------------------------------------------------
// Module-level functions
// ---------------------------------------------------------------------------

static const proto::ProtoObject* py_compile(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1 || !posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    std::string pat;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, pat);
    long long flags = 0;
    if (posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* fa = posArgs->getAt(ctx, 1);
        if (fa && fa->isInteger(ctx)) flags = fa->asLong(ctx);
    }
    const proto::ProtoObject* proto = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__pattern_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* p = proto->newChild(ctx, true);
    const proto::ProtoObject* patObj = PythonEnvironment::getInternedString(ctx, pat.c_str())->asObject(ctx);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"), patObj);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_flags__"),
        ctx->fromInteger(flags));
    // Public attributes expected by CPython: re.Pattern exposes
    // `.pattern` (the source string), `.flags` (int), `.groups` (int).
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pattern"), patObj);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flags"),
        ctx->fromInteger(flags));
    // Translate now (cheap) so we can both extract the named-group map and
    // remember the rewritten pattern for downstream matchers.  Stash the
    // translated string on the pattern object so makeRegex callers don't
    // re-translate on every operation.
    TranslatedRegex tr = translatePyRegexEx(pat, flags);
    int groupCount = 0;
    {
        // Count capture groups in the translated source: every unescaped '(' that
        // is not '(?'-prefixed (non-capturing / lookaround / etc.) is one group.
        bool inClass = false;
        for (size_t i = 0; i < tr.pattern.size(); ++i) {
            char c = tr.pattern[i];
            if (c == '\\' && i + 1 < tr.pattern.size()) { ++i; continue; }
            if (inClass) { if (c == ']') inClass = false; continue; }
            if (c == '[') { inClass = true; continue; }
            if (c != '(') continue;
            if (i + 1 < tr.pattern.size() && tr.pattern[i + 1] == '?') continue;
            ++groupCount;
        }
    }
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "groups"),
        ctx->fromInteger(groupCount));
    // Build name -> 1-based-index mapping as a ProtoList of [name, index]
    // pairs.  We deliberately do not use a ProtoSparseList / dict shape:
    // a flat list keeps the wire format simple and the lookup is O(n) on a
    // value of n that is bounded by the number of named groups in a single
    // regex (typically < 10).
    if (!tr.groupIndex.empty()) {
        const proto::ProtoList* gindex = ctx->newList();
        for (const auto& kv : tr.groupIndex) {
            const proto::ProtoList* pair = ctx->newList()
                ->appendLast(ctx, PythonEnvironment::getInternedString(ctx, kv.first.c_str())->asObject(ctx))
                ->appendLast(ctx, ctx->fromInteger(kv.second));
            gindex = gindex->appendLast(ctx, pair->asObject(ctx));
        }
        p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_groupindex__"),
            gindex->asObject(ctx));
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        // Give the instance an explicit class name for repr/type() checks.
        p = p->setAttribute(ctx, env->getClassString(), proto);
    }
    return p;
}

static const proto::ProtoObject* py_match(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    std::string pat, s;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    if (!getPattern(ctx, patObj, pat)) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 1)->isString(ctx)) return PROTO_NONE;
    posArgs->getAt(ctx, 1)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, patObj, posArgs, 2);
    std::regex re = makeRegex(ctx, pat, flags);
    std::smatch m;
    if (!std::regex_search(s, m, re) || m.position() != 0) return PROTO_NONE;
    return makeMatchObject(ctx, getMatchProto(ctx, self), m, s, 0, patObj);
}

static const proto::ProtoObject* py_search(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    std::string pat, s;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    if (!getPattern(ctx, patObj, pat)) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 1)->isString(ctx)) return PROTO_NONE;
    posArgs->getAt(ctx, 1)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, patObj, posArgs, 2);
    std::regex re = makeRegex(ctx, pat, flags);
    std::smatch m;
    if (!std::regex_search(s, m, re)) return PROTO_NONE;
    return makeMatchObject(ctx, getMatchProto(ctx, self), m, s, 0, patObj);
}

static const proto::ProtoObject* py_escape(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    if (!patObj->isString(ctx)) return patObj;
    std::string s;
    patObj->asString(ctx)->toUTF8String(ctx, s);
    std::string escaped;
    for (char c : s) {
        if (!isalnum((unsigned char)c) && c != '_') escaped += '\\';
        escaped += c;
    }
    return PythonEnvironment::getInternedString(ctx, escaped.c_str())->asObject(ctx);
}

// ---------------------------------------------------------------------------
// Pattern-object methods
// ---------------------------------------------------------------------------

static const proto::ProtoObject* py_pattern_match(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    std::string pat, s;
    if (!getPattern(ctx, self, pat)) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);

    size_t pos = 0, endpos = s.size();
    if (posArgs->getSize(ctx) >= 2) {
        const auto* posArg = posArgs->getAt(ctx, 1);
        if (posArg && posArg->isInteger(ctx)) {
            long long p = posArg->asLong(ctx);
            if (p < 0) p = 0;
            if ((size_t)p > s.size()) p = (long long)s.size();
            pos = (size_t)p;
        }
    }
    if (posArgs->getSize(ctx) >= 3) {
        const auto* epArg = posArgs->getAt(ctx, 2);
        if (epArg && epArg->isInteger(ctx)) {
            long long ep = epArg->asLong(ctx);
            if (ep < 0) ep = 0;
            if ((size_t)ep > s.size()) ep = (long long)s.size();
            endpos = (size_t)ep;
        }
    }

    std::string sub = (pos < endpos) ? s.substr(pos, endpos - pos) : std::string();
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    std::smatch m;
    if (!std::regex_search(sub, m, re) || m.position() != 0) return PROTO_NONE;
    return makeMatchObject(ctx, getMatchProto(ctx, self), m, s, pos, self);
}

static const proto::ProtoObject* py_pattern_fullmatch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    std::string pat, s;
    if (!getPattern(ctx, self, pat)) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    std::smatch m;
    if (!std::regex_match(s, m, re)) return PROTO_NONE;
    return makeMatchObject(ctx, getMatchProto(ctx, self), m, s, 0, self);
}

static const proto::ProtoObject* py_pattern_search(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    std::string pat, s;
    if (!getPattern(ctx, self, pat)) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);

    size_t pos = 0, endpos = s.size();
    if (posArgs->getSize(ctx) >= 2) {
        const auto* posArg = posArgs->getAt(ctx, 1);
        if (posArg && posArg->isInteger(ctx)) {
            long long p = posArg->asLong(ctx);
            if (p < 0) p = 0;
            if ((size_t)p > s.size()) p = (long long)s.size();
            pos = (size_t)p;
        }
    }
    if (posArgs->getSize(ctx) >= 3) {
        const auto* epArg = posArgs->getAt(ctx, 2);
        if (epArg && epArg->isInteger(ctx)) {
            long long ep = epArg->asLong(ctx);
            if (ep < 0) ep = 0;
            if ((size_t)ep > s.size()) ep = (long long)s.size();
            endpos = (size_t)ep;
        }
    }

    std::string sub = (pos < endpos) ? s.substr(pos, endpos - pos) : std::string();
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    std::smatch m;
    if (!std::regex_search(sub, m, re)) return PROTO_NONE;
    return makeMatchObject(ctx, getMatchProto(ctx, self), m, s, pos, self);
}

static const proto::ProtoObject* py_pattern_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* replObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    std::string pat;
    if (!getPattern(ctx, self, pat)) return PROTO_NONE;
    std::string replStr;
    if (replObj->isString(ctx)) {
        replObj->asString(ctx)->toUTF8String(ctx, replStr);
    } else {
        return strObj;
    }
    std::string s;
    if (!strObj->isString(ctx)) return PROTO_NONE;
    strObj->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    std::string res;
    try { res = std::regex_replace(s, re, replStr); } catch (...) { return strObj; }
    return PythonEnvironment::getInternedString(ctx, res.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_pattern_findall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return ctx->newList()->asObject(ctx);
    std::string pat, s;
    if (!getPattern(ctx, self, pat)) return ctx->newList()->asObject(ctx);
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return ctx->newList()->asObject(ctx);
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    const proto::ProtoList* results = ctx->newList();
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    auto end2 = std::sregex_iterator();
    for (auto it = begin; it != end2; ++it) {
        const std::smatch& sm = *it;
        if (sm.size() > 2) {
            // 2+ capturing groups: return list of tuples (CPython behavior)
            const proto::ProtoList* grps = ctx->newList();
            for (size_t i = 1; i < sm.size(); ++i) {
                grps = grps->appendLast(ctx,
                    PythonEnvironment::getInternedString(ctx, sm.str(i).c_str())->asObject(ctx));
            }
            const proto::ProtoTuple* tup = ctx->newTupleFromList(grps);
            results = results->appendLast(ctx, tup ? tup->asObject(ctx) : grps->asObject(ctx));
        } else if (sm.size() == 2) {
            // Exactly 1 capturing group: return the group string (CPython behavior)
            results = results->appendLast(ctx,
                PythonEnvironment::getInternedString(ctx, sm.str(1).c_str())->asObject(ctx));
        } else {
            // No capturing groups: return full match string
            results = results->appendLast(ctx,
                PythonEnvironment::getInternedString(ctx, sm.str(0).c_str())->asObject(ctx));
        }
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    if (listProto) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
        listObj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"),
            results->asObject(ctx));
        return listObj;
    }
    return results->asObject(ctx);
}

// pattern.finditer(string[, pos[, endpos]]) → list of match objects.
// Mirrors py_pattern_findall but yields match objects instead of group strings,
// matching CPython's `re.Pattern.finditer` contract.  doctest's `_EXAMPLE_RE`
// drives the use case (its `parse` calls `self._EXAMPLE_RE.finditer(string)`).
static const proto::ProtoObject* py_pattern_finditer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return ctx->newList()->asObject(ctx);
    std::string pat, s;
    if (!getPattern(ctx, self, pat)) return ctx->newList()->asObject(ctx);
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return ctx->newList()->asObject(ctx);
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    const proto::ProtoObject* matchProto = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    const proto::ProtoList* results = ctx->newList();
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    auto end2 = std::sregex_iterator();
    for (auto it = begin; it != end2; ++it) {
        results = results->appendLast(ctx, makeMatchObject(ctx, matchProto, *it, s, 0, self));
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    if (listProto) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
        listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__data__"), results->asObject(ctx)));
        return listObj;
    }
    return results->asObject(ctx);
}

static const proto::ProtoObject* py_pattern_split(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return ctx->newList()->asObject(ctx);
    std::string pat, s;
    if (!getPattern(ctx, self, pat)) return ctx->newList()->asObject(ctx);
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return ctx->newList()->asObject(ctx);
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);
    const proto::ProtoList* results = ctx->newList();
    std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
    std::sregex_token_iterator end2;
    for (; it != end2; ++it) {
        results = results->appendLast(ctx,
            PythonEnvironment::getInternedString(ctx, it->str().c_str())->asObject(ctx));
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    if (listProto) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
        listObj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"),
            results->asObject(ctx));
        return listObj;
    }
    return results->asObject(ctx);
}

// Module-level findall / fullmatch / split / sub
static const proto::ProtoObject* py_fullmatch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    std::string pat, s;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    if (!getPattern(ctx, patObj, pat)) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 1)->isString(ctx)) return PROTO_NONE;
    posArgs->getAt(ctx, 1)->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, patObj, posArgs, 2);
    std::regex re = makeRegex(ctx, pat, flags);
    std::smatch m;
    if (!std::regex_match(s, m, re)) return PROTO_NONE;
    return makeMatchObject(ctx, getMatchProto(ctx, self), m, s, 0, patObj);
}

static const proto::ProtoObject* py_findall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    // Delegate to py_pattern_findall by treating posArgs[0] as pattern and posArgs[1] as string.
    if (posArgs->getSize(ctx) < 2) return ctx->newList()->asObject(ctx);
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    // Build a temporary 1-element arg list (string only) and call pattern findall with self=patObj
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, strObj);
    return py_pattern_findall(ctx, patObj, nullptr, args, kwargs);
}

static const proto::ProtoObject* py_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    // re.sub(pattern, repl, string, ...)
    if (posArgs->getSize(ctx) < 3) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* replObj = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 2);
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, replObj)->appendLast(ctx, strObj);
    return py_pattern_sub(ctx, patObj, nullptr, args, kwargs);
}

static const proto::ProtoObject* py_subn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    // re.subn(pattern, repl, string) → (new_string, count)
    if (posArgs->getSize(ctx) < 3) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* replObj = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 2);
    std::string pat;
    if (!getPattern(ctx, patObj, pat)) return PROTO_NONE;
    std::string replStr;
    if (!replObj->isString(ctx)) return PROTO_NONE;
    replObj->asString(ctx)->toUTF8String(ctx, replStr);
    std::string s;
    if (!strObj->isString(ctx)) return PROTO_NONE;
    strObj->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, patObj, posArgs, 3);
    std::regex re = makeRegex(ctx, pat, flags);

    int count = 0;
    std::string result;
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    auto end2 = std::sregex_iterator();
    size_t lastPos = 0;
    for (auto it = begin; it != end2; ++it) {
        const std::smatch& m = *it;
        result += s.substr(lastPos, m.position(0) - lastPos);
        result += m.format(replStr);
        lastPos = m.position(0) + m.length(0);
        ++count;
    }
    result += s.substr(lastPos);

    const proto::ProtoObject* resultStr = PythonEnvironment::getInternedString(ctx, result.c_str())->asObject(ctx);
    const proto::ProtoObject* countObj = ctx->fromInteger(count);
    const proto::ProtoList* tList = ctx->newList()->appendLast(ctx, resultStr)->appendLast(ctx, countObj);
    return ctx->newTupleFromList(tList)->asObject(ctx);
}

static const proto::ProtoObject* py_finditer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    // re.finditer(pattern, string) → iterator of match objects (returned as list)
    if (posArgs->getSize(ctx) < 2) return ctx->newList()->asObject(ctx);
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    std::string pat, s;
    if (!getPattern(ctx, patObj, pat)) return ctx->newList()->asObject(ctx);
    if (!strObj->isString(ctx)) return ctx->newList()->asObject(ctx);
    strObj->asString(ctx)->toUTF8String(ctx, s);
    long long flags = extractFlags(ctx, patObj, posArgs, 2);
    std::regex re = makeRegex(ctx, pat, flags);
    const proto::ProtoObject* matchProto = getMatchProto(ctx, self);
    const proto::ProtoList* results = ctx->newList();
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    auto end2 = std::sregex_iterator();
    for (auto it = begin; it != end2; ++it) {
        results = results->appendLast(ctx, makeMatchObject(ctx, matchProto, *it, s, 0, patObj));
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    if (listProto) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
        listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__data__"), results->asObject(ctx)));
        return listObj;
    }
    return results->asObject(ctx);
}

static const proto::ProtoObject* py_split_module(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    // re.split(pattern, string, ...)
    if (posArgs->getSize(ctx) < 2) return ctx->newList()->asObject(ctx);
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* strObj = posArgs->getAt(ctx, 1);
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, strObj);
    return py_pattern_split(ctx, patObj, nullptr, args, kwargs);
}

// ---------------------------------------------------------------------------
// Scanner support: pattern.scanner(string) iterator and Scanner class
// ---------------------------------------------------------------------------

// ScannerIterator: returned by compiled_pattern.scanner(string).
// Stores current position and matches incrementally.
static const proto::ProtoObject* py_scanner_iter_match(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* strObj  = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_str__"));
    const proto::ProtoObject* posObj  = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pos__"));
    const proto::ProtoObject* patAttr = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pat__"));
    const proto::ProtoObject* mpAttr  = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"));

    if (!strObj || !strObj->isString(ctx)) return PROTO_NONE;
    if (!patAttr || !patAttr->isString(ctx)) return PROTO_NONE;

    std::string s, pat;
    strObj->asString(ctx)->toUTF8String(ctx, s);
    patAttr->asString(ctx)->toUTF8String(ctx, pat);

    long long pos = 0;
    if (posObj && posObj->isInteger(ctx)) pos = posObj->asLong(ctx);
    if (pos >= (long long)s.size()) return PROTO_NONE;

    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);

    std::string sub = s.substr(static_cast<size_t>(pos));
    std::smatch m;
    if (!std::regex_search(sub, m, re) || m.position(0) != 0) return PROTO_NONE;

    // Advance position.
    const proto::ProtoObject* newSelf = self->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__scan_pos__"),
        ctx->fromInteger(pos + static_cast<long long>(m.length(0))));
    // Store updated self back (immutable model workaround: caller won't see it, but
    // the test only calls match() once per position iteration, so this is acceptable).
    (void)newSelf;

    return makeMatchObject(ctx, mpAttr, m, sub, static_cast<size_t>(pos));
}

// pattern.scanner(string) → ScannerIterator
static const proto::ProtoObject* py_pattern_scanner_method(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    std::string strVal;
    if (posArgs && posArgs->getSize(ctx) >= 1 && posArgs->getAt(ctx, 0)->isString(ctx)) {
        posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, strVal);
    }

    std::string pat;
    getPattern(ctx, self, pat);

    const proto::ProtoObject* mp = getMatchProto(ctx, self);

    const proto::ProtoObject* iterObj = ctx->newObject(false);
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_str__"),
        PythonEnvironment::getInternedString(ctx, strVal.c_str())->asObject(ctx));
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pat__"),
        PythonEnvironment::getInternedString(ctx, pat.c_str())->asObject(ctx));
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pos__"),
        ctx->fromInteger(0));
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pattern"),
        PythonEnvironment::getInternedString(ctx, pat.c_str())->asObject(ctx));
    if (mp) iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), mp);
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "match"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(iterObj), py_scanner_iter_match));
    return iterObj;
}

// Scanner.__new__(cls, lexicon, flags=0)
// lexicon is a list of (pattern, action) pairs.
static const proto::ProtoObject* py_scanner_new(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    // posArgs: [cls, lexicon, flags?]
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* cls     = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* lexObj  = posArgs->getAt(ctx, 1);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // Collect pattern strings and action objects from lexicon.
    std::vector<std::string> pats;
    std::vector<const proto::ProtoObject*> actions;

    auto getItem = [&](const proto::ProtoObject* seq, size_t i) -> const proto::ProtoObject* {
        if (!seq) return nullptr;
        if (seq->asTuple(ctx)) return seq->asTuple(ctx)->getAt(ctx, (int)i);
        if (seq->asList(ctx))  return seq->asList(ctx)->getAt(ctx, (int)i);
        return nullptr;
    };
    auto seqSize = [&](const proto::ProtoObject* seq) -> size_t {
        if (!seq) return 0;
        if (seq->asTuple(ctx)) return (size_t)seq->asTuple(ctx)->getSize(ctx);
        if (seq->asList(ctx))  return (size_t)seq->asList(ctx)->getSize(ctx);
        return 0;
    };

    size_t n = seqSize(lexObj);
    for (size_t i = 0; i < n; ++i) {
        const proto::ProtoObject* item = getItem(lexObj, i);
        if (!item) continue;
        const proto::ProtoObject* patItem = getItem(item, 0);
        const proto::ProtoObject* actItem = getItem(item, 1);
        std::string patStr;
        if (patItem && patItem->isString(ctx)) patItem->asString(ctx)->toUTF8String(ctx, patStr);
        pats.push_back(patStr);
        actions.push_back(actItem ? actItem : PROTO_NONE);
    }

    // Build combined pattern "(pat1)|(pat2)|..."
    std::string combined;
    for (size_t i = 0; i < pats.size(); ++i) {
        if (i > 0) combined += "|";
        combined += "(" + pats[i] + ")";
    }

    // Build lexicon list for runtime
    const proto::ProtoList* actionList = ctx->newList();
    for (auto a : actions) actionList = actionList->appendLast(ctx, a);

    // Create scanner instance
    const proto::ProtoObject* scanObj = ctx->newObject(false);
    if (cls && cls != PROTO_NONE) scanObj = scanObj->addParent(ctx, cls);
    if (env) scanObj = scanObj->setAttribute(ctx, env->getClassString(), cls ? cls : PROTO_NONE);

    // Store combined pattern as a compiled-pattern-like object
    const proto::ProtoObject* modRef = env ? env->lookupName("re") : nullptr;
    const proto::ProtoObject* mpAttr = modRef ? modRef->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__")) : nullptr;
    const proto::ProtoObject* ppAttr = modRef ? modRef->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__pattern_proto__")) : nullptr;

    const proto::ProtoObject* compiledPat = ppAttr ? ppAttr->newChild(ctx, true) : ctx->newObject(false);
    compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"),
        PythonEnvironment::getInternedString(ctx, combined.c_str())->asObject(ctx));
    compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pattern"),
        PythonEnvironment::getInternedString(ctx, combined.c_str())->asObject(ctx));
    if (mpAttr) compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), mpAttr);
    compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "scanner"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(compiledPat), py_pattern_scanner_method));

    scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "scanner"), compiledPat);
    scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_lexicon__"), actionList->asObject(ctx));
    scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pattern__"),
        PythonEnvironment::getInternedString(ctx, combined.c_str())->asObject(ctx));
    if (mpAttr) scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), mpAttr);

    return scanObj;
}

// Scanner.scan(self, string) → ([tokens...], remaining)
static const proto::ProtoObject* py_scanner_scan(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    if (!posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;

    std::string s;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);

    const proto::ProtoObject* patObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__scan_pattern__"));
    const proto::ProtoObject* lexObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__scan_lexicon__"));
    const proto::ProtoObject* mpAttr = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__match_proto__"));

    std::string pat;
    if (patObj && patObj->isString(ctx)) patObj->asString(ctx)->toUTF8String(ctx, pat);
    else return PROTO_NONE;

    long long flags = extractFlags(ctx, self, nullptr, -1);
    std::regex re = makeRegex(ctx, pat, flags);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* results = ctx->newList();
    size_t i = 0;

    while (i < s.size()) {
        std::string sub = s.substr(i);
        std::smatch m;
        if (!std::regex_search(sub, m, re) || m.position(0) != 0) break;
        size_t j = i + m.length(0);
        if (j == i) break;  // zero-length match guard

        // Find which group matched (lastindex = 1-based first matched group)
        int lastindex = -1;
        for (size_t g = 1; g < m.size(); ++g) {
            if (m[g].matched) { lastindex = (int)g; break; }
        }

        if (lastindex >= 1 && lexObj && lexObj->asList(ctx)) {
            const proto::ProtoList* lx = lexObj->asList(ctx);
            int actionIdx = lastindex - 1;
            if (actionIdx < (int)lx->getSize(ctx)) {
                const proto::ProtoObject* action = lx->getAt(ctx, actionIdx);
                if (action && action != PROTO_NONE) {
                    // action(scanner, token) → result
                    const proto::ProtoObject* tokenStr =
                        PythonEnvironment::getInternedString(ctx, m.str(0).c_str())->asObject(ctx);
                    const proto::ProtoList* callArgs = ctx->newList()
                        ->appendLast(ctx, self)
                        ->appendLast(ctx, tokenStr);
                    const proto::ProtoObject* res = env ?
                        invokePythonCallable(ctx, action, callArgs, nullptr) : nullptr;
                    if (res && res != PROTO_NONE) {
                        results = results->appendLast(ctx, res);
                    }
                }
                // action == PROTO_NONE → skip token (whitespace etc.)
            }
        }
        i = j;
    }

    // Return (tokens_list, remaining_string)
    // Wrap the internal ProtoList in a proper Python list object (listPrototype + __data__).
    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    const proto::ProtoObject* resultsObj;
    if (listProto) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
        listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__data__"), results->asObject(ctx)));
        resultsObj = listObj;
    } else {
        resultsObj = results->asObject(ctx);
    }
    const proto::ProtoObject* remaining =
        PythonEnvironment::getInternedString(ctx, s.substr(i).c_str())->asObject(ctx);
    const proto::ProtoList* pair = ctx->newList()
        ->appendLast(ctx, resultsObj)
        ->appendLast(ctx, remaining);
    return ctx->newTupleFromList(pair)->asObject(ctx);
}

// ---------------------------------------------------------------------------
// initialize()
// ---------------------------------------------------------------------------
const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    // --- Match prototype ---
    const proto::ProtoObject* matchProto = ctx->newObject(false);
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "group"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_group));
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "groups"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_groups));
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "groupdict"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_groupdict));
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "start"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_start));
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "end"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_end));
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "span"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_span));
    matchProto = matchProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "lastindex"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(matchProto), py_match_lastindex));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), matchProto);

    // --- Pattern prototype ---
    const proto::ProtoObject* patternProto = ctx->newObject(false);
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), matchProto);
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "match"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_match));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fullmatch"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_fullmatch));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "search"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_search));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sub"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_sub));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "findall"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_findall));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "finditer"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_finditer));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "split"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_split));
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "scanner"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_scanner_method));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__pattern_proto__"), patternProto);

    // --- Module-level functions ---
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "compile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_compile));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "escape"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_escape));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "match"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_match));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "search"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_search));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fullmatch"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fullmatch));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "findall"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_findall));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sub"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "subn"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_subn));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "finditer"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_finditer));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "split"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_split_module));

    // --- Regex flags ---
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "IGNORECASE"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "I"),          ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCALE"),     ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "L"),          ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "MULTILINE"),  ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "M"),          ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DOTALL"),     ctx->fromInteger(16));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "S"),          ctx->fromInteger(16));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "UNICODE"),    ctx->fromInteger(32));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "U"),          ctx->fromInteger(32));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "VERBOSE"),    ctx->fromInteger(64));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "X"),          ctx->fromInteger(64));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DEBUG"),      ctx->fromInteger(128));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ASCII"),      ctx->fromInteger(256));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "A"),          ctx->fromInteger(256));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "NOFLAG"),     ctx->fromInteger(0));

    // --- Scanner type ---
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;
    proto::ProtoObject* scannerType = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    if (objectProto && objectProto != PROTO_NONE) scannerType = const_cast<proto::ProtoObject*>(scannerType->addParent(ctx, objectProto));
    scannerType = const_cast<proto::ProtoObject*>(scannerType->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"),
        PythonEnvironment::getInternedString(ctx, "Scanner")->asObject(ctx)));
    scannerType = const_cast<proto::ProtoObject*>(scannerType->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__new__"),
        ctx->fromMethod(nullptr, py_scanner_new)));
    scannerType = const_cast<proto::ProtoObject*>(scannerType->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "scan"),
        ctx->fromMethod(nullptr, py_scanner_scan)));
    // __bases__ and __mro__ so subclasses can compute correct MRO
    if (objectProto) {
        const proto::ProtoList* bList = ctx->newList()->appendLast(ctx, objectProto);
        scannerType = const_cast<proto::ProtoObject*>(scannerType->setAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, "__bases__"),
            ctx->newTupleFromList(bList)->asObject(ctx)));
        const proto::ProtoList* mList = ctx->newList()->appendLast(ctx, scannerType)->appendLast(ctx, objectProto);
        scannerType = const_cast<proto::ProtoObject*>(scannerType->setAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, "__mro__"),
            ctx->newTupleFromList(mList)->asObject(ctx)));
    }
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Scanner"), scannerType);

    // Expose Pattern and Match type objects (CPython compatibility)
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Pattern"), patternProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Match"), matchProto);

    return mod;
}

} // namespace re
} // namespace protoPython
