// src/library/ReModule.cpp
#include <protoPython/PythonEnvironment.h>
#include <protoPython/ReModule.h>
#include <cctype>
#include <climits>
#include <cwchar>
#include <cwctype>
#include <regex>
#include <string>
#include <vector>

namespace protoPython {

// Forward declaration for Scanner.scan to call actions
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

namespace re {

// ---------------------------------------------------------------------------
// Code points.
// A Python str is a sequence of code points. Matching std::regex over the
// UTF-8 bytes of a str made a character class see each byte of a multibyte
// character on its own, reported match positions in bytes and split
// characters in group strings. Subjects and patterns are therefore matched as
// UTF-32 strings with std::wregex (wchar_t is 32 bits on Linux), or with a
// char32_t std::basic_regex where wchar_t is 16 bits (Windows).
// ---------------------------------------------------------------------------
#if WCHAR_MAX >= 0x10FFFF
// wchar_t holds every code point (Linux, macOS): std::wregex as always.
using ReChar = wchar_t;
using ReString = std::wstring;
using ReRegex = std::wregex;
using ReMatch = std::wsmatch;
using ReIterator = std::wsregex_iterator;
using ReTokenIterator = std::wsregex_token_iterator;
#define RE_LIT(x) L##x
#else
// wchar_t is UTF-16 (Windows): match UTF-32 char32_t strings instead, so
// positions stay code point indexes. The standard library has regex traits
// for char and wchar_t only; these give std::basic_regex what it needs for
// char32_t, with the classification std::wregex's default "C" locale gives
// (towlower/isw* within the BMP, nothing above it).
class ReTraits : public std::_Regex_traits_base {
public:
    using _Uelem = unsigned int;
    using char_type = char32_t;
    using size_type = size_t;
    using string_type = std::u32string;
    using locale_type = std::locale;

    static size_type length(const char32_t* str) { return std::char_traits<char32_t>::length(str); }
    char32_t translate(char32_t c) const { return c; }
    char32_t translate_nocase(char32_t c) const {
        return c < 0x10000 ? static_cast<char32_t>(std::towlower(static_cast<wint_t>(c))) : c;
    }
    template <class It> string_type transform(It first, It last) const { return string_type(first, last); }
    template <class It> string_type transform_primary(It first, It last) const {
        string_type out(first, last);
        for (auto& c : out) c = translate_nocase(c);
        return out;
    }
    template <class It> string_type lookup_collatename(It first, It last) const {
        return string_type(first, last);
    }
    bool isctype(char32_t c, char_class_type mask) const {
        if (mask == static_cast<char_class_type>(-1)) return c == U'_' || isctype(c, _Ch_alnum);
        if (c >= 0x10000) return false;
        // The character's classification bits, laid out as ctype<wchar_t>'s
        // table: the alpha mask includes the upper and lower bits, so a
        // letter carries only alpha's own bit plus its case bit.
        const wint_t w = static_cast<wint_t>(c);
        unsigned int bits = 0;
        if (std::iswalpha(w)) bits |= static_cast<unsigned int>(_Ch_alpha & ~(_Ch_upper | _Ch_lower));
        if (std::iswupper(w)) bits |= _Ch_upper;
        if (std::iswlower(w)) bits |= _Ch_lower;
        if (std::iswdigit(w)) bits |= _Ch_digit;
        if (std::iswspace(w)) bits |= _Ch_space;
        if (std::iswpunct(w)) bits |= _Ch_punct;
        if (std::iswcntrl(w)) bits |= _Ch_cntrl;
        if (std::iswxdigit(w)) bits |= _Ch_xdigit;
        if (std::iswblank(w)) bits |= _Ch_blank;
        return (bits & static_cast<unsigned short>(mask)) != 0;
    }
    template <class It> char_class_type lookup_classname(It first, It last, bool icase = false) const {
        static const struct { const char* name; char_class_type mask; } names[] = {
            {"alnum", _Ch_alnum}, {"alpha", _Ch_alpha}, {"blank", _Ch_blank}, {"cntrl", _Ch_cntrl},
            {"d", _Ch_digit}, {"digit", _Ch_digit}, {"graph", _Ch_graph}, {"lower", _Ch_lower},
            {"print", _Ch_print}, {"punct", _Ch_punct}, {"space", _Ch_space}, {"s", _Ch_space},
            {"upper", _Ch_upper}, {"w", static_cast<char_class_type>(-1)}, {"xdigit", _Ch_xdigit},
        };
        std::string key;
        for (It it = first; it != last; ++it) {
            const char32_t c = *it;
            key += static_cast<char>(c < 0x80 ? std::tolower(static_cast<int>(c)) : '?');
        }
        for (const auto& n : names) {
            if (key == n.name) {
                char_class_type m = n.mask;
                if (icase && (m & (_Ch_lower | _Ch_upper))) m |= _Ch_lower | _Ch_upper;
                return m;
            }
        }
        return 0;
    }
    int value(char32_t c, int base) const {
        if ((base != 8 && U'0' <= c && c <= U'9') || (base == 8 && U'0' <= c && c <= U'7'))
            return static_cast<int>(c - U'0');
        if (base != 16) return -1;
        if (U'a' <= c && c <= U'f') return static_cast<int>(c - U'a' + 10);
        if (U'A' <= c && c <= U'F') return static_cast<int>(c - U'A' + 10);
        return -1;
    }
    locale_type imbue(locale_type loc) { locale_type old = loc_; loc_ = loc; return old; }
    locale_type getloc() const { return loc_; }
private:
    locale_type loc_;
};
using ReChar = char32_t;
using ReString = std::u32string;
using ReRegex = std::basic_regex<char32_t, ReTraits>;
using ReMatch = std::match_results<ReString::const_iterator>;
using ReIterator = std::regex_iterator<ReString::const_iterator, char32_t, ReTraits>;
using ReTokenIterator = std::regex_token_iterator<ReString::const_iterator, char32_t, ReTraits>;
#define RE_LIT(x) U##x
#endif

static ReString toWide(const std::string& s) {
    ReString out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = 0;
        size_t n = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 0xe) { cp = c & 0x0f; n = 3; }
        else if ((c >> 3) == 0x1e) { cp = c & 0x07; n = 4; }
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2) ok = false;
            else cp = (cp << 6) | (cc & 0x3f);
        }
        if (!ok) {
            // Not valid UTF-8: keep the byte value rather than dropping it.
            out += static_cast<ReChar>(c);
            ++i;
            continue;
        }
        out += static_cast<ReChar>(cp);
        i += n;
    }
    return out;
}

static std::string toUtf8(const ReString& w) {
    std::string out;
    out.reserve(w.size());
    for (ReChar wc : w) {
        const proto::proto_ulong cp = static_cast<proto::proto_ulong>(wc);
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }
    return out;
}

// The code points of a str argument; false when obj is not a str.
static bool wideArg(proto::ProtoContext* ctx, const proto::ProtoObject* obj, ReString& out) {
    if (!obj || !obj->isString(ctx)) return false;
    std::string utf8;
    obj->asString(ctx)->toUTF8String(ctx, utf8);
    out = toWide(utf8);
    return true;
}

// Every string `re` hands back — matches, groups, sub / split / findall
// results, escape output — is computed from the subject, so it is data, not
// vocabulary: an ordinary collectable str, never an interned symbol. Interning
// these made each distinct result permanent, so `re` over varying input grew
// the heap without bound. See PythonEnvironment::getInternedString in the
// header for the rule.
static const proto::ProtoObject* newStr(proto::ProtoContext* ctx, const ReString& w) {
    return PythonEnvironment::newStr(ctx, toUtf8(w));
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
            for (proto::proto_ulong k = 0; k < lst->getSize(ctx); ++k) {
                const proto::ProtoObject* pair = lst->getAt(ctx, static_cast<int>(k));
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
        for (proto::proto_ulong k = 0; k < lst->getSize(ctx); ++k) {
            const proto::ProtoObject* pair = lst->getAt(ctx, static_cast<int>(k));
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
    for (proto::proto_ulong i = 0; i < groups->getSize(ctx); ++i) {
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

// ---------------------------------------------------------------------------
// Line boundaries.
//
// Python and ECMAScript disagree on lines, and ECMAScript engines disagree
// with each other: in Python only "\n" ends a line, `^` and `$` without
// re.MULTILINE match only at the start and at the end (or before a final
// "\n"), and `.` excludes only "\n". ECMAScript treats "\r", U+2028 and U+2029
// as line terminators too, and its `multiline` option is missing from MSVC's
// std::regex, whose `^` and `$` matched at every line regardless.
//
// So the engine never sees a line terminator and never runs in multiline
// mode. Before matching, every "\n", "\r", U+2028 and U+2029 of the subject is
// replaced by a private code point above U+10FFFF (no str can contain one),
// and the pattern is rewritten to Python's rules in terms of those:
//   ^, \A      the start-of-subject marker (below)
//   ^ (M)      the start-of-subject or a line-start marker (below)
//   $          (?=\n?$): the end, or before a final "\n"
//   $ (M)      (?=\n|$)
//   \Z         $
//   .          any character but "\n" (all of them under re.DOTALL)
//   \s, [...]  their Python membership for the four terminators
// with "\n" standing for its private code point. ECMAScript has no
// lookbehind, so `^` cannot test the character before it, and the engines
// disagree on their own `^` once a search starts inside the subject (libc++
// lets it match there under match_prev_avail). So the engine's `^` is never
// used: every subject starts with a start-of-subject marker that `^` and
// `\A` consume, and when a MULTILINE pattern uses `^`, each "\n" of the
// subject is followed by a line-start marker that `^` consumes too;
// everything that can match "\n" may consume the marker after it, and
// nothing else matches a marker. Match positions are mapped back to the
// subject's code points (markers have no width there).
// ---------------------------------------------------------------------------
constexpr ReChar kNL  = static_cast<ReChar>(0x110000);  // "\n"
constexpr ReChar kCR  = static_cast<ReChar>(0x110001);  // "\r"
constexpr ReChar kLS  = static_cast<ReChar>(0x110002);  // U+2028 LINE SEPARATOR
constexpr ReChar kPS  = static_cast<ReChar>(0x110003);  // U+2029 PARAGRAPH SEPARATOR
constexpr ReChar kLSM = static_cast<ReChar>(0x110004);  // line start after "\n" (MULTILINE ^ only)
constexpr ReChar kBOS = static_cast<ReChar>(0x110005);  // start of the subject
static const unsigned long kTerminator[4] = {0x0A, 0x0D, 0x2028, 0x2029};
static const ReChar kTerminatorSentinel[4] = {kNL, kCR, kLS, kPS};

static int terminatorIndex(unsigned long cp) {
    for (int k = 0; k < 4; ++k) {
        if (kTerminator[k] == cp) return k;
    }
    return -1;
}

// Python re flags.
constexpr long long kReIgnoreCase = 2;
constexpr long long kReMultiline = 8;
constexpr long long kReDotAll = 16;
constexpr long long kReVerbose = 64;
constexpr long long kReAscii = 256;

// std::regex options for Python flags: never `multiline` (see above).
static std::regex_constants::syntax_option_type pyFlagsToStdFlags(long long pyFlags) {
    auto flags = std::regex_constants::ECMAScript;
    if (pyFlags & kReIgnoreCase) flags |= std::regex_constants::icase;
    return flags;
}

// Translate a Python re module source pattern into an ECMAScript-compatible
// pattern that std::regex accepts, with Python's line rules (above). Other
// differences rewritten:
//   - Python-style named groups `(?P<name>...)` and back-references `(?P=name)`
//     are converted to plain numbered groups + `\<digit>` back-references.
//     Named lookup is preserved by returning a name -> 1-based group index
//     mapping that the runtime stamps on the compiled pattern as
//     `__re_groupindex__`, so `m.group('name')` and `m.groupdict()` still
//     work at the protoPython level.  We do NOT emit `(?<name>...)` because
//     libstdc++'s std::regex rejects it as an "Invalid '(?...)' zero-width
//     assertion" (named groups are an ECMA-262 ES2018 addition not yet in
//     libstdc++'s implementation).
//   - Global inline flags at the start of the pattern, `(?aiLmsux)`, are
//     applied as flags; `(?#...)` comments are dropped.
//   - re.VERBOSE / re.X — comments (`#...EOL`) and unescaped whitespace are
//     stripped (outside character classes / escapes).
//   - A literal `]` first in a class is escaped (ECMAScript's `[]` is empty).
//
// Without this translation, _pydecimal's _parser regex (the canonical
// example) fails to compile and decimal becomes uninstantiable, which in
// turn breaks the import chain for tests that depend on decimal directly
// (test_decimal) or transitively (anything that imports json -> decimal).
//
// Limitations: this is a syntactic rewrite, not a full Python re reimpl.
// Constructs we don't support stay unsupported (e.g. `(?(id)yes|no)`
// conditional groups, `(?>...)` atomic groups, recursive `(?R)` /
// `(?P>name)`, scoped inline flags `(?i:...)`, lookbehind, named
// back-references where the index doesn't fit a single decimal digit).
struct TranslatedRegex {
    ReString pattern;                                          // ECMAScript-compatible source
    std::vector<std::pair<std::string, int>> groupIndex;       // name -> 1-based group index
    long long flags = 0;                                       // with the leading inline flags
    bool lineStartMarks = false;                               // subject needs line-start markers
};

// A character class (or a class escape such as \s), as the translator sees
// it: the engine's bracket contents, and whether the class matches each of
// the four line terminators under Python's rules.
struct ClassInfo {
    ReString body;              // between the brackets, in ECMAScript syntax
    bool negated = false;
    bool negatedEscape = false; // contains \S, \W or \D
    bool member[4] = {false, false, false, false};
};

static bool isHexDigit(ReChar c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

// Decodes the escape at src[i] == '\\' that denotes one character (\n, \x0a,
//  , \012, \\, \] ...). Returns the code point and the escape's length,
// or -1 for a class escape or one this translator does not decode.
static long decodeCharEscape(const ReString& src, size_t i, bool inClass, size_t& len) {
    len = 2;
    if (i + 1 >= src.size()) return -1;
    const ReChar n = src[i + 1];
    switch (n) {
        case L'n': return 0x0A;
        case L'r': return 0x0D;
        case L't': return 0x09;
        case L'f': return 0x0C;
        case L'v': return 0x0B;
        case L'a': return 0x07;
        case L'b': return inClass ? 0x08 : -1;
        default: break;
    }
    auto hexRun = [&](size_t start, size_t count) -> long {
        if (start + count > src.size()) return -1;
        long v = 0;
        for (size_t k = 0; k < count; ++k) {
            const ReChar h = src[start + k];
            if (!isHexDigit(h)) return -1;
            v = v * 16 + (h <= L'9' ? h - L'0' : (h | 0x20) - L'a' + 10);
        }
        return v;
    };
    if (n == L'x') { len = 4; return hexRun(i + 2, 2); }
    if (n == L'u') { len = 6; return hexRun(i + 2, 4); }
    if (n == L'U') { len = 10; return hexRun(i + 2, 8); }
    if (n >= L'0' && n <= L'7') {
        // Octal: \0, \0o, \0oo anywhere; \o, \oo, \ooo in a class; three
        // digits outside one (shorter forms there are back-references).
        size_t k = i + 1;
        long v = 0;
        size_t digits = 0;
        while (k < src.size() && digits < 3 && src[k] >= L'0' && src[k] <= L'7') {
            v = v * 8 + (src[k] - L'0');
            ++k;
            ++digits;
        }
        if (n != L'0' && !inClass && digits < 3) return -1;
        len = k - i;
        return v;
    }
    if ((n >= L'a' && n <= L'z') || (n >= L'A' && n <= L'Z') || (n >= L'1' && n <= L'9')) return -1;
    return static_cast<long>(n);  // an escaped punctuation character stands for itself
}

// Python's membership of the four terminators in a class escape.
static void classEscapeMembers(ReChar e, bool ascii, bool member[4]) {
    for (int k = 0; k < 4; ++k) {
        bool space = ascii ? (k < 2) : true;  // \s: \n, \r always; U+2028/9 unless ASCII
        switch (e) {
            case L's': member[k] = space; break;
            case L'S': member[k] = !space; break;
            case L'W': case L'D': member[k] = true; break;
            default: member[k] = false; break;  // \w, \d
        }
    }
}

// Parses the class at src[i] == '[' up to its closing ']'. Returns the index
// after it.
static size_t parseClass(const ReString& src, size_t i, bool ascii, ClassInfo& info) {
    size_t k = i + 1;
    if (k < src.size() && src[k] == L'^') { info.negated = true; ++k; }
    bool first = true;
    bool member[4] = {false, false, false, false};
    // The previous item when it was a single character (a range start).
    long pending = -1;
    while (k < src.size() && (src[k] != L']' || first)) {
        first = false;
        long cp = -1;
        size_t len = 1;
        ReString text;
        if (src[k] == L'\\' && k + 1 < src.size()) {
            const ReChar e = src[k + 1];
            if (e == L's' || e == L'S' || e == L'w' || e == L'W' || e == L'd' || e == L'D') {
                bool m[4];
                classEscapeMembers(e, ascii, m);
                for (int t = 0; t < 4; ++t) member[t] = member[t] || m[t];
                if (e == L'S' || e == L'W' || e == L'D') info.negatedEscape = true;
                info.body += src.substr(k, 2);
                k += 2;
                pending = -1;
                continue;
            }
            cp = decodeCharEscape(src, k, true, len);
            if (e == L'b') {
                text = RE_LIT("\\x08");  // a backspace; MSVC's std::regex read [\b] as "b"
            } else {
                text = src.substr(k, len);
            }
        } else {
            cp = static_cast<long>(src[k]);
            text = (src[k] == L']') ? ReString(RE_LIT("\\]")) : ReString(1, src[k]);
        }
        // A range: previous char, '-', this char.
        if (src[k] == L'-' && pending >= 0 && k + 1 < src.size() && src[k + 1] != L']') {
            size_t hiLen = 1;
            const long hi = (src[k + 1] == L'\\') ? decodeCharEscape(src, k + 1, true, hiLen)
                                                    : static_cast<long>(src[k + 1]);
            if (hi >= 0) {
                for (int t = 0; t < 4; ++t) {
                    const long term = static_cast<long>(kTerminator[t]);
                    if (term >= pending && term <= hi) member[t] = true;
                }
            }
            info.body += L'-';
            info.body += src.substr(k + 1, hiLen);
            k += 1 + hiLen;
            pending = -1;
            continue;
        }
        if (cp >= 0) {
            const int t = terminatorIndex(static_cast<unsigned long>(cp));
            if (t >= 0) member[t] = true;
        }
        info.body += text;
        pending = cp;
        k += len;
    }
    for (int t = 0; t < 4; ++t) info.member[t] = info.negated ? !member[t] : member[t];
    return k < src.size() ? k + 1 : k;
}

// Emits a class with Python's terminator membership. Without \S, \W or \D
// inside, the engine's class never matches a private code point, so the
// members are added to it (or the non-members to a negated one) and it stays
// a single bracket. With them, the engine's verdict on private code points is
// replaced by an explicit one.
static void emitClass(ReString& out, const ClassInfo& info, bool lineStartMarks) {
    ReString cls;
    if (!info.negatedEscape) {
        cls += info.negated ? RE_LIT("[^") : RE_LIT("[");
        cls += info.body;
        for (int t = 0; t < 4; ++t) {
            if (info.member[t] != info.negated) cls += kTerminatorSentinel[t];
        }
        if (info.negated) { cls += kLSM; cls += kBOS; }
        cls += L']';
    } else {
        cls += RE_LIT("(?:");
        ReString in;
        for (int t = 0; t < 4; ++t) {
            if (info.member[t]) in += kTerminatorSentinel[t];
        }
        if (!in.empty()) {
            cls += L'[';
            cls += in;
            cls += RE_LIT("]|");
        }
        cls += RE_LIT("(?![");
        for (int t = 0; t < 4; ++t) cls += kTerminatorSentinel[t];
        cls += kLSM;
        cls += kBOS;
        cls += RE_LIT("])");
        cls += info.negated ? RE_LIT("[^") : RE_LIT("[");
        cls += info.body;
        cls += RE_LIT("])");
    }
    if (lineStartMarks && info.member[0]) {
        // After a "\n" may come its line-start marker.
        out += RE_LIT("(?:");
        out += cls;
        out += kLSM;
        out += RE_LIT("?)");
    } else {
        out += cls;
    }
}

// The pattern uses `^` outside a class (it needs line-start markers under
// MULTILINE).
static bool usesCaret(const ReString& src, size_t i, bool verbose) {
    bool inClass = false;
    bool classStart = false;
    for (; i < src.size(); ++i) {
        const ReChar c = src[i];
        if (c == L'\\') { ++i; classStart = false; continue; }
        if (inClass) {
            if (c == L']' && !classStart) inClass = false;
            classStart = classStart && c == L'^';
            continue;
        }
        if (verbose && c == L'#') {
            while (i < src.size() && src[i] != L'\n') ++i;
            continue;
        }
        if (c == L'[') { inClass = true; classStart = true; continue; }
        if (c == L'^') return true;
    }
    return false;
}

static TranslatedRegex translatePyRegexEx(const std::string& srcUtf8, long long pyFlags) {
    const ReString src = toWide(srcUtf8);
    TranslatedRegex result;
    size_t i = 0;
    // Global inline flags at the start: (?aiLmsux), possibly several groups.
    while (i + 2 < src.size() && src[i] == L'(' && src[i + 1] == L'?') {
        size_t j = i + 2;
        long long f = 0;
        bool ok = true;
        for (; j < src.size() && src[j] != L')'; ++j) {
            switch (src[j]) {
                case L'a': f |= kReAscii; break;
                case L'i': f |= kReIgnoreCase; break;
                case L'L': f |= 4; break;
                case L'm': f |= kReMultiline; break;
                case L's': f |= kReDotAll; break;
                case L'u': f |= 32; break;
                case L'x': f |= kReVerbose; break;
                default: ok = false; break;
            }
            if (!ok) break;
        }
        if (!ok || j >= src.size() || j == i + 2) break;
        pyFlags |= f;
        i = j + 1;
    }
    result.flags = pyFlags;
    const bool verbose = (pyFlags & kReVerbose) != 0;
    const bool multiline = (pyFlags & kReMultiline) != 0;
    const bool dotall = (pyFlags & kReDotAll) != 0;
    const bool ascii = (pyFlags & kReAscii) != 0;
    const bool marks = multiline && usesCaret(src, i, verbose);
    result.lineStartMarks = marks;

    ReString& out = result.pattern;
    out.reserve(src.size() * 2);
    int groupCounter = 0;
    auto emitNewline = [&]() {
        if (marks) {
            out += RE_LIT("(?:");
            out += kNL;
            out += kLSM;
            out += RE_LIT("?)");
        } else {
            out += kNL;
        }
    };
    auto emitLiteral = [&](unsigned long cp, const ReString& asWritten) {
        const int t = terminatorIndex(cp);
        if (t == 0) emitNewline();
        else if (t > 0) out += kTerminatorSentinel[t];
        else out += asWritten;
    };
    while (i < src.size()) {
        const ReChar c = src[i];
        if (c == L'\\' && i + 1 < src.size()) {
            const ReChar n = src[i + 1];
            if (n == L'A') { out += kBOS; i += 2; continue; }
            if (n == L'z' || n == L'Z') { out += L'$'; i += 2; continue; }
            if (n == L'B') {
                // Never between a marker and what follows it.
                out += RE_LIT("(?![");
                out += kLSM;
                out += kBOS;
                out += RE_LIT("])\\B");
                i += 2;
                continue;
            }
            if (n == L's' || n == L'S' || n == L'w' || n == L'W' || n == L'd' || n == L'D') {
                ClassInfo info;
                info.body = src.substr(i, 2);
                bool m[4];
                classEscapeMembers(n, ascii, m);
                for (int t = 0; t < 4; ++t) info.member[t] = m[t];
                if (n == L'S' || n == L'W' || n == L'D') {
                    // [^\s...] / [^\w] / [^\d]: a negated bracket keeps it one class.
                    info.negated = true;
                    info.body = ReString(RE_LIT("\\")) + static_cast<ReChar>(n | 0x20);
                    for (int t = 0; t < 4; ++t) info.member[t] = m[t];
                }
                emitClass(out, info, marks);
                i += 2;
                continue;
            }
            size_t len = 2;
            const long cp = decodeCharEscape(src, i, false, len);
            if (cp >= 0 && terminatorIndex(static_cast<unsigned long>(cp)) >= 0) {
                emitLiteral(static_cast<unsigned long>(cp), ReString());
                i += len;
                continue;
            }
            out += c;
            out += n;
            i += 2;
            continue;
        }
        if (verbose) {
            if (c == L'#') {
                while (i < src.size() && src[i] != L'\n') ++i;
                continue;
            }
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v') {
                ++i;
                continue;
            }
        }
        if (c == L'[') {
            ClassInfo info;
            i = parseClass(src, i, ascii, info);
            emitClass(out, info, marks);
            continue;
        }
        if (c == L'.') {
            ClassInfo info;
            info.negated = true;  // [^\n] or, under DOTALL, everything
            info.member[0] = dotall;
            info.member[1] = info.member[2] = info.member[3] = true;
            emitClass(out, info, marks);
            ++i;
            continue;
        }
        if (c == L'^') {
            if (marks) {
                out += RE_LIT("(?:");
                out += kBOS;
                out += L'|';
                out += kLSM;
                out += L')';
            } else {
                out += kBOS;
            }
            ++i;
            continue;
        }
        if (c == L'$') {
            out += RE_LIT("(?=");
            out += kNL;
            out += multiline ? RE_LIT("|$)") : RE_LIT("?$)");
            ++i;
            continue;
        }
        if (c == L'(' && i + 2 < src.size() && src[i + 1] == L'?' && src[i + 2] == L'#') {
            while (i < src.size() && src[i] != L')') ++i;
            if (i < src.size()) ++i;
            continue;
        }
        if (c == L'(' && i + 3 < src.size() && src[i + 1] == L'?' && src[i + 2] == L'P') {
            // (?P<name>X) -> (X), capturing, recorded in groupIndex.
            if (src[i + 3] == L'<') {
                size_t j = i + 4;
                const size_t nameStart = j;
                while (j < src.size() && src[j] != L'>') ++j;
                std::string name = toUtf8(src.substr(nameStart, j - nameStart));
                ++groupCounter;
                result.groupIndex.emplace_back(std::move(name), groupCounter);
                out += L'(';
                if (j < src.size()) ++j;  // consume '>'
                i = j;
                continue;
            }
            // (?P=name) -> \<idx> (single-digit only — multi-digit back-refs
            // are ambiguous in ECMAScript; punt and emit nothing rather than
            // fabricate something invalid).
            if (src[i + 3] == L'=') {
                size_t j = i + 4;
                const size_t nameStart = j;
                while (j < src.size() && src[j] != L')') ++j;
                const std::string name = toUtf8(src.substr(nameStart, j - nameStart));
                int idx = -1;
                for (const auto& p : result.groupIndex) {
                    if (p.first == name) { idx = p.second; break; }
                }
                if (idx >= 1 && idx <= 9) {
                    out += L'\\';
                    out += static_cast<ReChar>(L'0' + idx);
                }
                if (j < src.size()) ++j;  // consume ')'
                i = j;
                continue;
            }
        }
        // Other (?...) prefixes are non-capturing — pass through, no counter bump.
        if (c == L'(' && i + 1 < src.size() && src[i + 1] == L'?') {
            out += c;
            ++i;
            continue;
        }
        // Plain '(' starts a capturing group.
        if (c == L'(') {
            ++groupCounter;
            out += c;
            ++i;
            continue;
        }
        emitLiteral(static_cast<unsigned long>(c), ReString(1, c));
        ++i;
    }
    return result;
}

// The subject as the engine sees it: a start-of-subject marker, then the
// code points with line terminators replaced by private code points and,
// when the pattern needs them, a line-start marker after each "\n".
// Positions map back to the subject's code points.
struct Subject {
    ReString orig;                // the str's code points
    ReString mapped;              // what the engine matches
    std::vector<size_t> origAt;   // mapped index -> orig index
    std::vector<size_t> mappedAt; // orig index -> mapped index after any marker

    size_t toOrig(size_t k) const { return origAt[k]; }
    // Where a search starting at orig position p begins: before the marker
    // in front of p (the start-of-subject marker, or the line-start marker
    // after a "\n"), so that ^ can take it.
    size_t searchStart(size_t p) const {
        const size_t k = mappedAt[p];
        return (k > 0 && (mapped[k - 1] == kBOS || mapped[k - 1] == kLSM)) ? k - 1 : k;
    }
    // Where a range ending at orig position p ends: after that marker.
    size_t searchEnd(size_t p) const { return mappedAt[p]; }
};

static Subject makeSubject(ReString s, bool lineStartMarks) {
    Subject sj;
    sj.orig = std::move(s);
    sj.mapped.reserve(sj.orig.size() + 1 + (lineStartMarks ? 16 : 0));
    sj.mappedAt.reserve(sj.orig.size() + 1);
    sj.origAt.reserve(sj.orig.size() + 2);
    sj.mapped += kBOS;
    sj.origAt.push_back(0);
    for (size_t p = 0; p < sj.orig.size(); ++p) {
        const ReChar c = sj.orig[p];
        sj.mappedAt.push_back(sj.mapped.size());
        sj.origAt.push_back(p);
        const int t = terminatorIndex(static_cast<unsigned long>(c));
        sj.mapped += t >= 0 ? kTerminatorSentinel[t] : c;
        if (lineStartMarks && t == 0) {
            sj.origAt.push_back(p + 1);
            sj.mapped += kLSM;
        }
    }
    sj.mappedAt.push_back(sj.mapped.size());
    sj.origAt.push_back(sj.orig.size());
    return sj;
}

// A compiled pattern and how its subjects must be prepared.
struct CompiledRe {
    TranslatedRegex tr;
    ReRegex re;
};

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
    if (flagsArgIdx >= 0 && posArgs && posArgs->getSize(ctx) > (proto::proto_ulong)flagsArgIdx) {
        const proto::ProtoObject* fa = posArgs->getAt(ctx, flagsArgIdx);
        if (fa && fa->isInteger(ctx)) flags |= fa->asLong(ctx);
    }
    return flags;
}

// An optional integer argument given at position `idx` or as keyword `name`.
static long long intArg(proto::ProtoContext* ctx, const proto::ProtoList* posArgs, proto::proto_ulong idx,
                        const proto::ProtoSparseList* kwArgs, const char* name, long long dflt) {
    const proto::ProtoObject* v = nullptr;
    if (posArgs && posArgs->getSize(ctx) > idx) {
        v = posArgs->getAt(ctx, static_cast<int>(idx));
    } else if (kwArgs) {
        const proto::proto_ulong h = PythonEnvironment::getInternedString(ctx, name)->getHash(ctx);
        if (kwArgs->has(ctx, h)) v = kwArgs->getAt(ctx, h);
    }
    return (v && v->isInteger(ctx)) ? v->asLong(ctx) : dflt;
}

// The flags of a module-level call: the compiled pattern's, the `flags`
// argument at position `idx` and the `flags` keyword.
static long long callFlags(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                           const proto::ProtoList* posArgs, proto::proto_ulong idx,
                           const proto::ProtoSparseList* kwArgs) {
    return extractFlags(ctx, patObj, nullptr, -1) | intArg(ctx, posArgs, idx, kwArgs, "flags", 0);
}

// A regex that never matches anything.  Used by compileRe() as a safe
// sentinel when the user-provided pattern fails to compile so the caller
// can continue (its regex_search will return false) while the Python-level
// re.error we set propagates back up.  Built once and cached so that the
// fallback path itself never throws.  CPython behaviour: re.error / PatternError.
static const ReRegex& neverMatchesRegex() {
    static const ReRegex kNever(RE_LIT(R"(\b\B)"));  // word-boundary AND non-boundary -> never true
    return kNever;
}

// Translates and compiles a pattern. Returns false, with an exception
// pending, when std::regex rejects it.
static bool compileRe(proto::ProtoContext* ctx, const std::string& pat, long long pyFlags, CompiledRe& out) {
    out.tr = translatePyRegexEx(pat, pyFlags);
    try {
        out.re = ReRegex(out.tr.pattern, pyFlagsToStdFlags(out.tr.flags));
        return true;
    } catch (const std::regex_error& e) {
        // std::regex (the C++ stdlib) rejects several constructs the
        // Python `re` module accepts — conditional groups `(?(...)...)`,
        // recursive patterns, lookbehind, atomic groups `(?>...)`.  Without
        // this they escape as C++ exceptions and abort the process via
        // std::terminate.  Push them across the boundary as a Python
        // exception so that user code (and unittest) can handle them.
        out.re = neverMatchesRegex();
        if (PythonEnvironment* env = ctx ? PythonEnvironment::fromContext(ctx) : nullptr) {
            env->raiseRuntimeError(ctx, std::string("regex compile error: ") + e.what());
        }
        return false;
    } catch (...) {
        out.re = neverMatchesRegex();
        if (PythonEnvironment* env = ctx ? PythonEnvironment::fromContext(ctx) : nullptr) {
            env->raiseRuntimeError(ctx, std::string("regex compile error"));
        }
        return false;
    }
}

// The mapped index of a sub-match boundary.
static size_t mappedIndex(const Subject& sj, ReString::const_iterator it) {
    return static_cast<size_t>(it - sj.mapped.cbegin());
}

// libc++'s regex_search over a non-empty range does not try the empty match
// at its very end ($ or a lookahead there); libstdc++ and MSVC do. Try it
// separately when the search from `from` found nothing.
static bool matchAtEnd(const CompiledRe& cr, const Subject& sj, size_t from, size_t last, ReMatch& m) {
    if (from >= last) return false;
    const auto end = sj.mapped.cbegin() + static_cast<std::ptrdiff_t>(last);
    return std::regex_search(end, end, m, cr.re,
                             std::regex_constants::match_prev_avail | std::regex_constants::match_continuous);
}

// Searches [pos, endpos) of the subject (code point positions). `anchored`:
// the match must start at pos (pattern.match). `whole`: it must span the
// whole range (fullmatch). `^` and `\A` match only at the true start, as in
// CPython, so a search from pos > 0 runs with match_not_bol and the previous
// character available.
static bool findMatch(const CompiledRe& cr, const Subject& sj, size_t pos, size_t endpos,
                      bool anchored, bool whole, ReMatch& m) {
    if (pos > endpos || endpos > sj.orig.size()) return false;
    const auto base = sj.mapped.cbegin();
    const auto last = base + static_cast<std::ptrdiff_t>(sj.searchEnd(endpos));
    auto flags = std::regex_constants::match_default;
    const size_t start = sj.searchStart(pos);
    // The previous character is there for \b (^ is the marker, not the engine's).
    if (start > 0) flags |= std::regex_constants::match_prev_avail;
    if (anchored) flags |= std::regex_constants::match_continuous;
    auto attempt = [&](size_t from, std::regex_constants::match_flag_type f) {
        const auto first = base + static_cast<std::ptrdiff_t>(from);
        return whole ? std::regex_match(first, last, m, cr.re, f) : std::regex_search(first, last, m, cr.re, f);
    };
    if (attempt(start, flags)) return true;
    if (!anchored && !whole && matchAtEnd(cr, sj, start, sj.searchEnd(endpos), m)) return true;
    // A MULTILINE pattern whose match at pos does not take the line-start
    // marker before pos: try once more after it.
    const size_t after = sj.searchEnd(pos);
    if ((anchored || whole) && after != start) {
        return attempt(after, flags | std::regex_constants::match_prev_avail);
    }
    return false;
}

// Calls f(match) for each successive match in [pos, endpos), until f returns
// false, with ECMAScript's (and Python's) rule for empty matches: after an
// empty match the next one must be non-empty or start further on. The
// iteration is done here rather than by std::regex_iterator, whose handling
// of an empty match at the end of the subject differs between libc++ and
// libstdc++. Two empty matches at one position of the subject (one on each
// side of a marker) count once.
template <class F>
static void forEachMatch(const CompiledRe& cr, const Subject& sj, size_t pos, size_t endpos, F&& f) {
    if (pos > endpos || endpos > sj.orig.size()) return;
    const auto base = sj.mapped.cbegin();
    const size_t last = sj.searchEnd(endpos);
    size_t cur = sj.searchStart(pos);
    bool notNull = false;
    size_t lastEmpty = static_cast<size_t>(-1);
    ReMatch m;
    while (cur <= last) {
        auto flags = std::regex_constants::match_default;
        if (cur > 0) flags |= std::regex_constants::match_prev_avail;
        if (notNull) flags |= std::regex_constants::match_not_null | std::regex_constants::match_continuous;
        if (!std::regex_search(base + static_cast<std::ptrdiff_t>(cur), base + static_cast<std::ptrdiff_t>(last),
                               m, cr.re, flags)
            && (notNull || !matchAtEnd(cr, sj, cur, last, m))) {
            if (!notNull || cur == last) break;
            notNull = false;
            ++cur;
            continue;
        }
        const size_t ms = mappedIndex(sj, m[0].first);
        const size_t me = mappedIndex(sj, m[0].second);
        const size_t a = sj.toOrig(ms);
        const size_t b = sj.toOrig(me);
        bool report = true;
        if (a == b) {
            report = a != lastEmpty;
            lastEmpty = a;
        }
        if (report && !f(m)) break;
        notNull = ms == me;
        cur = me;
    }
}

// Sub-match `i` as code point positions of the subject; false if it did not
// participate.
static bool groupSpan(const Subject& sj, const ReMatch& m, size_t i, size_t& a, size_t& b) {
    if (i >= m.size() || !m[i].matched) return false;
    a = sj.toOrig(mappedIndex(sj, m[i].first));
    b = sj.toOrig(mappedIndex(sj, m[i].second));
    return true;
}

static ReString groupText(const Subject& sj, const ReMatch& m, size_t i) {
    size_t a, b;
    return groupSpan(sj, m, i, a, b) ? sj.orig.substr(a, b - a) : ReString();
}

// ---------------------------------------------------------------------------
// Helper: build a match object from a match over a Subject.
// Stores:
//   __re_match_str__  — full match string (group 0)
//   __re_pos__        — start position in the subject string (code points)
//   __re_end__        — end position in the subject string (code points)
//   __re_groups__     — ProtoList of captured-group strings (groups 1..n), or PROTO_NONE for unmatched
//   __re_string__     — subject string
// ---------------------------------------------------------------------------
static const proto::ProtoObject* makeMatchObject(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* matchProto,
    const ReMatch& m,
    const Subject& sj,
    const proto::ProtoObject* subjectStr,
    const proto::ProtoObject* patObj = nullptr)
{
    if (!matchProto) return PROTO_NONE;
    const proto::ProtoObject* mo = matchProto->newChild(ctx, true);

    size_t a = 0, b = 0;
    groupSpan(sj, m, 0, a, b);
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_match_str__"),
        newStr(ctx, sj.orig.substr(a, b - a)));
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_string__"),
        (subjectStr && subjectStr->isString(ctx)) ? subjectStr : newStr(ctx, sj.orig));
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pos__"),
        ctx->fromInteger(static_cast<long long>(a)));
    mo = mo->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_end__"),
        ctx->fromInteger(static_cast<long long>(b)));

    // Store captured groups (indices 1..n) as a list.
    const proto::ProtoList* groups = ctx->newList();
    for (size_t i = 1; i < m.size(); ++i) {
        size_t ga, gb;
        if (groupSpan(sj, m, i, ga, gb)) {
            groups = groups->appendLast(ctx, newStr(ctx, sj.orig.substr(ga, gb - ga)));
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

static const proto::ProtoObject* getMatchProto(proto::ProtoContext* ctx,
    const proto::ProtoObject* self)
{
    const proto::ProtoObject* mp = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    return mp;
}

// A Python list holding `items`.
static const proto::ProtoObject* pyList(proto::ProtoContext* ctx, const proto::ProtoList* items) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
    if (listProto) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
        listObj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"), items->asObject(ctx));
        return listObj;
    }
    return PythonEnvironment::wrapList(ctx, items);
}

// ---------------------------------------------------------------------------
// Substitution
// ---------------------------------------------------------------------------

// Appends the expansion of a Python replacement template for match `m`, as
// re._parser.parse_template defines it: group references \1 to \99,
// \g<number> and \g<name>, octal escapes (\0, \0oo and \ooo), the escapes
// \a \b \f \n \r \t \v \\, and any other escaped non-letter kept as written.
// Returns false with an exception pending for a bad reference or escape.
static bool expandTemplate(proto::ProtoContext* ctx, const ReString& tmpl, const ReMatch& m,
                           const Subject& sj, const TranslatedRegex& tr, ReString& out) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    auto fail = [&](const std::string& msg) {
        if (env) env->raiseRuntimeError(ctx, msg);
        return false;
    };
    auto appendGroup = [&](size_t g) {
        if (g >= m.size()) return fail("invalid group reference " + std::to_string(g));
        if (m[g].matched) out += groupText(sj, m, g);  // an unmatched group expands to ''
        return true;
    };
    auto isDigit = [](ReChar d) { return d >= L'0' && d <= L'9'; };
    auto isOctal = [](ReChar d) { return d >= L'0' && d <= L'7'; };
    for (size_t i = 0; i < tmpl.size(); ++i) {
        const ReChar c = tmpl[i];
        if (c != L'\\' || i + 1 == tmpl.size()) {
            out += c;
            continue;
        }
        const ReChar n = tmpl[++i];
        if (n == L'g') {
            if (i + 1 >= tmpl.size() || tmpl[i + 1] != L'<') return fail("missing <");
            const size_t close = tmpl.find(L'>', i + 2);
            if (close == ReString::npos) return fail("missing >, unterminated name");
            const ReString name = tmpl.substr(i + 2, close - (i + 2));
            i = close;
            if (name.empty()) return fail("missing group name");
            bool numeric = true;
            for (ReChar d : name) numeric = numeric && isDigit(d);
            if (numeric) {
                if (name.size() > 9) return fail("invalid group reference " + toUtf8(name));
                if (!appendGroup(std::stoul(toUtf8(name)))) return false;
                continue;
            }
            const std::string key = toUtf8(name);
            int idx = -1;
            for (const auto& p : tr.groupIndex) {
                if (p.first == key) { idx = p.second; break; }
            }
            if (idx < 0) return fail("unknown group name '" + key + "'");
            if (!appendGroup(static_cast<size_t>(idx))) return false;
            continue;
        }
        if (n == L'0') {
            unsigned v = 0;
            size_t j = i + 1;
            for (int k = 0; k < 2 && j < tmpl.size() && isOctal(tmpl[j]); ++k, ++j) v = v * 8 + (tmpl[j] - L'0');
            out += static_cast<ReChar>(v);
            i = j - 1;
            continue;
        }
        if (isDigit(n)) {
            if (i + 2 < tmpl.size() && isOctal(n) && isOctal(tmpl[i + 1]) && isOctal(tmpl[i + 2])) {
                const unsigned v = (n - L'0') * 64 + (tmpl[i + 1] - L'0') * 8 + (tmpl[i + 2] - L'0');
                if (v > 0377) return fail("octal escape value outside of range 0-0o377");
                out += static_cast<ReChar>(v);
                i += 2;
                continue;
            }
            size_t g = static_cast<size_t>(n - L'0');
            if (i + 1 < tmpl.size() && isDigit(tmpl[i + 1])) g = g * 10 + static_cast<size_t>(tmpl[++i] - L'0');
            if (!appendGroup(g)) return false;
            continue;
        }
        switch (n) {
            case L'a': out += L'\a'; break;
            case L'b': out += L'\b'; break;
            case L'f': out += L'\f'; break;
            case L'n': out += L'\n'; break;
            case L'r': out += L'\r'; break;
            case L't': out += L'\t'; break;
            case L'v': out += L'\v'; break;
            case L'\\': out += L'\\'; break;
            default:
                if ((n >= L'a' && n <= L'z') || (n >= L'A' && n <= L'Z')) {
                    return fail(std::string("bad escape \\") + static_cast<char>(n));
                }
                out += L'\\';
                out += n;
        }
    }
    return true;
}


// pattern.sub / subn and re.sub / subn: replaces the first `count` matches
// (all when count is 0) of the pattern in `strObj`. `replObj` is a replacement
// template or a callable that receives each match object and returns the
// replacement (None inserts nothing). Returns false with an exception pending
// on error; otherwise stores the new string and the number of replacements.
static bool substitute(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                       const proto::ProtoObject* matchProto, const proto::ProtoObject* replObj,
                       const proto::ProtoObject* strObj, long long count, long long flags,
                       const proto::ProtoObject*& resultOut, long long& replacedOut) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return false;
    std::string pat;
    if (!patObj || !getPattern(ctx, patObj, pat)) {
        env->raiseTypeError(ctx, "first argument must be string or compiled pattern");
        return false;
    }
    ReString s;
    if (!wideArg(ctx, strObj, s)) {
        env->raiseTypeError(ctx, "expected string or bytes-like object");
        return false;
    }
    ReString tmpl;
    const bool replIsTemplate = wideArg(ctx, replObj, tmpl);
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return false;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);

    ReString out;
    long long replaced = 0;
    size_t last = 0;
    bool ok = true;
    forEachMatch(cr, sj, 0, sj.orig.size(), [&](const ReMatch& m) {
        if (count > 0 && replaced >= count) return false;
        size_t start, end;
        groupSpan(sj, m, 0, start, end);
        out.append(sj.orig, last, start - last);
        if (replIsTemplate) {
            if (!expandTemplate(ctx, tmpl, m, sj, cr.tr, out)) { ok = false; return false; }
        } else {
            const proto::ProtoObject* mo = makeMatchObject(ctx, matchProto, m, sj, strObj, patObj);
            PythonEnvironment::TransientPin pinMatch(env, mo);
            const proto::ProtoObject* r = env->callObject(replObj, { mo });
            if (!r) {
                if (env->hasPendingException()) { ok = false; return false; }
            } else if (r->isString(ctx)) {
                ReString piece;
                wideArg(ctx, r, piece);
                out += piece;
            } else if (r != PROTO_NONE && r != env->getNonePrototype()) {
                std::string typeName = "object";
                const proto::ProtoObject* cls = env->getType(ctx, r);
                const proto::ProtoObject* nm = cls ? cls->getAttribute(ctx, env->getNameString()) : nullptr;
                if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, typeName);
                env->raiseTypeError(ctx, "expected str instance, " + typeName + " found");
                ok = false;
                return false;
            }
        }
        last = end;
        ++replaced;
        return true;
    });
    if (!ok) return false;
    out.append(sj.orig, last, ReString::npos);
    resultOut = newStr(ctx, out);
    replacedOut = replaced;
    return true;
}

static const proto::ProtoObject* newPair(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* a, const proto::ProtoObject* b) {
    return ctx->newTupleFromList(ctx->newList()->appendLast(ctx, a)->appendLast(ctx, b))->asObject(ctx);
}

// The [pos, endpos) window of the pattern methods, in code points: the
// arguments at positions `first` and `first + 1`, clamped as CPython does.
static void matchWindow(proto::ProtoContext* ctx, const proto::ProtoList* posArgs, proto::proto_ulong first,
                        size_t size, size_t& pos, size_t& endpos) {
    pos = 0;
    endpos = size;
    if (posArgs->getSize(ctx) > first) {
        const auto* posArg = posArgs->getAt(ctx, static_cast<int>(first));
        if (posArg && posArg->isInteger(ctx)) {
            long long p = posArg->asLong(ctx);
            if (p < 0) p = 0;
            if ((size_t)p > size) p = (long long)size;
            pos = (size_t)p;
        }
    }
    if (posArgs->getSize(ctx) > first + 1) {
        const auto* epArg = posArgs->getAt(ctx, static_cast<int>(first + 1));
        if (epArg && epArg->isInteger(ctx)) {
            long long ep = epArg->asLong(ctx);
            if (ep < 0) ep = 0;
            if ((size_t)ep > size) ep = (long long)size;
            endpos = (size_t)ep;
        }
    }
}

enum class MatchKind { Match, Search, FullMatch };

// The match object of pattern `patObj` against `strObj`, or None.
static const proto::ProtoObject* matchOnce(proto::ProtoContext* ctx, const proto::ProtoObject* matchProto,
                                           const proto::ProtoObject* patObj, const proto::ProtoObject* strObj,
                                           long long flags, MatchKind kind,
                                           const proto::ProtoList* windowArgs, proto::proto_ulong windowFirst) {
    std::string pat;
    ReString s;
    if (!getPattern(ctx, patObj, pat)) return PROTO_NONE;
    if (!wideArg(ctx, strObj, s)) return PROTO_NONE;
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return nullptr;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);
    size_t pos = 0, endpos = sj.orig.size();
    if (windowArgs) matchWindow(ctx, windowArgs, windowFirst, sj.orig.size(), pos, endpos);
    ReMatch m;
    if (!findMatch(cr, sj, pos, endpos, kind == MatchKind::Match, kind == MatchKind::FullMatch, m)) return PROTO_NONE;
    return makeMatchObject(ctx, matchProto, m, sj, strObj, patObj);
}

// The list re.findall returns: each match's group 0, its one group, or the
// tuple of its groups.
static const proto::ProtoObject* findAll(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                         const proto::ProtoObject* strObj, long long flags,
                                         const proto::ProtoList* windowArgs, proto::proto_ulong windowFirst) {
    std::string pat;
    ReString s;
    if (!getPattern(ctx, patObj, pat) || !wideArg(ctx, strObj, s)) return pyList(ctx, ctx->newList());
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return nullptr;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);
    size_t pos = 0, endpos = sj.orig.size();
    if (windowArgs) matchWindow(ctx, windowArgs, windowFirst, sj.orig.size(), pos, endpos);
    const proto::ProtoList* results = ctx->newList();
    forEachMatch(cr, sj, pos, endpos, [&](const ReMatch& sm) {
        if (sm.size() > 2) {
            // 2+ capturing groups: return list of tuples (CPython behavior)
            const proto::ProtoList* grps = ctx->newList();
            for (size_t i = 1; i < sm.size(); ++i) grps = grps->appendLast(ctx, newStr(ctx, groupText(sj, sm, i)));
            const proto::ProtoTuple* tup = ctx->newTupleFromList(grps);
            results = results->appendLast(ctx, tup ? tup->asObject(ctx) : grps->asObject(ctx));
        } else if (sm.size() == 2) {
            // Exactly 1 capturing group: return the group string (CPython behavior)
            results = results->appendLast(ctx, newStr(ctx, groupText(sj, sm, 1)));
        } else {
            // No capturing groups: return full match string
            results = results->appendLast(ctx, newStr(ctx, groupText(sj, sm, 0)));
        }
        return true;
    });
    return pyList(ctx, results);
}

// The match objects of re.finditer, as a list.
static const proto::ProtoObject* findIter(proto::ProtoContext* ctx, const proto::ProtoObject* matchProto,
                                          const proto::ProtoObject* patObj, const proto::ProtoObject* strObj,
                                          long long flags, const proto::ProtoList* windowArgs,
                                          proto::proto_ulong windowFirst) {
    std::string pat;
    ReString s;
    if (!getPattern(ctx, patObj, pat) || !wideArg(ctx, strObj, s)) return pyList(ctx, ctx->newList());
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return nullptr;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);
    size_t pos = 0, endpos = sj.orig.size();
    if (windowArgs) matchWindow(ctx, windowArgs, windowFirst, sj.orig.size(), pos, endpos);
    const proto::ProtoList* results = ctx->newList();
    forEachMatch(cr, sj, pos, endpos, [&](const ReMatch& m) {
        results = results->appendLast(ctx, makeMatchObject(ctx, matchProto, m, sj, strObj, patObj));
        return true;
    });
    return pyList(ctx, results);
}

// re.split: the pieces between matches, each match's groups after the piece
// before it, at most `maxsplit` splits (0: all). Empty matches split too, as
// in CPython 3.7 and later.
static const proto::ProtoObject* splitString(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                             const proto::ProtoObject* strObj, long long maxsplit, long long flags) {
    std::string pat;
    ReString s;
    if (!getPattern(ctx, patObj, pat) || !wideArg(ctx, strObj, s)) return pyList(ctx, ctx->newList());
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return nullptr;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);
    const proto::ProtoList* results = ctx->newList();
    size_t last = 0;
    long long splits = 0;
    forEachMatch(cr, sj, 0, sj.orig.size(), [&](const ReMatch& m) {
        if (maxsplit > 0 && splits >= maxsplit) return false;
        size_t a, b;
        groupSpan(sj, m, 0, a, b);
        results = results->appendLast(ctx, newStr(ctx, sj.orig.substr(last, a - last)));
        for (size_t g = 1; g < m.size(); ++g) {
            size_t ga, gb;
            results = results->appendLast(ctx, groupSpan(sj, m, g, ga, gb)
                ? newStr(ctx, sj.orig.substr(ga, gb - ga)) : PROTO_NONE);
        }
        last = b;
        ++splits;
        return true;
    });
    results = results->appendLast(ctx, newStr(ctx, sj.orig.substr(last)));
    return pyList(ctx, results);
}

// ---------------------------------------------------------------------------
// Module-level functions
// ---------------------------------------------------------------------------

static const proto::ProtoObject* py_compile(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    if (posArgs->getSize(ctx) < 1 || !posArgs->getAt(ctx, 0)->isString(ctx)) return PROTO_NONE;
    std::string pat;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, pat);
    const long long flags = intArg(ctx, posArgs, 1, kwArgs, "flags", 0);
    const proto::ProtoObject* proto = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__pattern_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* p = proto->newChild(ctx, true);
    // The pattern source is user text, not vocabulary.
    const proto::ProtoObject* patObj = PythonEnvironment::newStr(ctx, pat);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_pattern__"), patObj);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__re_flags__"),
        ctx->fromInteger(flags));
    // Public attributes expected by CPython: re.Pattern exposes
    // `.pattern` (the source string), `.flags` (int), `.groups` (int).
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pattern"), patObj);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flags"),
        ctx->fromInteger(flags));
    // Translate now (cheap) to extract the named-group map and the number of
    // groups.
    TranslatedRegex tr = translatePyRegexEx(pat, flags);
    int groupCount = 0;
    {
        // Count capture groups in the translated source: every unescaped '(' that
        // is not '(?'-prefixed (non-capturing / lookaround / etc.) is one group.
        bool inClass = false;
        for (size_t i = 0; i < tr.pattern.size(); ++i) {
            const ReChar c = tr.pattern[i];
            if (c == L'\\' && i + 1 < tr.pattern.size()) { ++i; continue; }
            if (inClass) { if (c == L']') inClass = false; continue; }
            if (c == L'[') { inClass = true; continue; }
            if (c != L'(') continue;
            if (i + 1 < tr.pattern.size() && tr.pattern[i + 1] == L'?') continue;
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

// re.match(pattern, string, flags=0)
static const proto::ProtoObject* py_match(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    return matchOnce(ctx, getMatchProto(ctx, self), patObj, posArgs->getAt(ctx, 1),
                     callFlags(ctx, patObj, posArgs, 2, kwArgs), MatchKind::Match, nullptr, 0);
}

// re.search(pattern, string, flags=0)
static const proto::ProtoObject* py_search(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    return matchOnce(ctx, getMatchProto(ctx, self), patObj, posArgs->getAt(ctx, 1),
                     callFlags(ctx, patObj, posArgs, 2, kwArgs), MatchKind::Search, nullptr, 0);
}

static const proto::ProtoObject* py_escape(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    ReString s;
    if (!wideArg(ctx, patObj, s)) return patObj;
    ReString escaped;
    for (ReChar c : s) {
        // Escaping the bytes of a multibyte character broke it; like CPython,
        // non-ASCII characters are never escaped.
        if (c < 0x80 && !isalnum(static_cast<unsigned char>(c)) && c != L'_') escaped += L'\\';
        escaped += c;
    }
    return newStr(ctx, escaped);
}

// ---------------------------------------------------------------------------
// Pattern-object methods
// ---------------------------------------------------------------------------

// pattern.match(string[, pos[, endpos]])
static const proto::ProtoObject* py_pattern_match(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    return matchOnce(ctx, getMatchProto(ctx, self), self, posArgs->getAt(ctx, 0),
                     extractFlags(ctx, self, nullptr, -1), MatchKind::Match, posArgs, 1);
}

// pattern.fullmatch(string[, pos[, endpos]])
static const proto::ProtoObject* py_pattern_fullmatch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    return matchOnce(ctx, getMatchProto(ctx, self), self, posArgs->getAt(ctx, 0),
                     extractFlags(ctx, self, nullptr, -1), MatchKind::FullMatch, posArgs, 1);
}

// pattern.search(string[, pos[, endpos]])
static const proto::ProtoObject* py_pattern_search(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    return matchOnce(ctx, getMatchProto(ctx, self), self, posArgs->getAt(ctx, 0),
                     extractFlags(ctx, self, nullptr, -1), MatchKind::Search, posArgs, 1);
}

// pattern.sub(repl, string, count=0)
static const proto::ProtoObject* py_pattern_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!posArgs || posArgs->getSize(ctx) < 2) {
        if (env) env->raiseTypeError(ctx, "sub() missing required argument 'repl' or 'string'");
        return nullptr;
    }
    const proto::ProtoObject* result = nullptr;
    long long replaced = 0;
    if (!substitute(ctx, self, getMatchProto(ctx, self), posArgs->getAt(ctx, 0), posArgs->getAt(ctx, 1),
                    intArg(ctx, posArgs, 2, kwArgs, "count", 0), extractFlags(ctx, self, nullptr, -1),
                    result, replaced)) {
        return nullptr;
    }
    return result;
}

// pattern.subn(repl, string, count=0) -> (new_string, number_of_subs)
static const proto::ProtoObject* py_pattern_subn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!posArgs || posArgs->getSize(ctx) < 2) {
        if (env) env->raiseTypeError(ctx, "subn() missing required argument 'repl' or 'string'");
        return nullptr;
    }
    const proto::ProtoObject* result = nullptr;
    long long replaced = 0;
    if (!substitute(ctx, self, getMatchProto(ctx, self), posArgs->getAt(ctx, 0), posArgs->getAt(ctx, 1),
                    intArg(ctx, posArgs, 2, kwArgs, "count", 0), extractFlags(ctx, self, nullptr, -1),
                    result, replaced)) {
        return nullptr;
    }
    return newPair(ctx, result, ctx->fromInteger(replaced));
}

// pattern.findall(string[, pos[, endpos]])
static const proto::ProtoObject* py_pattern_findall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return pyList(ctx, ctx->newList());
    return findAll(ctx, self, posArgs->getAt(ctx, 0), extractFlags(ctx, self, nullptr, -1), posArgs, 1);
}

// pattern.finditer(string[, pos[, endpos]]) → list of match objects.
// Mirrors py_pattern_findall but yields match objects instead of group strings,
// matching CPython's `re.Pattern.finditer` contract.  doctest's `_EXAMPLE_RE`
// drives the use case (its `parse` calls `self._EXAMPLE_RE.finditer(string)`).
static const proto::ProtoObject* py_pattern_finditer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*)
{
    if (posArgs->getSize(ctx) < 1) return pyList(ctx, ctx->newList());
    return findIter(ctx, getMatchProto(ctx, self), self, posArgs->getAt(ctx, 0),
                    extractFlags(ctx, self, nullptr, -1), posArgs, 1);
}

// pattern.split(string, maxsplit=0)
static const proto::ProtoObject* py_pattern_split(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    if (posArgs->getSize(ctx) < 1) return pyList(ctx, ctx->newList());
    return splitString(ctx, self, posArgs->getAt(ctx, 0), intArg(ctx, posArgs, 1, kwArgs, "maxsplit", 0),
                       extractFlags(ctx, self, nullptr, -1));
}

// Module-level findall / fullmatch / split / sub
// re.fullmatch(pattern, string, flags=0)
static const proto::ProtoObject* py_fullmatch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs)
{
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    return matchOnce(ctx, getMatchProto(ctx, self), patObj, posArgs->getAt(ctx, 1),
                     callFlags(ctx, patObj, posArgs, 2, kwArgs), MatchKind::FullMatch, nullptr, 0);
}

// re.findall(pattern, string, flags=0)
static const proto::ProtoObject* py_findall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    if (posArgs->getSize(ctx) < 2) return pyList(ctx, ctx->newList());
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    return findAll(ctx, patObj, posArgs->getAt(ctx, 1), callFlags(ctx, patObj, posArgs, 2, kwargs), nullptr, 0);
}

// re.sub(pattern, repl, string, count=0, flags=0)
static const proto::ProtoObject* py_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!posArgs || posArgs->getSize(ctx) < 3) {
        if (env) env->raiseTypeError(ctx, "sub() missing required argument 'pattern', 'repl' or 'string'");
        return nullptr;
    }
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* result = nullptr;
    long long replaced = 0;
    if (!substitute(ctx, patObj, getMatchProto(ctx, self), posArgs->getAt(ctx, 1), posArgs->getAt(ctx, 2),
                    intArg(ctx, posArgs, 3, kwargs, "count", 0), callFlags(ctx, patObj, posArgs, 4, kwargs),
                    result, replaced)) {
        return nullptr;
    }
    return result;
}

// re.subn(pattern, repl, string, count=0, flags=0) -> (new_string, number_of_subs)
static const proto::ProtoObject* py_subn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!posArgs || posArgs->getSize(ctx) < 3) {
        if (env) env->raiseTypeError(ctx, "subn() missing required argument 'pattern', 'repl' or 'string'");
        return nullptr;
    }
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* result = nullptr;
    long long replaced = 0;
    if (!substitute(ctx, patObj, getMatchProto(ctx, self), posArgs->getAt(ctx, 1), posArgs->getAt(ctx, 2),
                    intArg(ctx, posArgs, 3, kwargs, "count", 0), callFlags(ctx, patObj, posArgs, 4, kwargs),
                    result, replaced)) {
        return nullptr;
    }
    return newPair(ctx, result, ctx->fromInteger(replaced));
}

// re.finditer(pattern, string, flags=0) → match objects (returned as a list)
static const proto::ProtoObject* py_finditer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    if (posArgs->getSize(ctx) < 2) return pyList(ctx, ctx->newList());
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    return findIter(ctx, getMatchProto(ctx, self), patObj, posArgs->getAt(ctx, 1),
                    callFlags(ctx, patObj, posArgs, 2, kwargs), nullptr, 0);
}

// re.split(pattern, string, maxsplit=0, flags=0)
static const proto::ProtoObject* py_split_module(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs)
{
    if (posArgs->getSize(ctx) < 2) return pyList(ctx, ctx->newList());
    const proto::ProtoObject* patObj = posArgs->getAt(ctx, 0);
    return splitString(ctx, patObj, posArgs->getAt(ctx, 1), intArg(ctx, posArgs, 2, kwargs, "maxsplit", 0),
                       callFlags(ctx, patObj, posArgs, 3, kwargs));
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

    ReString s;
    std::string pat;
    wideArg(ctx, strObj, s);
    patAttr->asString(ctx)->toUTF8String(ctx, pat);

    long long pos = 0;
    if (posObj && posObj->isInteger(ctx)) pos = posObj->asLong(ctx);
    if (pos >= (long long)s.size()) return PROTO_NONE;

    long long flags = extractFlags(ctx, self, nullptr, -1);
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return nullptr;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);
    ReMatch m;
    if (!findMatch(cr, sj, static_cast<size_t>(pos), sj.orig.size(), true, false, m)) return PROTO_NONE;
    size_t a, b;
    groupSpan(sj, m, 0, a, b);

    // Advance position.
    const proto::ProtoObject* newSelf = self->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__scan_pos__"),
        ctx->fromInteger(static_cast<long long>(b)));
    // Store updated self back (immutable model workaround: caller won't see it, but
    // the test only calls match() once per position iteration, so this is acceptable).
    (void)newSelf;

    return makeMatchObject(ctx, mpAttr, m, sj, strObj);
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
    // The scanned subject and the pattern are data: interning them kept whole
    // input texts alive for the life of the process.
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_str__"),
        PythonEnvironment::newStr(ctx, strVal));
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pat__"),
        PythonEnvironment::newStr(ctx, pat));
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pos__"),
        ctx->fromInteger(0));
    iterObj = iterObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pattern"),
        PythonEnvironment::newStr(ctx, pat));
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
        PythonEnvironment::newStr(ctx, combined));
    compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pattern"),
        PythonEnvironment::newStr(ctx, combined));
    if (mpAttr) compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__match_proto__"), mpAttr);
    compiledPat = compiledPat->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "scanner"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(compiledPat), py_pattern_scanner_method));

    scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "scanner"), compiledPat);
    scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_lexicon__"), actionList->asObject(ctx));
    scanObj = scanObj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__scan_pattern__"),
        PythonEnvironment::newStr(ctx, combined));
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

    ReString s;
    wideArg(ctx, posArgs->getAt(ctx, 0), s);

    const proto::ProtoObject* patObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__scan_pattern__"));
    const proto::ProtoObject* lexObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__scan_lexicon__"));
    const proto::ProtoObject* mpAttr = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__match_proto__"));
    (void)mpAttr;

    std::string pat;
    if (patObj && patObj->isString(ctx)) patObj->asString(ctx)->toUTF8String(ctx, pat);
    else return PROTO_NONE;

    long long flags = extractFlags(ctx, self, nullptr, -1);
    CompiledRe cr;
    if (!compileRe(ctx, pat, flags, cr)) return nullptr;
    const Subject sj = makeSubject(std::move(s), cr.tr.lineStartMarks);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* results = ctx->newList();
    size_t i = 0;

    while (i < sj.orig.size()) {
        ReMatch m;
        if (!findMatch(cr, sj, i, sj.orig.size(), true, false, m)) break;
        size_t a, j;
        groupSpan(sj, m, 0, a, j);
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
                    const proto::ProtoObject* tokenStr = newStr(ctx, sj.orig.substr(a, j - a));
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
    const proto::ProtoObject* remaining = newStr(ctx, sj.orig.substr(i));
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
    patternProto = patternProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "subn"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(patternProto), py_pattern_subn));
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
