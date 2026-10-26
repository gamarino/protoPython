// src/library/ReModule.cpp -- the `re` module on PCRE2.
//
// Python's `re` is implemented over PCRE2's 8-bit library (UTF mode for str
// patterns, byte mode for bytes patterns) with its JIT. Earlier versions
// translated patterns to ECMAScript and matched with std::wregex, recompiling
// the pattern on every call: about 47 us per search where CPython takes 0.5.
//
// The pieces:
//   Translator       Python pattern syntax -> PCRE2 syntax, with Python's
//                    errors (re.error, with the position) for everything
//                    Python rejects, so PCRE2 never sees a construct whose
//                    meaning differs between the two (see "Translation").
//   CompiledPattern  the translation and its compiled code: one variant for
//                    search, one anchored (match) and one anchored at both
//                    ends (fullmatch), each compiled on first use and
//                    JIT-compiled where the platform supports it. Owned by
//                    the Pattern object (released by its finalizer).
//   Subject          the UTF-8 bytes of a str subject (the bytes of a bytes
//                    one) and the code point <-> byte offset map, built only
//                    for non-ASCII text.
//   MatchState       per-thread PCRE2 match data, match context and JIT
//                    stack (thread_local: no native thread API is used).
//
// Positions reported to Python are code point indexes; PCRE2 works in bytes.

#include <protoPython/PythonEnvironment.h>
#include <protoPython/ReModule.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace protoPython {
namespace re {

namespace {

// Python re flag bits.
constexpr long long F_TEMPLATE = 1;
constexpr long long F_I = 2;
constexpr long long F_L = 4;
constexpr long long F_M = 8;
constexpr long long F_S = 16;
constexpr long long F_U = 32;
constexpr long long F_X = 64;
constexpr long long F_DEBUG = 128;
constexpr long long F_A = 256;

// The engine's resource limits. A pattern that needs more raises
// RecursionError, never a silent "no match".
constexpr std::uint32_t kMatchLimit = 100000000u;       // backtracking steps
constexpr std::uint32_t kDepthLimit = 10000000u;
constexpr std::uint32_t kHeapLimitKiB = 1u << 20;       // 1 GiB of backtracking memory
constexpr std::size_t kJitStackStart = 32u << 10;
constexpr std::size_t kJitStackMax = 8u << 20;
// Subjects at least this long are matched outside the managed region, so a
// long match never holds up a collection.
constexpr std::size_t kUnmanagedSubjectBytes = 64u << 10;
// Module-level calls with a str or bytes pattern reuse compiled patterns
// from a cache of at most this many entries (CPython keeps 512).
constexpr std::size_t kCacheSize = 512;

// ---------------------------------------------------------------------------
// Code points
// ---------------------------------------------------------------------------

// The length of the UTF-8 sequence that starts at s[i], as protoCore reads
// it: a malformed sequence is one code point per byte. Surrogates (encoded
// as three-byte sequences, as a str may hold them) count as one code point.
size_t utf8SeqLen(const std::string& s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
    if (i + n > s.size()) return 1;
    for (size_t k = 1; k < n; ++k) {
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return 1;
    }
    return n;
}

std::uint32_t utf8Decode(const std::string& s, size_t i, size_t n) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (n == 1) return c;
    std::uint32_t cp = n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
    return cp;
}

void decodeCodePoints(const std::string& s, std::vector<std::uint32_t>& out) {
    out.clear();
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t n = utf8SeqLen(s, i);
        out.push_back(utf8Decode(s, i, n));
        i += n;
    }
}

void appendUtf8(std::string& out, std::uint32_t cp) {
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

bool isAsciiLetter(std::uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiDigit(std::uint32_t c) { return c >= '0' && c <= '9'; }
bool isOctDigit(std::uint32_t c) { return c >= '0' && c <= '7'; }
bool isHexDigit(std::uint32_t c) {
    return isAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
std::uint32_t hexValue(std::uint32_t c) {
    return isAsciiDigit(c) ? c - '0' : (c | 0x20) - 'a' + 10;
}

// ---------------------------------------------------------------------------
// Translation: Python pattern syntax -> PCRE2 pattern syntax
//
// The output is pure ASCII: every character that is not an ASCII letter,
// digit or '_' is written as \x{...}, so no character of the pattern can take
// a PCRE2 meaning Python does not give it. Python constructs are rewritten:
//   (?P<name>...)       a plain capturing group; names are kept here
//   (?P=name), \N       \g{N}
//   (?(name)...)        (?(N)...)
//   \Z, \z              \z (PCRE2's \Z also matches before a final "\n")
//   \v                  \x{0b} (PCRE2's \v is a class)
//   \u, \U, \x, octal   \x{...}
//   {,n} {m,} {m,n}     normalized; a '{' that is not a quantifier is literal
//   [ inside a class    \x{5b} (PCRE2 would read [:alpha:])
//   (?x) / re.VERBOSE   whitespace and comments are removed here
//   \w, \W, \s, \S      for str patterns without re.ASCII, Python's own sets:
//                       \w is [\p{L}\p{N}_] (str.isalnum() or '_'), \s is the
//                       29 code points str.isspace() accepts
// Everything Python rejects raises re.error here, with Python's message and
// position. Constructs PCRE2 has and Python does not -- (?R), (?1), (?&name),
// (?|...), (*VERB), \K, \Q...\E, \p{..} and the like -- are rejected the way
// Python rejects them (unknown extension, bad escape, nothing to repeat).
// ---------------------------------------------------------------------------

struct PatternError {
    std::string msg;
    size_t pos;             // code point index in the pattern
    bool overflow = false;  // OverflowError, as CPython raises for huge repeat counts
};

// The code points str.isspace() accepts (CPython 3.14, Unicode 16), and
// their complement, for use inside a character class.
const char* const kPySpaceClass =
    "\\x{09}-\\x{0d}\\x{1c}-\\x{20}\\x{85}\\x{a0}\\x{1680}\\x{2000}-\\x{200a}"
    "\\x{2028}\\x{2029}\\x{202f}\\x{205f}\\x{3000}";
const char* const kPyNonSpaceClass =
    "\\x{0}-\\x{8}\\x{e}-\\x{1b}\\x{21}-\\x{84}\\x{86}-\\x{9f}\\x{a1}-\\x{167f}\\x{1681}-\\x{1fff}"
    "\\x{200b}-\\x{2027}\\x{202a}-\\x{202e}\\x{2030}-\\x{205e}\\x{2060}-\\x{2fff}\\x{3001}-\\x{10ffff}";
// re.ASCII's sets, spelled out so they hold whatever PCRE2_UCP says.
const char* const kAsciiWordClass = "A-Za-z0-9_";
const char* const kAsciiNonWordClass = "\\x{0}-\\x{2f}\\x{3a}-\\x{40}\\x{5b}-\\x{5e}\\x{60}\\x{7b}-\\x{10ffff}";
const char* const kAsciiNonDigitClass = "\\x{0}-\\x{2f}\\x{3a}-\\x{10ffff}";
const char* const kAsciiSpaceClass = "\\x{9}-\\x{d}\\x{20}";
const char* const kAsciiNonSpaceClass = "\\x{0}-\\x{8}\\x{e}-\\x{1f}\\x{21}-\\x{10ffff}";
// A class no character belongs to: what a surrogate code point becomes (a
// subject holding one is rejected before matching, so none can match).
const char* const kNoChar = "[^\\x{0}-\\x{10ffff}]";

// A valid group name: a Python identifier. str.isidentifier is
// approximated by its definition over general categories (XID_Start:
// letters, letter numbers, '_'; XID_Continue adds marks, decimal digits and
// connector punctuation), checked by PCRE2's Unicode tables. Bytes names
// must be ASCII identifiers.
bool isIdentifier(const std::string& name, bool isBytes) {
    if (name.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(name[0]);
    if (isBytes || first < 0x80) {
        bool ascii = true;
        for (unsigned char c : name) ascii = ascii && c < 0x80;
        if (ascii || isBytes) {
            if (!ascii) return false;
            if (!(std::isalpha(first) || first == '_')) return false;
            for (unsigned char c : name) {
                if (!(std::isalnum(c) || c == '_')) return false;
            }
            return true;
        }
    }
    static pcre2_code* code = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        int err = 0;
        PCRE2_SIZE off = 0;
        static const char kIdent[] =
            "^[\\p{L}\\p{Nl}_][\\p{L}\\p{Nl}\\p{Mn}\\p{Mc}\\p{Nd}\\p{Pc}]*\\z";
        code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(kIdent), PCRE2_ZERO_TERMINATED, PCRE2_UTF, &err, &off,
                             nullptr);
    });
    if (!code) return false;
    pcre2_match_data* md = pcre2_match_data_create(1, nullptr);
    if (!md) throw std::bad_alloc();
    const int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(name.data()), name.size(), 0, 0, md, nullptr);
    pcre2_match_data_free(md);
    return rc >= 0;
}

struct Translation {
    std::string out;
    long long flags = 0;  // effective flags (argument, inline, implied UNICODE)
    unsigned groups = 0;
    std::vector<std::pair<std::string, unsigned>> names;  // in definition order
    std::vector<unsigned> parent;                         // parent[g]: enclosing group, 0 at top
};

class Translator {
public:
    Translator(const std::vector<std::uint32_t>& p, bool isBytes, long long flags)
        : p_(p), n_(p.size()), isBytes_(isBytes) {
        t_.flags = flags;
        t_.parent.push_back(0);
        closed_.push_back(true);
    }

    Translation run() {
        verbose_ = (t_.flags & F_X) != 0;
        caseless_ = (t_.flags & F_I) != 0;
        leadingFlags();
        globalAscii_ = !isBytes_ && (t_.flags & F_A);
        ascii_ = globalAscii_;
        pcreI_ = wantPcreI();
        while (true) {
            if (verbose_) i_ = skipVerbose(i_);
            if (i_ >= n_) break;
            const std::uint32_t c = p_[i_];
            switch (c) {
                case '\\': escape(); break;
                case '[': klass(); last_ = Last::Atom; break;
                case '(': open(); break;
                case ')': close(); break;
                case '|':
                    if (!st_.empty() && st_.back().cond && ++st_.back().bars > 1)
                        fail("conditional backref with more than two branches", i_);
                    t_.out += '|';
                    last_ = Last::None;
                    ++i_;
                    break;
                case '*': case '+': case '?':
                    quantifier(std::string(1, static_cast<char>(c)), i_, i_ + 1);
                    break;
                case '{': brace(); break;
                case '^': t_.out += '^'; last_ = Last::Anchor; ++i_; break;
                case '$': t_.out += '$'; last_ = Last::Anchor; ++i_; break;
                case '.': t_.out += '.'; last_ = Last::Atom; ++i_; break;
                default:
                    literal(c);
                    last_ = Last::Atom;
                    ++i_;
            }
        }
        if (!st_.empty()) fail("missing ), unterminated subpattern", st_.back().open);
        for (const auto& r : condRefs_) {
            if (r.first > t_.groups) fail("invalid group reference " + std::to_string(r.first), r.second);
        }
        return std::move(t_);
    }

private:
    enum class Last { None, Atom, Anchor, Quant };
    struct Frame {
        unsigned cap;
        bool verbose;
        bool caseless;
        bool ascii;
        bool pcreI;
        size_t open;
        bool cond;
        int bars;
        bool lookbehind;
        unsigned firstGroup;  // groups defined before this frame opened
    };

    const std::vector<std::uint32_t>& p_;
    size_t n_;
    bool isBytes_;
    size_t i_ = 0;
    Translation t_;
    bool verbose_ = false;
    bool caseless_ = false;
    bool globalAscii_ = false;  // a str pattern under re.ASCII (no PCRE2_UCP)
    bool ascii_ = false;        // re.ASCII in the current scope (str only)
    bool pcreI_ = false;        // PCRE2's caseless option in the current scope
    Last last_ = Last::None;
    std::vector<Frame> st_;
    std::vector<bool> closed_;
    std::vector<std::pair<unsigned, size_t>> condRefs_;

    [[noreturn]] void fail(const std::string& msg, size_t pos) { throw PatternError{msg, pos}; }

    // re.ASCII | re.IGNORECASE on a str pattern: PCRE2 folds every letter in
    // UTF mode, Python only ASCII ones, so case-insensitivity is spelled out
    // here ([aA]) and PCRE2 matches case-sensitively in that scope.
    bool emulateCaseless() const { return caseless_ && !isBytes_ && ascii_; }
    bool wantPcreI() const { return caseless_ && (isBytes_ || !ascii_); }

    // The first group number a back reference made here may not use: the
    // groups of an enclosing lookbehind (CPython rejects them).
    int lookbehindFirst() const {
        for (const Frame& f : st_) {
            if (f.lookbehind) return static_cast<int>(f.firstGroup) + 1;
        }
        return -1;
    }

    // \w \W \d \D \s \S as PCRE2 syntax, outside (inClass false: a complete
    // atom) or inside a character class (a class body fragment).
    std::string classEscape(std::uint32_t e, bool inClass) {
        auto wrap = [&](const std::string& body, bool neg) {
            return inClass ? body : (neg ? "[^" : "[") + body + "]";
        };
        if (isBytes_) return std::string("\\") + static_cast<char>(e);
        if (ascii_) {
            switch (e) {
                case 'w': return wrap(kAsciiWordClass, false);
                case 'W': return inClass ? std::string(kAsciiNonWordClass) : wrap(kAsciiWordClass, true);
                case 'd': return wrap("0-9", false);
                case 'D': return inClass ? std::string(kAsciiNonDigitClass) : wrap("0-9", true);
                case 's': return wrap(kAsciiSpaceClass, false);
                default: return inClass ? std::string(kAsciiNonSpaceClass) : wrap(kAsciiSpaceClass, true);
            }
        }
        switch (e) {
            case 'w': return wrap("\\p{L}\\p{N}_", false);
            case 'W':
                if (!inClass) return wrap("\\p{L}\\p{N}_", true);
                // No class body is the complement of [\p{L}\p{N}_]. PCRE2's
                // own \W under PCRE2_UCP is that complement (minus marks and
                // connector punctuation from PCRE2 10.43 on); without UCP
                // (a (?u:...) scope in a re.ASCII pattern) it is ASCII-only.
                if (globalAscii_)
                    fail("\\W inside a character class in a (?u:...) scope of a re.ASCII pattern is not "
                         "supported by this re module", i_ - 2);
                return "\\W";
            case 'd': return inClass ? std::string("\\p{Nd}") : std::string("\\p{Nd}");
            case 'D': return "\\P{Nd}";
            case 's': return wrap(kPySpaceClass, false);
            default: return inClass ? std::string(kPyNonSpaceClass) : wrap(kPySpaceClass, true);
        }
    }

    // \b / \B: PCRE2's own where its notion of a word character is
    // Python's for the scope, else spelled out with lookarounds.
    std::string boundary(bool word) const {
        if (isBytes_ || ascii_ == globalAscii_) return word ? "\\b" : "\\B";
        const std::string w = ascii_ ? "[A-Za-z0-9_]" : "[\\p{L}\\p{N}_]";
        if (word) return "(?:(?<=" + w + ")(?!" + w + ")|(?<!" + w + ")(?=" + w + "))";
        return "(?:(?<=" + w + ")(?=" + w + ")|(?<!" + w + ")(?!" + w + "))";
    }

    std::string text(size_t a, size_t b) const {
        std::string s;
        for (size_t k = a; k < b && k < n_; ++k) appendUtf8(s, p_[k]);
        return s;
    }

    static bool isFlagChar(std::uint32_t c) {
        return c == 'a' || c == 'i' || c == 'L' || c == 'm' || c == 's' || c == 'u' || c == 'x';
    }
    static long long flagOf(std::uint32_t c) {
        switch (c) {
            case 'a': return F_A;
            case 'i': return F_I;
            case 'L': return F_L;
            case 'm': return F_M;
            case 's': return F_S;
            case 'u': return F_U;
            case 'x': return F_X;
            default: return 0;
        }
    }

    size_t skipVerbose(size_t j) const {
        while (j < n_) {
            const std::uint32_t c = p_[j];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
                ++j;
            } else if (c == '#') {
                while (j < n_ && p_[j] != '\n') ++j;
            } else {
                break;
            }
        }
        return j;
    }

    void checkFlagCombination(long long f, size_t pos) {
        if (isBytes_) {
            if (f & F_U) fail("bad inline flags: cannot use 'u' flag with a bytes pattern", pos);
        } else if (f & F_L) {
            fail("bad inline flags: cannot use 'L' flag with a str pattern", pos);
        }
        const int count = ((f & F_A) ? 1 : 0) + ((f & F_U) ? 1 : 0) + ((f & F_L) ? 1 : 0);
        if (count > 1) fail("bad inline flags: flags 'a', 'u' and 'L' are incompatible", pos);
    }

    // (?aiLmsux) groups at the very start set global flags.
    void leadingFlags() {
        for (;;) {
            size_t j = verbose_ ? skipVerbose(i_) : i_;
            if (!(j + 2 < n_ && p_[j] == '(' && p_[j + 1] == '?' && isFlagChar(p_[j + 2]))) return;
            size_t k = j + 2;
            long long add = 0;
            while (k < n_ && isFlagChar(p_[k])) add |= flagOf(p_[k++]);
            if (k >= n_ || p_[k] != ')') return;  // a scoped group: the main loop
            // Conflicts with the flags argument are a ValueError, checked
            // by the caller once the whole pattern is read (CPython's
            // fix_flags); here only the inline flags among themselves.
            checkFlagCombination(add, k);
            t_.flags |= add;
            verbose_ = (t_.flags & F_X) != 0;
            caseless_ = (t_.flags & F_I) != 0;
            i_ = k + 1;
        }
    }

    static void emitCp(std::string& out, std::uint32_t c) {
        if (c < 0x80 && (isAsciiLetter(c) || isAsciiDigit(c) || c == '_')) {
            out += static_cast<char>(c);
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "\\x{%x}", static_cast<unsigned>(c));
        out += buf;
    }

    bool isSurrogate(std::uint32_t c) const { return !isBytes_ && c >= 0xD800 && c <= 0xDFFF; }

    void literal(std::uint32_t c) {
        if (isSurrogate(c)) {
            t_.out += kNoChar;
        } else if (emulateCaseless() && isAsciiLetter(c)) {
            t_.out += '[';
            t_.out += static_cast<char>(c | 0x20);
            t_.out += static_cast<char>(c & ~0x20u);
            t_.out += ']';
        } else {
            emitCp(t_.out, c);
        }
    }

    void quantifier(const std::string& spec, size_t start, size_t next) {
        if (last_ == Last::None || last_ == Last::Anchor) fail("nothing to repeat", start);
        if (last_ == Last::Quant) fail("multiple repeat", start);
        t_.out += spec;
        i_ = next;
        if (i_ < n_ && (p_[i_] == '?' || p_[i_] == '+')) {
            t_.out += static_cast<char>(p_[i_]);
            ++i_;
        }
        last_ = Last::Quant;
    }

    void brace() {
        const size_t s = i_;
        size_t k = i_ + 1;
        auto asLiteral = [&]() {
            literal('{');
            last_ = Last::Atom;
            ++i_;
        };
        if (k < n_ && p_[k] == '}') return asLiteral();
        std::string lo, hi;
        while (k < n_ && isAsciiDigit(p_[k])) lo += static_cast<char>(p_[k++]);
        if (k < n_ && p_[k] == ',') {
            ++k;
            while (k < n_ && isAsciiDigit(p_[k])) hi += static_cast<char>(p_[k++]);
        } else {
            hi = lo;
        }
        if (!(k < n_ && p_[k] == '}')) return asLiteral();
        ++k;
        const unsigned long long kMaxRepeat = 4294967295ull;  // CPython's MAXREPEAT
        auto value = [&](const std::string& d) -> unsigned long long {
            const unsigned long long v = d.size() > 10 ? kMaxRepeat : std::stoull(d);
            if (v >= kMaxRepeat) throw PatternError{"the repetition number is too large", s + 1, true};
            return v;
        };
        const unsigned long long mn = lo.empty() ? 0 : value(lo);
        const bool unbounded = hi.empty();
        const unsigned long long mx = unbounded ? 0 : value(hi);
        if (!unbounded && mx < mn) fail("min repeat greater than max repeat", s + 1);
        if (mn > 65535 || (!unbounded && mx > 65535))
            fail("repetition counts above 65535 are not supported by the regular-expression engine", s + 1);
        std::string spec = "{" + std::to_string(mn) + "," + (unbounded ? std::string() : std::to_string(mx)) + "}";
        quantifier(spec, s, k);
    }

    // Exactly `count` hex digits at i_ (after the escape letter).
    std::uint32_t hexDigits(size_t count, size_t escStart) {
        if (i_ + count > n_) fail("incomplete escape " + text(escStart, n_), escStart);
        std::uint32_t v = 0;
        for (size_t k = 0; k < count; ++k) {
            const std::uint32_t h = p_[i_ + k];
            if (!isHexDigit(h)) fail("incomplete escape " + text(escStart, i_ + k), escStart);
            v = v * 16 + hexValue(h);
        }
        i_ += count;
        return v;
    }

    // The escape letters both contexts share that stand for one code point:
    // \a \f \n \r \t \v \x \u \U \N. Returns true and sets `cp` for one.
    bool charEscape(std::uint32_t e, size_t s, std::uint32_t& cp) {
        switch (e) {
            case 'a': cp = 0x07; return true;
            case 'f': cp = 0x0c; return true;
            case 'n': cp = 0x0a; return true;
            case 'r': cp = 0x0d; return true;
            case 't': cp = 0x09; return true;
            case 'v': cp = 0x0b; return true;
            case 'x': cp = hexDigits(2, s); return true;
            case 'u':
                if (isBytes_) fail("bad escape \\u", s);
                cp = hexDigits(4, s);
                return true;
            case 'U':
                if (isBytes_) fail("bad escape \\U", s);
                cp = hexDigits(8, s);
                if (cp > 0x10FFFF) fail("bad escape " + text(s, i_), s);
                return true;
            case 'N':
                if (isBytes_) fail("bad escape \\N", s);
                fail("named Unicode escapes (\\N{...}) are not supported by this re module", s);
            default:
                return false;
        }
    }

    void escape() {
        const size_t s = i_;
        if (i_ + 1 >= n_) fail("bad escape (end of pattern)", s);
        const std::uint32_t e = p_[i_ + 1];
        i_ += 2;
        std::uint32_t cp = 0;
        switch (e) {
            case 'A': t_.out += "\\A"; last_ = Last::Anchor; return;
            case 'Z': case 'z': t_.out += "\\z"; last_ = Last::Anchor; return;
            case 'b': t_.out += boundary(true); last_ = Last::Anchor; return;
            case 'B': t_.out += boundary(false); last_ = Last::Anchor; return;
            case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
                t_.out += classEscape(e, false);
                last_ = Last::Atom;
                return;
            case '0': {
                std::uint32_t v = 0;
                for (int k = 0; k < 2 && i_ < n_ && isOctDigit(p_[i_]); ++k) v = v * 8 + (p_[i_++] - '0');
                emitLiteralAt(v, s);
                return;
            }
            default:
                break;
        }
        if (e >= '1' && e <= '9') {
            unsigned g = e - '0';
            if (i_ < n_ && isAsciiDigit(p_[i_])) {
                if (isOctDigit(e) && isOctDigit(p_[i_]) && i_ + 1 < n_ && isOctDigit(p_[i_ + 1])) {
                    const std::uint32_t v = (e - '0') * 64 + (p_[i_] - '0') * 8 + (p_[i_ + 1] - '0');
                    i_ += 2;
                    if (v > 0377) fail("octal escape value " + text(s, i_) + " outside of range 0-0o377", s);
                    emitLiteralAt(v, s);
                    return;
                }
                g = g * 10 + (p_[i_] - '0');
                ++i_;
            }
            backref(g, s);
            return;
        }
        if (charEscape(e, s, cp)) {
            emitLiteralAt(cp, s);
            return;
        }
        if (e < 0x80 && isAsciiLetter(e)) fail("bad escape \\" + std::string(1, static_cast<char>(e)), s);
        emitLiteralAt(e, s);
    }

    void emitLiteralAt(std::uint32_t cp, size_t s) {
        const size_t save = i_;
        i_ = s;
        literal(cp);
        i_ = save;
        last_ = Last::Atom;
    }

    void backref(unsigned g, size_t s) {
        if (g > t_.groups) fail("invalid group reference " + std::to_string(g), s + 1);
        if (!closed_[g]) fail("cannot refer to an open group", s);
        checkLookbehindRef(g, i_);
        if (emulateCaseless())
            fail("case-insensitive back references are not supported together with re.ASCII by this re module", s);
        t_.out += "\\g{" + std::to_string(g) + "}";
        last_ = Last::Atom;
    }

    void checkLookbehindRef(unsigned g, size_t pos) {
        const int first = lookbehindFirst();
        if (first > 0 && static_cast<int>(g) >= first)
            fail("cannot refer to group defined in the same lookbehind subpattern", pos);
    }

    // ---- character classes ----

    struct ClassItem {
        bool isSet = false;
        std::string set;
        std::uint32_t cp = 0;
    };

    ClassItem classItem() {
        ClassItem it;
        const std::uint32_t c = p_[i_];
        if (c != '\\') {
            it.cp = c;
            ++i_;
            return it;
        }
        const size_t s = i_;
        if (i_ + 1 >= n_) fail("bad escape (end of pattern)", s);
        const std::uint32_t e = p_[i_ + 1];
        i_ += 2;
        switch (e) {
            case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
                it.isSet = true;
                it.set = classEscape(e, true);
                return it;
            case 'b': it.cp = 0x08; return it;
            default: break;
        }
        if (isOctDigit(e)) {
            std::uint32_t v = e - '0';
            for (int k = 0; k < 2 && i_ < n_ && isOctDigit(p_[i_]); ++k) v = v * 8 + (p_[i_++] - '0');
            if (v > 0377) fail("octal escape value " + text(s, i_) + " outside of range 0-0o377", s);
            it.cp = v;
            return it;
        }
        if (e == '8' || e == '9') fail("bad escape \\" + std::string(1, static_cast<char>(e)), s);
        std::uint32_t cp = 0;
        if (charEscape(e, s, cp)) {
            it.cp = cp;
            return it;
        }
        if (e < 0x80 && isAsciiLetter(e)) fail("bad escape \\" + std::string(1, static_cast<char>(e)), s);
        it.cp = e;
        return it;
    }

    void emitRange(std::string& body, std::uint32_t lo, std::uint32_t hi) {
        if (!isBytes_ && hi >= 0xD800 && lo <= 0xDFFF) {
            // Surrogates cannot occur in a subject (see kNoChar): keep the
            // rest of the range.
            if (lo < 0xD800) emitRange(body, lo, 0xD7FF);
            if (hi > 0xDFFF) emitRange(body, 0xE000, hi);
            return;
        }
        emitCp(body, lo);
        if (hi > lo) {
            body += '-';
            emitCp(body, hi);
        }
        if (!emulateCaseless()) return;
        // The other case of the ASCII letters in [lo, hi].
        auto addShifted = [&](std::uint32_t a, std::uint32_t b, std::uint32_t from, std::uint32_t to, int shift) {
            const std::uint32_t x = std::max(a, from), y = std::min(b, to);
            if (x > y) return;
            emitCp(body, static_cast<std::uint32_t>(static_cast<int>(x) + shift));
            if (y > x) {
                body += '-';
                emitCp(body, static_cast<std::uint32_t>(static_cast<int>(y) + shift));
            }
        };
        addShifted(lo, hi, 'a', 'z', -32);
        addShifted(lo, hi, 'A', 'Z', 32);
    }

    void klass() {
        const size_t s = i_;
        ++i_;
        bool neg = false;
        if (i_ < n_ && p_[i_] == '^') {
            neg = true;
            ++i_;
        }
        std::string body;
        bool first = true;
        for (;;) {
            if (i_ >= n_) fail("unterminated character set", s);
            if (p_[i_] == ']' && !first) {
                ++i_;
                break;
            }
            first = false;
            const size_t itemStart = i_;
            ClassItem a = classItem();
            if (i_ + 1 < n_ && p_[i_] == '-' && p_[i_ + 1] != ']') {
                ++i_;
                ClassItem b = classItem();
                if (a.isSet || b.isSet || b.cp < a.cp)
                    fail("bad character range " + text(itemStart, i_), itemStart);
                emitRange(body, a.cp, b.cp);
            } else if (a.isSet) {
                body += a.set;
            } else {
                emitRange(body, a.cp, a.cp);
            }
        }
        if (body.empty()) {
            // Only surrogates: no character, or (negated) every character.
            t_.out += neg ? "[\\x{0}-\\x{10ffff}]" : kNoChar;
            return;
        }
        t_.out += neg ? "[^" : "[";
        t_.out += body;
        t_.out += ']';
    }

    // ---- groups ----

    void push(unsigned cap, size_t open, bool cond = false, bool lookbehind = false) {
        st_.push_back(Frame{cap, verbose_, caseless_, ascii_, pcreI_, open, cond, 0, lookbehind, t_.groups});
        last_ = Last::None;
    }

    unsigned currentCapture() const {
        for (auto it = st_.rbegin(); it != st_.rend(); ++it) {
            if (it->cap) return it->cap;
        }
        return 0;
    }

    // A group name ending at `term`, validated as CPython does. i_ is left
    // after the terminator.
    std::string groupName(size_t start, std::uint32_t term) {
        size_t j = start;
        while (j < n_ && p_[j] != term) ++j;
        if (j == start) fail("missing group name", start);
        if (j >= n_) fail(term == '>' ? "missing >, unterminated name" : "missing ), unterminated name", start);
        std::string name;
        if (isBytes_) {
            for (size_t k = start; k < j; ++k) name += static_cast<char>(p_[k]);
        } else {
            name = text(start, j);
        }
        if (!isIdentifier(name, isBytes_)) {
            std::string shown = name;
            if (isBytes_) {
                shown.clear();
                for (size_t k = start; k < j; ++k) {
                if (p_[k] < 0x80) {
                    shown += static_cast<char>(p_[k]);
                } else {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\x%02x", static_cast<unsigned>(p_[k]));
                    shown += b;
                }
            }
            }
            fail("bad character in group name '" + shown + "'", start);
        }
        i_ = j + 1;
        return name;
    }

    int lookupName(const std::string& name) const {
        for (const auto& kv : t_.names) {
            if (kv.first == name) return static_cast<int>(kv.second);
        }
        return -1;
    }

    void capture(const std::string& name, size_t s, size_t nameStart) {
        const unsigned g = ++t_.groups;
        if (g > 65535) fail("too many groups", s);
        t_.parent.push_back(currentCapture());
        closed_.push_back(false);
        if (!name.empty()) {
            const int old = lookupName(name);
            if (old >= 0) {
                fail("redefinition of group name '" + name + "' as group " + std::to_string(g) +
                     "; was group " + std::to_string(old), nameStart);
            }
            t_.names.emplace_back(name, g);
        }
        t_.out += '(';
        push(g, s);
    }

    void open() {
        const size_t s = i_;
        if (!(i_ + 1 < n_ && p_[i_ + 1] == '?')) {
            ++i_;
            capture(std::string(), s, s);
            return;
        }
        const size_t k = i_ + 2;
        if (k >= n_) fail("unexpected end of pattern", k);
        const std::uint32_t c = p_[k];
        if (c == 'P') {
            if (k + 1 >= n_) fail("unexpected end of pattern", k + 1);
            const std::uint32_t d = p_[k + 1];
            if (d == '<') {
                const std::string name = groupName(k + 2, '>');
                capture(name, s, k + 2);
                return;
            }
            if (d == '=') {
                const std::string name = groupName(k + 2, ')');
                const int g = lookupName(name);
                if (g < 0) fail("unknown group name '" + name + "'", k + 2);
                if (!closed_[static_cast<size_t>(g)]) fail("cannot refer to an open group", k + 2);
                checkLookbehindRef(static_cast<unsigned>(g), i_);
                if (emulateCaseless())
                    fail("case-insensitive back references are not supported together with re.ASCII by this re module", s);
                t_.out += "\\g{" + std::to_string(g) + "}";
                last_ = Last::Atom;
                return;
            }
            fail("unknown extension ?P" + text(k + 1, k + 2), s + 1);
        }
        if (c == ':') {
            t_.out += "(?:";
            i_ = k + 1;
            push(0, s);
            return;
        }
        if (c == '=' || c == '!') {
            t_.out += "(?";
            t_.out += static_cast<char>(c);
            i_ = k + 1;
            push(0, s);
            return;
        }
        if (c == '<') {
            if (k + 1 >= n_) fail("unexpected end of pattern", k + 1);
            if (p_[k + 1] == '=' || p_[k + 1] == '!') {
                t_.out += "(?<";
                t_.out += static_cast<char>(p_[k + 1]);
                i_ = k + 2;
                push(0, s, false, true);
                return;
            }
            fail("unknown extension ?" + text(k, k + 2), s + 1);
        }
        if (c == '>') {
            t_.out += "(?>";
            i_ = k + 1;
            push(0, s);
            return;
        }
        if (c == '#') {
            size_t j = k + 1;
            while (j < n_ && p_[j] != ')') ++j;
            if (j >= n_) fail("missing ), unterminated comment", s);
            i_ = j + 1;
            return;  // a comment changes nothing, not even what a quantifier repeats
        }
        if (c == '(') {
            size_t j = k + 1;
            while (j < n_ && p_[j] != ')') ++j;
            if (j == k + 1) fail("missing group name", k + 1);
            if (j >= n_) fail("missing ), unterminated name", k + 1);
            bool digits = true;
            for (size_t q = k + 1; q < j; ++q) digits = digits && isAsciiDigit(p_[q]);
            unsigned g = 0;
            if (digits) {
                const std::string d = text(k + 1, j);
                if (d.size() > 5) fail("invalid group reference " + d, k + 1);
                g = static_cast<unsigned>(std::stoul(d));
                if (g == 0) fail("bad group number", k + 1);
                i_ = j + 1;
                if (lookbehindFirst() > 0 && g > t_.groups) fail("cannot refer to an open group", i_);
                condRefs_.emplace_back(g, k + 1);
            } else {
                const std::string name = groupName(k + 1, ')');
                const int idx = lookupName(name);
                if (idx < 0) fail("unknown group name '" + name + "'", k + 1);
                g = static_cast<unsigned>(idx);
            }
            checkLookbehindRef(g, i_);
            t_.out += "(?(" + std::to_string(g) + ")";
            push(0, s, true);
            return;
        }
        if (isFlagChar(c) || c == '-') {
            scopedFlags(s, k);
            return;
        }
        fail("unknown extension ?" + text(k, k + 1), s + 1);
    }

    void scopedFlags(size_t s, size_t k) {
        long long add = 0, del = 0;
        size_t j = k;
        while (j < n_ && isFlagChar(p_[j])) add |= flagOf(p_[j++]);
        auto letter = [&](size_t q) { return q < n_ && p_[q] < 0x80 && isAsciiLetter(p_[q]); };
        bool minus = false;
        if (j < n_ && p_[j] == '-') {
            minus = true;
            ++j;
            if (j >= n_ || !isFlagChar(p_[j])) fail(letter(j) ? "unknown flag" : "missing flag", j);
            while (j < n_ && isFlagChar(p_[j])) {
                const long long f = flagOf(p_[j]);
                if (f & (F_A | F_U | F_L)) fail("bad inline flags: cannot turn off flags 'a', 'u' and 'L'", j + 1);
                del |= f;
                ++j;
            }
        }
        if (j < n_ && p_[j] == ')' && !minus) fail("global flags not at the start of the expression", s);
        if (j >= n_ || p_[j] != ':') {
            if (letter(j)) fail("unknown flag", j);
            fail(minus ? "missing :" : "missing -, : or )", j);
        }
        if (add & del) fail("bad inline flags: flag turned on and off", j);
        checkFlagCombination(add, j);
        push(0, s);
        if (add & F_X) verbose_ = true;
        if (del & F_X) verbose_ = false;
        if (add & F_I) caseless_ = true;
        if (del & F_I) caseless_ = false;
        if (!isBytes_ && (add & F_A)) ascii_ = true;
        if (!isBytes_ && (add & F_U)) ascii_ = false;
        // PCRE2's caseless option for the scope: on where Python folds case
        // and PCRE2 can do it (not for re.ASCII str scopes, spelled out).
        const bool wantI = wantPcreI();
        std::string on, off;
        if (wantI && !pcreI_) on += 'i';
        if (!wantI && pcreI_) off += 'i';
        pcreI_ = wantI;
        if (add & F_M) on += 'm';
        if (add & F_S) on += 's';
        if (del & F_M) off += 'm';
        if (del & F_S) off += 's';
        t_.out += "(?" + on + (off.empty() ? std::string() : "-" + off) + ":";
        i_ = j + 1;
        last_ = Last::None;
    }

    void close() {
        if (st_.empty()) fail("unbalanced parenthesis", i_);
        const Frame f = st_.back();
        st_.pop_back();
        if (f.cap) closed_[f.cap] = true;
        verbose_ = f.verbose;
        caseless_ = f.caseless;
        ascii_ = f.ascii;
        pcreI_ = f.pcreI;
        t_.out += ')';
        ++i_;
        last_ = Last::Atom;
    }
};

// ---------------------------------------------------------------------------
// Compiled patterns
// ---------------------------------------------------------------------------

struct CompiledPattern {
    std::string source;  // the pattern's UTF-8 (str) or bytes
    bool isBytes = false;
    long long argFlags = 0;  // the flags argument (cache key)
    Translation tr;
    std::uint32_t options = 0;
    pcre2_code* code[3] = {nullptr, nullptr, nullptr};  // search, match, fullmatch
    std::once_flag once[2];

    CompiledPattern() = default;
    CompiledPattern(const CompiledPattern&) = delete;
    CompiledPattern& operator=(const CompiledPattern&) = delete;
    ~CompiledPattern() {
        for (pcre2_code* c : code) {
            if (c) pcre2_code_free(c);
        }
    }
};

enum Variant { kSearch = 0, kMatch = 1, kFull = 2 };

std::string pcreMessage(int err) {
    PCRE2_UCHAR buf[256];
    const int n = pcre2_get_error_message(err, buf, sizeof buf);
    return n < 0 ? "PCRE2 error " + std::to_string(err) : std::string(reinterpret_cast<char*>(buf));
}

pcre2_code* compileVariant(const CompiledPattern& cp, std::uint32_t extra, int& err) {
    pcre2_compile_context* cc = pcre2_compile_context_create(nullptr);
    if (!cc) throw std::bad_alloc();
    pcre2_set_newline(cc, PCRE2_NEWLINE_LF);
    PCRE2_SIZE off = 0;
    pcre2_code* code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(cp.tr.out.data()), cp.tr.out.size(),
                                     cp.options | extra, &err, &off, cc);
    pcre2_compile_context_free(cc);
    // Without a JIT (an unsupported platform, or one that forbids executable
    // memory) the interpreter runs the same code.
    if (code) pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
    return code;
}

// The code of a variant; nullptr only when PCRE2 rejects an anchored form of
// a pattern it accepted unanchored (not expected; reported as re.error).
const pcre2_code* variantCode(CompiledPattern& cp, Variant v, int& err) {
    err = 0;
    if (v == kSearch) return cp.code[0];
    std::call_once(cp.once[v - 1], [&] {
        int e = 0;
        cp.code[v] = compileVariant(cp, v == kMatch ? PCRE2_ANCHORED : (PCRE2_ANCHORED | PCRE2_ENDANCHORED), e);
    });
    return cp.code[v];
}

void compiledPatternFinalizer(void* p) { delete static_cast<CompiledPattern*>(p); }

// ---------------------------------------------------------------------------
// Per-environment state: attribute-name symbols
// ---------------------------------------------------------------------------

struct ReState {
    const proto::ProtoString* compiled;   // __re_compiled__
    const proto::ProtoString* string;     // string
    const proto::ProtoString* re;         // re
    const proto::ProtoString* pos;        // pos
    const proto::ProtoString* endpos;     // endpos
    const proto::ProtoString* lastindex;  // lastindex
    const proto::ProtoString* lastgroup;  // lastgroup
    const proto::ProtoString* spans;      // __re_spans__
    const proto::ProtoString* matchProto; // __match_proto__
    const proto::ProtoString* patternProto; // __pattern_proto__
    const proto::ProtoString* error;      // __re_error__
    const proto::ProtoString* cache;      // __re_cache__
    const proto::ProtoString* pattern;    // pattern
    const proto::ProtoString* flags;      // flags
    const proto::ProtoString* groups;     // groups
    const proto::ProtoString* groupindex; // groupindex
    const proto::ProtoString* scanPattern;  // __scan_pattern__
    const proto::ProtoString* scanString;   // __scan_string__
    const proto::ProtoString* scanPos;      // __scan_pos__
    const proto::ProtoString* scanEnd;      // __scan_end__
    const proto::ProtoString* scanAdvance;  // __scan_advance__
    const proto::ProtoString* scanDone;     // __scan_done__
    const proto::ProtoString* scanIterProto;  // __scan_iter_proto__
    const proto::ProtoString* match;      // match
    const proto::ProtoString* lexicon;    // lexicon
    const proto::ProtoString* scanner;    // scanner
    const proto::ProtoString* scanOuter;  // __scan_outer__
    const proto::ProtoString* scanActions;  // __scan_actions__
    const proto::ProtoObject* module = nullptr;  // the re module (mutable, rooted by the module table)
};

ReState& state(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) throw std::logic_error("re: no Python environment for this context");
    std::shared_ptr<void>& slot = env->reModuleState();
    if (!slot) {
        auto s = std::make_shared<ReState>();
        auto sym = [&](const char* n) { return proto::ProtoString::createSymbol(ctx, n); };
        s->compiled = sym("__re_compiled__");
        s->string = sym("string");
        s->re = sym("re");
        s->pos = sym("pos");
        s->endpos = sym("endpos");
        s->lastindex = sym("lastindex");
        s->lastgroup = sym("lastgroup");
        s->spans = sym("__re_spans__");
        s->matchProto = sym("__match_proto__");
        s->patternProto = sym("__pattern_proto__");
        s->error = sym("__re_error__");
        s->cache = sym("__re_cache__");
        s->pattern = sym("pattern");
        s->flags = sym("flags");
        s->groups = sym("groups");
        s->groupindex = sym("groupindex");
        s->scanPattern = sym("__scan_pattern__");
        s->scanString = sym("__scan_string__");
        s->scanPos = sym("__scan_pos__");
        s->scanEnd = sym("__scan_end__");
        s->scanAdvance = sym("__scan_advance__");
        s->scanDone = sym("__scan_done__");
        s->scanIterProto = sym("__scan_iter_proto__");
        s->match = sym("match");
        s->lexicon = sym("lexicon");
        s->scanner = sym("scanner");
        s->scanOuter = sym("__scan_outer__");
        s->scanActions = sym("__scan_actions__");
        slot = s;
    }
    return *static_cast<ReState*>(slot.get());
}

const proto::ProtoObject* attr(proto::ProtoContext* ctx, const proto::ProtoObject* o, const proto::ProtoString* name) {
    const proto::ProtoObject* v = o ? o->getAttribute(ctx, name) : nullptr;
    return v == PROTO_NONE ? nullptr : v;
}

PythonEnvironment& envOf(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) throw std::logic_error("re: no Python environment for this context");
    return *env;
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

// re.error(msg) with CPython's attributes: msg, pattern, pos, lineno, colno.
[[noreturn]] void raiseReError(proto::ProtoContext* ctx, const proto::ProtoObject* holder, const std::string& msg,
                               const proto::ProtoObject* patternObj = nullptr,
                               const std::vector<std::uint32_t>* cps = nullptr, long long pos = -1) {
    PythonEnvironment& env = envOf(ctx);
    ReState& rs = state(ctx);
    std::string full = msg;
    long long lineno = 1, colno = pos + 1;
    if (pos >= 0 && cps) {
        for (long long k = 0; k < pos && k < static_cast<long long>(cps->size()); ++k) {
            if ((*cps)[static_cast<size_t>(k)] == '\n') {
                ++lineno;
                colno = pos - k;
            }
        }
        full += " at position " + std::to_string(pos);
        if (std::find(cps->begin(), cps->end(), static_cast<std::uint32_t>('\n')) != cps->end())
            full += " (line " + std::to_string(lineno) + ", column " + std::to_string(colno) + ")";
    }
    const proto::ProtoObject* cls = attr(ctx, holder, rs.error);
    if (!cls) {
        env.raiseRuntimeError(ctx, "re.error: " + full);
        throw std::logic_error("re: raise returned");
    }
    const proto::ProtoObject* exc = env.callObject(cls, {PythonEnvironment::newStr(ctx, full)});
    if (!exc || exc == PROTO_NONE) throw std::logic_error("re: could not construct re.error");
    PythonEnvironment::TransientPin pin(&env, exc);
    proto::ProtoObject* e = const_cast<proto::ProtoObject*>(exc);
    e->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "msg"), PythonEnvironment::newStr(ctx, msg));
    e->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "pattern"), patternObj ? patternObj : PROTO_NONE);
    e->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "pos"), pos >= 0 ? ctx->fromInteger(pos) : PROTO_NONE);
    e->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "lineno"), pos >= 0 ? ctx->fromInteger(lineno) : PROTO_NONE);
    e->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "colno"), pos >= 0 ? ctx->fromInteger(colno) : PROTO_NONE);
    env.raiseException(exc);
}

// An instance of the builtin exception `name` with `msg`, raised.
[[noreturn]] void raiseBuiltin(proto::ProtoContext* ctx, const char* name, const std::string& msg) {
    PythonEnvironment& env = envOf(ctx);
    const proto::ProtoObject* cls = env.resolve(name, ctx);
    const proto::ProtoObject* exc = (cls && cls != PROTO_NONE)
        ? env.callObject(cls, {PythonEnvironment::newStr(ctx, msg)}) : nullptr;
    if (exc && exc != PROTO_NONE) env.raiseException(exc);
    env.raiseRuntimeError(ctx, msg);
    throw std::logic_error("re: raise returned");
}

[[noreturn]] void raiseTypeError(proto::ProtoContext* ctx, const std::string& msg) {
    envOf(ctx).raiseTypeError(ctx, msg);
    throw std::logic_error("re: raise returned");
}

[[noreturn]] void raiseValueError(proto::ProtoContext* ctx, const std::string& msg) {
    envOf(ctx).raiseValueError(ctx, PythonEnvironment::newStr(ctx, msg));
    throw std::logic_error("re: raise returned");
}

[[noreturn]] void raiseIndexError(proto::ProtoContext* ctx, const std::string& msg) {
    envOf(ctx).raiseIndexError(ctx, msg);
    throw std::logic_error("re: raise returned");
}

// ---------------------------------------------------------------------------
// bytes
// ---------------------------------------------------------------------------

// The octets of a bytes or bytearray object; false for anything else.
bool bytesOf(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    if (!obj || obj == PROTO_NONE || obj->isString(ctx) || obj->isInteger(ctx)) return false;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return false;
    const proto::ProtoObject* d = obj->getAttribute(ctx, env->getDataString());
    if (d && d != PROTO_NONE && d->isByteBuffer(ctx)) {
        const proto::ProtoByteBuffer* bb = d->asByteBuffer(ctx);
        if (!bb) return false;
        out.assign(bb->getBuffer(ctx), bb->getSize(ctx));
        return true;
    }
    // Other buffer objects (memoryview, array.array): their bytes, through
    // tobytes(). A copy, as protoPython has no buffer protocol.
    const proto::ProtoObject* tobytes = obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tobytes"));
    if (!tobytes || tobytes == PROTO_NONE) return false;
    const proto::ProtoObject* b = env->callMethod(obj, "tobytes", {});
    if (!b || b == PROTO_NONE || b->isString(ctx) || b->isInteger(ctx)) return false;
    const proto::ProtoObject* bd = b->getAttribute(ctx, env->getDataString());
    if (!bd || bd == PROTO_NONE || !bd->isByteBuffer(ctx)) return false;
    const proto::ProtoByteBuffer* bb = bd->asByteBuffer(ctx);
    if (!bb) return false;
    out.assign(bb->getBuffer(ctx), bb->getSize(ctx));
    return true;
}

const proto::ProtoObject* makeBytes(proto::ProtoContext* ctx, const char* data, size_t n) {
    PythonEnvironment& env = envOf(ctx);
    const proto::ProtoObject* obj = ctx->newObject(false);
    if (env.getBytesPrototype()) {
        obj = obj->addParent(ctx, env.getBytesPrototype());
        obj = obj->setAttribute(ctx, env.getClassString(), env.getBytesPrototype());
    }
    const proto::ProtoByteBuffer* bb = ctx->newByteBuffer(data, static_cast<proto::proto_ulong>(n));
    return obj->setAttribute(ctx, env.getDataString(), bb->asObject(ctx));
}

// ---------------------------------------------------------------------------
// Subjects
// ---------------------------------------------------------------------------

struct Subject {
    const proto::ProtoObject* obj = nullptr;
    bool isBytes = false;
    std::string data;
    bool ascii = true;
    std::vector<std::uint32_t> cpOff;  // byte offset of each code point, then the end (non-ASCII only)
    size_t ncp = 0;

    size_t byteAt(size_t cp) const { return ascii ? cp : cpOff[cp]; }
    size_t cpAt(size_t byte) const {
        if (ascii) return byte;
        return static_cast<size_t>(std::lower_bound(cpOff.begin(), cpOff.end(), static_cast<std::uint32_t>(byte)) -
                                   cpOff.begin());
    }
    // A piece of the subject as a Python value (str or bytes), from byte
    // offsets. The whole of a str subject is the subject itself (immutable).
    const proto::ProtoObject* piece(proto::ProtoContext* ctx, size_t a, size_t b) const {
        if (isBytes) return makeBytes(ctx, data.data() + a, b - a);
        if (a == 0 && b == data.size() && obj) return obj;
        return PythonEnvironment::newStr(ctx, data.substr(a, b - a));
    }
};

std::string typeName(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

// Reads `obj` as a subject for a pattern of the given kind; raises TypeError
// for a wrong type.
void loadSubject(proto::ProtoContext* ctx, const proto::ProtoObject* obj, bool patternIsBytes, Subject& s) {
    s.obj = obj;
    if (obj && obj->isString(ctx)) {
        if (patternIsBytes) raiseTypeError(ctx, "cannot use a bytes pattern on a string-like object");
        s.isBytes = false;
        obj->asString(ctx)->toUTF8String(ctx, s.data);
    } else if (bytesOf(ctx, obj, s.data)) {
        if (!patternIsBytes) raiseTypeError(ctx, "cannot use a string pattern on a bytes-like object");
        s.isBytes = true;
    } else {
        raiseTypeError(ctx, "expected string or bytes-like object, got '" + typeName(ctx, obj) + "'");
    }
    s.ascii = true;
    if (!s.isBytes) {
        for (unsigned char c : s.data) {
            if (c >= 0x80) {
                s.ascii = false;
                break;
            }
        }
    }
    if (s.ascii) {
        s.ncp = s.data.size();
        return;
    }
    s.cpOff.clear();
    s.cpOff.reserve(s.data.size() + 1);
    for (size_t i = 0; i < s.data.size();) {
        const size_t n = utf8SeqLen(s.data, i);
        if (n == 3 && static_cast<unsigned char>(s.data[i]) == 0xED &&
            static_cast<unsigned char>(s.data[i + 1]) >= 0xA0) {
            // PCRE2 cannot match a lone surrogate: it would be skipped as
            // invalid UTF-8, a silent difference from CPython.
            raiseValueError(ctx, "this re module cannot match strings that contain lone surrogates");
        }
        s.cpOff.push_back(static_cast<std::uint32_t>(i));
        i += n;
    }
    s.cpOff.push_back(static_cast<std::uint32_t>(s.data.size()));
    s.ncp = s.cpOff.size() - 1;
}

// Converting a str subject to UTF-8 is linear in its length, so a loop of
// pattern.match(text, pos) over one long text would be quadratic. Each
// thread therefore keeps its last long str subject converted. The cached
// object is kept alive by a slot of the re module that only this thread
// writes, so the pointer cannot be reused by another string while it is
// cached. Shorter subjects, and bytes subjects (a bytearray may change),
// are converted on every call.
constexpr size_t kCachedSubjectMinBytes = 4096;

struct SubjectCache {
    const proto::ProtoObject* obj = nullptr;
    std::shared_ptr<const Subject> subject;
    const proto::ProtoString* slot = nullptr;  // this thread's module slot
    PythonEnvironment* env = nullptr;
};

std::shared_ptr<const Subject> subjectFor(proto::ProtoContext* ctx, const proto::ProtoObject* obj, bool patternIsBytes) {
    thread_local SubjectCache cache;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (obj && obj == cache.obj && env == cache.env && cache.subject && !patternIsBytes) return cache.subject;
    auto fresh = std::make_shared<Subject>();
    loadSubject(ctx, obj, patternIsBytes, *fresh);
    ReState& rs = state(ctx);
    if (!fresh->isBytes && fresh->data.size() >= kCachedSubjectMinBytes && rs.module) {
        if (!cache.slot || cache.env != env) {
            static std::atomic<unsigned long long> nextSlot{0};
            const std::string name = "__re_subject_" + std::to_string(nextSlot.fetch_add(1)) + "__";
            cache.slot = proto::ProtoString::createSymbol(ctx, name.c_str());
        }
        const_cast<proto::ProtoObject*>(rs.module)->setAttribute(ctx, cache.slot, obj);
        cache.obj = obj;
        cache.env = env;
        cache.subject = fresh;
    }
    return fresh;
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

struct MatchState {
    pcre2_match_context* context = nullptr;
    pcre2_jit_stack* jitStack = nullptr;
    pcre2_match_data* data = nullptr;
    std::uint32_t pairs = 0;

    MatchState() {
        context = pcre2_match_context_create(nullptr);
        jitStack = pcre2_jit_stack_create(kJitStackStart, kJitStackMax, nullptr);
        if (!context || !jitStack) {
            release();
            throw std::bad_alloc();
        }
        pcre2_jit_stack_assign(context, nullptr, jitStack);
        pcre2_set_match_limit(context, kMatchLimit);
        pcre2_set_depth_limit(context, kDepthLimit);
        pcre2_set_heap_limit(context, kHeapLimitKiB);
    }
    ~MatchState() { release(); }
    MatchState(const MatchState&) = delete;
    MatchState& operator=(const MatchState&) = delete;

    void release() {
        if (data) pcre2_match_data_free(data);
        if (jitStack) pcre2_jit_stack_free(jitStack);
        if (context) pcre2_match_context_free(context);
        data = nullptr;
        jitStack = nullptr;
        context = nullptr;
    }

    pcre2_match_data* dataFor(unsigned groups) {
        if (!data || pairs < groups + 1) {
            const std::uint32_t want = std::max<std::uint32_t>(groups + 1, 16);
            pcre2_match_data* fresh = pcre2_match_data_create(want, nullptr);
            if (!fresh) throw std::bad_alloc();
            if (data) pcre2_match_data_free(data);
            data = fresh;
            pairs = want;
        }
        return data;
    }
};

MatchState& matchState() {
    thread_local MatchState st;
    return st;
}

// The spans of one match, as byte offsets (UNSET for a group that did not
// take part). A copy, so that a callback that matches again cannot change it.
struct Hit {
    std::vector<PCRE2_SIZE> ov;  // 2 * (groups + 1)
    size_t start() const { return ov[0]; }
    size_t end() const { return ov[1]; }
    bool has(unsigned g) const { return ov[2 * g] != PCRE2_UNSET; }
};

// One pcre2_match. Returns false for no match; raises for an engine limit or
// failure.
bool runMatch(proto::ProtoContext* ctx, const proto::ProtoObject* holder, CompiledPattern& cp, Variant v,
              const Subject& s, size_t startByte, size_t endByte, std::uint32_t opts, Hit& hit) {
    int err = 0;
    const pcre2_code* code = variantCode(cp, v, err);
    if (!code) raiseReError(ctx, holder, "the regular-expression engine rejected the anchored pattern");
    MatchState& ms = matchState();
    pcre2_match_data* md = ms.dataFor(cp.tr.groups);
    int rc;
    const PCRE2_SPTR subject = reinterpret_cast<PCRE2_SPTR>(s.data.data());
    if (endByte >= kUnmanagedSubjectBytes) {
        proto::ProtoContext::UnmanagedScope u(ctx);
        rc = pcre2_match(code, subject, endByte, startByte, opts, md, ms.context);
    } else {
        rc = pcre2_match(code, subject, endByte, startByte, opts, md, ms.context);
    }
    if (rc == PCRE2_ERROR_NOMATCH) return false;
    if (rc < 0) {
        if (rc == PCRE2_ERROR_JIT_STACKLIMIT || rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT ||
            rc == PCRE2_ERROR_HEAPLIMIT) {
            raiseBuiltin(ctx, "RecursionError",
                         "maximum regular-expression backtracking exceeded (" + pcreMessage(rc) + ")");
        }
        if (rc == PCRE2_ERROR_NOMEMORY) raiseBuiltin(ctx, "MemoryError", "regular-expression engine: " + pcreMessage(rc));
        raiseBuiltin(ctx, "RuntimeError", "regular-expression engine failure: " + pcreMessage(rc));
    }
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
    const size_t n = 2 * (static_cast<size_t>(cp.tr.groups) + 1);
    hit.ov.assign(ov, ov + n);
    // Groups past the highest one set are not reset by pcre2_match.
    for (size_t k = 2 * static_cast<size_t>(rc); k < n; ++k) hit.ov[k] = PCRE2_UNSET;
    return true;
}

// Calls f(hit) for each successive match in [startByte, endByte) with
// Python's rule for empty matches: after an empty match, the next one may not
// be empty at the same position (PCRE2_NOTEMPTY_ATSTART). Stops when f
// returns false.
void forEachMatch(proto::ProtoContext* ctx, const proto::ProtoObject* holder, CompiledPattern& cp,
                  const Subject& s, size_t startByte, size_t endByte, const std::function<bool(const Hit&)>& f) {
    size_t cur = startByte;
    bool afterEmpty = false;
    Hit hit;
    while (cur <= endByte) {
        if (!runMatch(ctx, holder, cp, kSearch, s, cur, endByte, afterEmpty ? PCRE2_NOTEMPTY_ATSTART : 0, hit)) break;
        if (!f(hit)) break;
        afterEmpty = hit.start() == hit.end();
        cur = hit.end();
    }
}

// ---------------------------------------------------------------------------
// Argument helpers
// ---------------------------------------------------------------------------

const proto::ProtoObject* argAt(proto::ProtoContext* ctx, const proto::ProtoArguments* pos,
                                const proto::ProtoKeywordArguments* kw, proto::proto_ulong idx, const char* name) {
    if (pos && pos->getSize(ctx) > idx) return pos->getAt(ctx, static_cast<int>(idx));
    if (kw && name && kw->getSize(ctx) > 0) {
        const proto::proto_ulong h = PythonEnvironment::getInternedString(ctx, name)->getHash(ctx);
        if (kw->has(ctx, h)) return kw->getAt(ctx, h);
    }
    return nullptr;
}

long long intArg(proto::ProtoContext* ctx, const proto::ProtoArguments* pos, const proto::ProtoKeywordArguments* kw,
                 proto::proto_ulong idx, const char* name, long long dflt) {
    const proto::ProtoObject* v = argAt(ctx, pos, kw, idx, name);
    if (!v || v == PROTO_NONE) return dflt;
    if (v == PROTO_TRUE) return 1;
    if (v == PROTO_FALSE) return 0;
    if (!v->isInteger(ctx)) raiseTypeError(ctx, std::string("'") + name + "' must be an integer");
    return v->asLong(ctx);
}

const proto::ProtoObject* requiredArg(proto::ProtoContext* ctx, const proto::ProtoArguments* pos,
                                      const proto::ProtoKeywordArguments* kw, proto::proto_ulong idx,
                                      const char* name, const char* func) {
    const proto::ProtoObject* v = argAt(ctx, pos, kw, idx, name);
    if (!v) raiseTypeError(ctx, std::string(func) + "() missing required argument '" + name + "'");
    return v;
}

// A Python list holding `items`.
const proto::ProtoObject* pyList(proto::ProtoContext* ctx, const std::vector<const proto::ProtoObject*>& items) {
    const proto::ProtoList* l = items.empty()
        ? ctx->newList() : ctx->newList(static_cast<unsigned>(items.size()), items.data());
    return PythonEnvironment::wrapList(ctx, l);
}

const proto::ProtoObject* pyTuple(proto::ProtoContext* ctx, const std::vector<const proto::ProtoObject*>& items) {
    const proto::ProtoList* l = items.empty()
        ? ctx->newList() : ctx->newList(static_cast<unsigned>(items.size()), items.data());
    return PythonEnvironment::tupleFromList(ctx, l);
}

// ---------------------------------------------------------------------------
// Pattern objects and the compile cache
// ---------------------------------------------------------------------------

CompiledPattern* compiledOf(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || obj->isString(ctx) || obj->isInteger(ctx)) return nullptr;
    const proto::ProtoObject* ext = obj->getAttribute(ctx, state(ctx).compiled);
    if (!ext || ext == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* p = ext->asExternalPointer(ctx);
    return p ? static_cast<CompiledPattern*>(p->getPointer(ctx)) : nullptr;
}

const proto::ProtoObject* makeDict(proto::ProtoContext* ctx) {
    PythonEnvironment& env = envOf(ctx);
    const proto::ProtoObject* d = env.getDictPrototype() ? env.getDictPrototype()->newChild(ctx, true)
                                                          : ctx->newObject(true);
    return env.initDictStorage(ctx, d);
}

// Compiles `source` (a str or bytes object) into a new Pattern object.
// `holder` reaches the module's prototypes and error class.
const proto::ProtoObject* compileNew(proto::ProtoContext* ctx, const proto::ProtoObject* holder,
                                     const proto::ProtoObject* sourceObj, bool isBytes, std::string&& src,
                                     long long flags) {
    PythonEnvironment& env = envOf(ctx);
    ReState& rs = state(ctx);
    auto cp = std::make_unique<CompiledPattern>();
    cp->source = std::move(src);
    cp->isBytes = isBytes;
    cp->argFlags = flags;
    std::vector<std::uint32_t> cps;
    if (isBytes) {
        cps.assign(cp->source.begin(), cp->source.end());
        for (auto& c : cps) c &= 0xFF;
    } else {
        decodeCodePoints(cp->source, cps);
    }
    try {
        Translator t(cps, isBytes, flags & ~(F_DEBUG | F_TEMPLATE));
        cp->tr = t.run();
    } catch (const PatternError& e) {
        if (e.overflow) raiseBuiltin(ctx, "OverflowError", e.msg);
        raiseReError(ctx, holder, e.msg, sourceObj, &cps, static_cast<long long>(e.pos));
    }
    // The flags argument together with the leading inline flags, checked as
    // CPython's fix_flags checks them (ValueError); a str pattern is
    // re.UNICODE unless it is re.ASCII.
    long long& eff = cp->tr.flags;
    if (isBytes) {
        if (eff & F_U) raiseValueError(ctx, "cannot use UNICODE flag with a bytes pattern");
        if ((eff & F_A) && (eff & F_L)) raiseValueError(ctx, "ASCII and LOCALE flags are incompatible");
    } else {
        if (eff & F_L) raiseValueError(ctx, "cannot use LOCALE flag with a str pattern");
        if ((eff & F_A) && (eff & F_U)) raiseValueError(ctx, "ASCII and UNICODE flags are incompatible");
        if (!(eff & F_A)) eff |= F_U;
    }
    std::uint32_t opt = PCRE2_ALT_CIRCUMFLEX | PCRE2_NEVER_BACKSLASH_C;
    if (!isBytes) {
        opt |= PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
        if (!(cp->tr.flags & F_A)) opt |= PCRE2_UCP;
    }
    if ((cp->tr.flags & F_I) && (isBytes || !(cp->tr.flags & F_A))) opt |= PCRE2_CASELESS;
    if (cp->tr.flags & F_M) opt |= PCRE2_MULTILINE;
    if (cp->tr.flags & F_S) opt |= PCRE2_DOTALL;
    cp->options = opt;
    int err = 0;
    cp->code[0] = compileVariant(*cp, 0, err);
    if (!cp->code[0]) raiseReError(ctx, holder, "the regular-expression engine rejected the pattern: " + pcreMessage(err), sourceObj);

    const proto::ProtoObject* proto = attr(ctx, holder, rs.patternProto);
    if (!proto) throw std::logic_error("re: no Pattern prototype");
    // groupindex: {name: number}
    const proto::ProtoObject* gi = makeDict(ctx);
    PythonEnvironment::TransientPin pinGi(&env, gi);
    for (const auto& kv : cp->tr.names) {
        env.callMethod(gi, "__setitem__", {PythonEnvironment::newStr(ctx, kv.first),
                                           ctx->fromInteger(static_cast<long long>(kv.second))});
    }
    // The pattern text is user data, not vocabulary: a collectable copy.
    const proto::ProtoObject* patText = isBytes ? makeBytes(ctx, cp->source.data(), cp->source.size())
                                                : PythonEnvironment::newStr(ctx, cp->source);
    const long long effFlags = cp->tr.flags;
    const unsigned groups = cp->tr.groups;
    const proto::ProtoObject* ext = ctx->fromExternalPointer(cp.release(), compiledPatternFinalizer);
    const proto::ProtoString* names[6] = {rs.compiled, rs.pattern, rs.flags, rs.groups, rs.groupindex,
                                          env.getClassString()};
    const proto::ProtoObject* values[6] = {ext, patText, ctx->fromInteger(effFlags),
                                           ctx->fromInteger(static_cast<long long>(groups)), gi, proto};
    // Immutable: nothing assigns to a pattern. (Measured against a mutable
    // pattern: instructions per search within 1 %, immutable slightly fewer.)
    return proto->newChild(ctx, false)->setAttributes(ctx, 6, names, values);
}

std::uint64_t cacheKey(bool isBytes, long long flags, const std::string& src) {
    std::uint64_t h = std::hash<std::string>{}(src);
    h ^= static_cast<std::uint64_t>(flags) * 0x9E3779B97F4A7C15ull + (isBytes ? 0x51ull : 0x17ull);
    return h;
}

// The Pattern object for a module-level call's pattern argument: a compiled
// pattern as given (flags must then be 0), otherwise compiled through the
// module's cache. `module` is the re module object.
const proto::ProtoObject* obtainPattern(proto::ProtoContext* ctx, const proto::ProtoObject* module,
                                        const proto::ProtoObject* patArg, long long flags) {
    if (compiledOf(ctx, patArg)) {
        if (flags & ~F_TEMPLATE) raiseValueError(ctx, "cannot process flags argument with a compiled pattern");
        return patArg;
    }
    ReState& rs = state(ctx);
    const proto::ProtoObject* cacheObj = attr(ctx, module, rs.cache);
    const proto::ProtoSparseList* cache = cacheObj ? cacheObj->asSparseList(ctx) : nullptr;
    std::string src;
    bool isBytes = false;
    std::uint64_t key = 0;
    if (patArg && patArg->isString(ctx)) {
        // A str pattern is looked up by protoCore's content hash and
        // compared with the cached pattern's text, without converting it.
        const proto::ProtoString* text = patArg->asString(ctx);
        key = (static_cast<std::uint64_t>(patArg->getHash(ctx)) * 0x9E3779B97F4A7C15ull) ^
              (static_cast<std::uint64_t>(flags) << 1);
        if (cache && cache->has(ctx, key)) {
            const proto::ProtoObject* hit = cache->getAt(ctx, key);
            CompiledPattern* cp = compiledOf(ctx, hit);
            const proto::ProtoObject* hitText = attr(ctx, hit, rs.pattern);
            if (cp && !cp->isBytes && cp->argFlags == flags && hitText && hitText->isString(ctx) &&
                text->cmp_to_string(ctx, hitText->asString(ctx)) == 0)
                return hit;
        }
        text->toUTF8String(ctx, src);
    } else if (bytesOf(ctx, patArg, src)) {
        isBytes = true;
        key = cacheKey(true, flags, src);
        if (cache && cache->has(ctx, key)) {
            const proto::ProtoObject* hit = cache->getAt(ctx, key);
            CompiledPattern* cp = compiledOf(ctx, hit);
            if (cp && cp->isBytes && cp->argFlags == flags && cp->source == src) return hit;
        }
    } else {
        raiseTypeError(ctx, "first argument must be string or compiled pattern");
    }
    const proto::ProtoObject* pat = compileNew(ctx, module, patArg, isBytes, std::move(src), flags);
    if (!cache || cache->getSize(ctx) >= kCacheSize) cache = ctx->newSparseList();
    cache = cache->setAt(ctx, key, pat);
    const_cast<proto::ProtoObject*>(module)->setAttribute(ctx, rs.cache, cache->asObject(ctx));
    return pat;
}

// ---------------------------------------------------------------------------
// Match objects
//
// A Match carries `string`, `re`, `pos`, `endpos`, `lastindex`, `lastgroup`
// and `__re_spans__` (a flat list of code point offsets, -1 for a group that
// did not take part); group strings are sliced from the subject on demand.
// ---------------------------------------------------------------------------

// lastindex: the group that closed last. Among the participating groups that
// end furthest, an enclosing group closes after the groups inside it, and of
// two groups side by side the later one closes last.
unsigned lastIndexOf(const CompiledPattern& cp, const Hit& hit) {
    unsigned best = 0;
    for (unsigned g = 1; g <= cp.tr.groups; ++g) {
        if (!hit.has(g)) continue;
        if (best == 0 || hit.ov[2 * g + 1] > hit.ov[2 * best + 1]) {
            best = g;
            continue;
        }
        if (hit.ov[2 * g + 1] < hit.ov[2 * best + 1]) continue;
        // Same end. Is `best` inside g (g encloses it)? Then g closed later.
        bool gEnclosesBest = false;
        for (unsigned a = cp.tr.parent[best]; a; a = cp.tr.parent[a]) {
            if (a == g) gEnclosesBest = true;
        }
        bool bestEnclosesG = false;
        for (unsigned a = cp.tr.parent[g]; a; a = cp.tr.parent[a]) {
            if (a == best) bestEnclosesG = true;
        }
        if (gEnclosesBest || !bestEnclosesG) best = g;
    }
    return best;
}

const proto::ProtoObject* makeMatch(proto::ProtoContext* ctx, const proto::ProtoObject* patObj, CompiledPattern& cp,
                                    const Subject& s, size_t pos, size_t endpos, const Hit& hit,
                                    int lastIndexOverride = -1, unsigned groupsOverride = 0) {
    ReState& rs = state(ctx);
    const proto::ProtoObject* matchProto = attr(ctx, patObj, rs.matchProto);
    if (!matchProto) throw std::logic_error("re: no Match prototype");
    // The match's group count is the length of its span list.
    const unsigned ngroups = groupsOverride ? std::min(groupsOverride, cp.tr.groups) : cp.tr.groups;
    std::vector<const proto::ProtoObject*> spans;
    spans.reserve(2 * (ngroups + 1));
    for (unsigned g = 0; g <= ngroups; ++g) {
        if (hit.has(g)) {
            spans.push_back(ctx->fromInteger(static_cast<long long>(s.cpAt(hit.ov[2 * g]))));
            spans.push_back(ctx->fromInteger(static_cast<long long>(s.cpAt(hit.ov[2 * g + 1]))));
        } else {
            spans.push_back(ctx->fromInteger(-1));
            spans.push_back(ctx->fromInteger(-1));
        }
    }
    const unsigned li = lastIndexOverride >= 0 ? static_cast<unsigned>(lastIndexOverride) : lastIndexOf(cp, hit);
    const proto::ProtoObject* lastgroup = PROTO_NONE;
    if (li) {
        for (const auto& kv : cp.tr.names) {
            if (kv.second == li) lastgroup = PythonEnvironment::newStr(ctx, kv.first);
        }
    }
    const proto::ProtoList* spanList = ctx->newList(static_cast<unsigned>(spans.size()), spans.data());
    const proto::ProtoString* names[7] = {rs.string, rs.re, rs.pos, rs.endpos, rs.lastindex, rs.lastgroup, rs.spans};
    const proto::ProtoObject* values[7] = {s.obj, patObj,
                                           ctx->fromInteger(static_cast<long long>(pos)),
                                           ctx->fromInteger(static_cast<long long>(endpos)),
                                           li ? ctx->fromInteger(static_cast<long long>(li)) : PROTO_NONE,
                                           lastgroup, spanList->asObject(ctx)};
    // Immutable: a Match takes no attribute assignment, and an immutable
    // object costs no entry in the mutable-object table.
    return matchProto->newChild(ctx, false)->setAttributes(ctx, 7, names, values);
}

struct MatchView {
    const proto::ProtoObject* self;
    const proto::ProtoObject* subject;
    const proto::ProtoList* spans;
    CompiledPattern* cp;
    unsigned ngroups;  // from the span list (a Scanner's match has fewer than its pattern)
};

MatchView viewOf(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    ReState& rs = state(ctx);
    MatchView v{self, attr(ctx, self, rs.string), nullptr, nullptr, 0};
    const proto::ProtoObject* sp = attr(ctx, self, rs.spans);
    v.spans = sp ? sp->asList(ctx) : nullptr;
    v.cp = compiledOf(ctx, attr(ctx, self, rs.re));
    if (!v.subject || !v.spans || !v.cp) raiseTypeError(ctx, "not a re.Match object");
    v.ngroups = static_cast<unsigned>(v.spans->getSize(ctx) / 2) - 1;
    return v;
}

// The group number of a reference (an int or a group name); IndexError for
// no such group.
unsigned groupIndex(proto::ProtoContext* ctx, const MatchView& m, const proto::ProtoObject* ref) {
    if (!ref) return 0;
    if (ref->isInteger(ctx)) {
        // A LargeInteger (a cell, not an embedded value) names no group.
        if (ref->isCell(ctx)) raiseIndexError(ctx, "no such group");
        const long long g = ref->asLong(ctx);
        if (g < 0 || g > static_cast<long long>(m.ngroups)) raiseIndexError(ctx, "no such group");
        return static_cast<unsigned>(g);
    }
    if (ref->isString(ctx)) {
        std::string name;
        ref->asString(ctx)->toUTF8String(ctx, name);
        for (const auto& kv : m.cp->tr.names) {
            if (kv.first == name && kv.second <= m.ngroups) return kv.second;
        }
        raiseIndexError(ctx, "no such group");
    }
    // An object with __index__ stands for its integer.
    const proto::ProtoObject* index = ref->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__index__"));
    if (index && index != PROTO_NONE) {
        const proto::ProtoObject* v = envOf(ctx).callMethod(ref, "__index__", {});
        if (v && v != PROTO_NONE && v->isInteger(ctx)) return groupIndex(ctx, m, v);
    }
    raiseIndexError(ctx, "no such group");
}

void spanOf(proto::ProtoContext* ctx, const MatchView& m, unsigned g, long long& a, long long& b) {
    a = m.spans->getAt(ctx, static_cast<int>(2 * g))->asLong(ctx);
    b = m.spans->getAt(ctx, static_cast<int>(2 * g + 1))->asLong(ctx);
}

// The text of group g, or `dflt` when it did not take part.
const proto::ProtoObject* groupValue(proto::ProtoContext* ctx, const MatchView& m, unsigned g,
                                     const proto::ProtoObject* dflt) {
    long long a, b;
    spanOf(ctx, m, g, a, b);
    if (a < 0) return dflt;
    if (m.subject->isString(ctx)) {
        return m.subject->asString(ctx)->getSlice(ctx, static_cast<int>(a), static_cast<int>(b))->asObject(ctx);
    }
    std::string data;
    bytesOf(ctx, m.subject, data);
    // A bytearray may have shrunk since the match: CPython slices what is
    // there now.
    const size_t lo = std::min(static_cast<size_t>(a), data.size());
    const size_t hi = std::min(static_cast<size_t>(b), data.size());
    return makeBytes(ctx, data.data() + lo, hi - lo);
}

// ---------------------------------------------------------------------------
// Replacement templates
// ---------------------------------------------------------------------------

struct TemplatePiece {
    bool isGroup = false;
    unsigned group = 0;
    std::string text;  // UTF-8 (str) or bytes
};

std::vector<TemplatePiece> parseTemplate(proto::ProtoContext* ctx, const proto::ProtoObject* holder,
                                         const std::string& raw, bool isBytes, const CompiledPattern& cp,
                                         const proto::ProtoObject* tmplObj) {
    std::vector<std::uint32_t> t;
    if (isBytes) {
        t.assign(raw.begin(), raw.end());
        for (auto& c : t) c &= 0xFF;
    } else {
        decodeCodePoints(raw, t);
    }
    std::vector<TemplatePiece> out;
    std::string lit;
    auto put = [&](std::uint32_t c) {
        if (isBytes) lit += static_cast<char>(c);
        else appendUtf8(lit, c);
    };
    auto flush = [&] {
        if (!lit.empty()) {
            TemplatePiece p;
            p.text = std::move(lit);
            out.push_back(std::move(p));
            lit.clear();
        }
    };
    auto group = [&](unsigned g, size_t pos) {
        if (g > cp.tr.groups) raiseReError(ctx, holder, "invalid group reference " + std::to_string(g), tmplObj, &t, static_cast<long long>(pos));
        flush();
        TemplatePiece p;
        p.isGroup = true;
        p.group = g;
        out.push_back(std::move(p));
    };
    const size_t n = t.size();
    for (size_t i = 0; i < n; ++i) {
        const std::uint32_t c = t[i];
        if (c != '\\') {
            put(c);
            continue;
        }
        const size_t s = i;
        if (i + 1 >= n) raiseReError(ctx, holder, "bad escape (end of pattern)", tmplObj, &t, static_cast<long long>(s));
        const std::uint32_t e = t[++i];
        if (e == 'g') {
            if (i + 1 >= n || t[i + 1] != '<') raiseReError(ctx, holder, "missing <", tmplObj, &t, static_cast<long long>(i + 1));
            size_t j = i + 2;
            while (j < n && t[j] != '>') ++j;
            if (j == i + 2) raiseReError(ctx, holder, "missing group name", tmplObj, &t, static_cast<long long>(i + 2));
            if (j >= n) raiseReError(ctx, holder, "missing >, unterminated name", tmplObj, &t, static_cast<long long>(i + 2));
            std::string name, shown;
            bool digits = true;
            for (size_t k = i + 2; k < j; ++k) {
                if (isBytes) {
                    name += static_cast<char>(t[k]);
                    if (t[k] < 0x80) {
                        shown += static_cast<char>(t[k]);
                    } else {
                        char b[8];
                        std::snprintf(b, sizeof b, "\\x%02x", static_cast<unsigned>(t[k]));
                        shown += b;
                    }
                } else {
                    appendUtf8(name, t[k]);
                }
                digits = digits && isAsciiDigit(t[k]);
            }
            if (!isBytes) shown = name;
            const size_t namePos = i + 2;
            i = j;
            if (digits) {
                if (name.size() > 9) raiseReError(ctx, holder, "invalid group reference " + name, tmplObj, &t, static_cast<long long>(namePos));
                group(static_cast<unsigned>(std::stoul(name)), namePos);
                continue;
            }
            int idx = -1;
            for (const auto& kv : cp.tr.names) {
                if (kv.first == name) idx = static_cast<int>(kv.second);
            }
            if (!isIdentifier(name, isBytes)) raiseReError(ctx, holder, "bad character in group name '" + shown + "'", tmplObj, &t, static_cast<long long>(namePos));
            if (idx < 0) raiseBuiltin(ctx, "IndexError", "unknown group name '" + name + "'");
            group(static_cast<unsigned>(idx), namePos);
            continue;
        }
        if (e == '0') {
            std::uint32_t v = 0;
            for (int k = 0; k < 2 && i + 1 < n && isOctDigit(t[i + 1]); ++k) v = v * 8 + (t[++i] - '0');
            put(v);
            continue;
        }
        if (isAsciiDigit(e)) {
            if (i + 2 < n && isOctDigit(e) && isOctDigit(t[i + 1]) && isOctDigit(t[i + 2])) {
                const std::uint32_t v = (e - '0') * 64 + (t[i + 1] - '0') * 8 + (t[i + 2] - '0');
                if (v > 0377) raiseReError(ctx, holder, "octal escape value \\" + std::to_string(e - '0') + std::to_string(t[i + 1] - '0') + std::to_string(t[i + 2] - '0') + " outside of range 0-0o377", tmplObj, &t, static_cast<long long>(s));
                i += 2;
                put(v);
                continue;
            }
            unsigned g = e - '0';
            if (i + 1 < n && isAsciiDigit(t[i + 1])) g = g * 10 + (t[++i] - '0');
            group(g, s + 1);
            continue;
        }
        switch (e) {
            case 'a': put(0x07); continue;
            case 'b': put(0x08); continue;
            case 'f': put(0x0c); continue;
            case 'n': put(0x0a); continue;
            case 'r': put(0x0d); continue;
            case 't': put(0x09); continue;
            case 'v': put(0x0b); continue;
            case '\\': put('\\'); continue;
            default: break;
        }
        if (e < 0x80 && isAsciiLetter(e)) raiseReError(ctx, holder, std::string("bad escape \\") + static_cast<char>(e), tmplObj, &t, static_cast<long long>(s));
        put('\\');
        put(e);
    }
    flush();
    return out;
}

void expandInto(std::string& out, const std::vector<TemplatePiece>& tmpl, const Subject& s, const Hit& hit) {
    for (const auto& p : tmpl) {
        if (!p.isGroup) {
            out += p.text;
        } else if (hit.has(p.group)) {
            out.append(s.data, hit.ov[2 * p.group], hit.ov[2 * p.group + 1] - hit.ov[2 * p.group]);
        }
    }
}

// ---------------------------------------------------------------------------
// The operations
// ---------------------------------------------------------------------------

// The [pos, endpos) window as code points, clamped as CPython clamps it.
void window(proto::ProtoContext* ctx, const proto::ProtoArguments* pos, const proto::ProtoKeywordArguments* kw,
            proto::proto_ulong first, size_t size, size_t& p, size_t& e) {
    long long a = intArg(ctx, pos, kw, first, "pos", 0);
    long long b = intArg(ctx, pos, kw, first + 1, "endpos", static_cast<long long>(size));
    if (a < 0) a = 0;
    if (a > static_cast<long long>(size)) a = static_cast<long long>(size);
    if (b < 0) b = 0;
    if (b > static_cast<long long>(size)) b = static_cast<long long>(size);
    p = static_cast<size_t>(a);
    e = static_cast<size_t>(b);
}

const proto::ProtoObject* doMatch(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                  const proto::ProtoObject* subjectObj, Variant v, const proto::ProtoArguments* pos,
                                  const proto::ProtoKeywordArguments* kw, proto::proto_ulong windowFirst, bool hasWindow) {
    CompiledPattern* cp = compiledOf(ctx, patObj);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    size_t p = 0, e = s.ncp;
    if (hasWindow) window(ctx, pos, kw, windowFirst, s.ncp, p, e);
    if (p > e) return PROTO_NONE;
    Hit hit;
    if (!runMatch(ctx, patObj, *cp, v, s, s.byteAt(p), s.byteAt(e), 0, hit)) return PROTO_NONE;
    return makeMatch(ctx, patObj, *cp, s, p, e, hit);
}

const proto::ProtoObject* doFindAll(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                    const proto::ProtoObject* subjectObj, const proto::ProtoArguments* pos,
                                    const proto::ProtoKeywordArguments* kw, proto::proto_ulong windowFirst, bool hasWindow) {
    CompiledPattern* cp = compiledOf(ctx, patObj);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    size_t p = 0, e = s.ncp;
    if (hasWindow) window(ctx, pos, kw, windowFirst, s.ncp, p, e);
    std::vector<const proto::ProtoObject*> items;
    if (p <= e) {
        const unsigned groups = cp->tr.groups;
        forEachMatch(ctx, patObj, *cp, s, s.byteAt(p), s.byteAt(e), [&](const Hit& h) {
            auto text = [&](unsigned g) {
                return h.has(g) ? s.piece(ctx, h.ov[2 * g], h.ov[2 * g + 1]) : s.piece(ctx, 0, 0);
            };
            if (groups == 0) {
                items.push_back(text(0));
            } else if (groups == 1) {
                items.push_back(text(1));
            } else {
                std::vector<const proto::ProtoObject*> t;
                for (unsigned g = 1; g <= groups; ++g) t.push_back(text(g));
                items.push_back(pyTuple(ctx, t));
            }
            return true;
        });
    }
    return pyList(ctx, items);
}

const proto::ProtoObject* doFindIter(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                     const proto::ProtoObject* subjectObj, const proto::ProtoArguments* pos,
                                     const proto::ProtoKeywordArguments* kw, proto::proto_ulong windowFirst, bool hasWindow) {
    CompiledPattern* cp = compiledOf(ctx, patObj);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    size_t p = 0, e = s.ncp;
    if (hasWindow) window(ctx, pos, kw, windowFirst, s.ncp, p, e);
    std::vector<const proto::ProtoObject*> items;
    if (p <= e) {
        forEachMatch(ctx, patObj, *cp, s, s.byteAt(p), s.byteAt(e), [&](const Hit& h) {
            items.push_back(makeMatch(ctx, patObj, *cp, s, p, e, h));
            return true;
        });
    }
    // An iterator, as CPython's finditer returns (the matches are found
    // eagerly).
    PythonEnvironment& env = envOf(ctx);
    const proto::ProtoObject* list = pyList(ctx, items);
    PythonEnvironment::TransientPin pin(&env, list);
    const proto::ProtoObject* iterFn = env.resolve("iter", ctx);
    if (!iterFn || iterFn == PROTO_NONE) return list;
    return env.callObject(iterFn, {list});
}

const proto::ProtoObject* doSplit(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                  const proto::ProtoObject* subjectObj, long long maxsplit) {
    CompiledPattern* cp = compiledOf(ctx, patObj);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    std::vector<const proto::ProtoObject*> items;
    size_t last = 0;
    long long splits = 0;
    if (maxsplit >= 0) {
        forEachMatch(ctx, patObj, *cp, s, 0, s.data.size(), [&](const Hit& h) {
            if (maxsplit > 0 && splits >= maxsplit) return false;
            items.push_back(s.piece(ctx, last, h.start()));
            for (unsigned g = 1; g <= cp->tr.groups; ++g) {
                items.push_back(h.has(g) ? s.piece(ctx, h.ov[2 * g], h.ov[2 * g + 1]) : PROTO_NONE);
            }
            last = h.end();
            ++splits;
            return true;
        });
    }
    items.push_back(s.piece(ctx, last, s.data.size()));
    return pyList(ctx, items);
}

bool isCallable(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    PythonEnvironment& env = envOf(ctx);
    const proto::ProtoObject* fn = env.resolve("callable", ctx);
    if (!fn || fn == PROTO_NONE) return false;
    return env.callObject(fn, {obj}) == PROTO_TRUE;
}

std::string typeName(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    PythonEnvironment& env = envOf(ctx);
    std::string name = "object";
    const proto::ProtoObject* cls = env.getType(ctx, obj);
    const proto::ProtoObject* nm = cls ? cls->getAttribute(ctx, env.getNameString()) : nullptr;
    if (nm && nm != PROTO_NONE && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, name);
    return name;
}

// sub / subn: the new string and the number of replacements.
std::pair<const proto::ProtoObject*, long long> doSub(proto::ProtoContext* ctx, const proto::ProtoObject* patObj,
                                                      const proto::ProtoObject* repl, const proto::ProtoObject* subjectObj,
                                                      long long count) {
    PythonEnvironment& env = envOf(ctx);
    CompiledPattern* cp = compiledOf(ctx, patObj);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    std::string raw;
    bool isTemplate = false;
    // A replacement of the other type is an error only once a match needs
    // it, reported as CPython's join reports it ("sequence item N: ...").
    std::string mismatch;
    if (repl && repl->isString(ctx)) {
        if (cp->isBytes) mismatch = "expected a bytes-like object, str found";
        repl->asString(ctx)->toUTF8String(ctx, raw);
        isTemplate = true;
    } else if (bytesOf(ctx, repl, raw)) {
        if (!cp->isBytes) mismatch = "expected str instance, bytes found";
        isTemplate = true;
    } else if (!isCallable(ctx, repl)) {
        raiseTypeError(ctx, cp->isBytes ? "expected a bytes-like object, " + typeName(ctx, repl) + " found"
                                        : "decoding to str: need a bytes-like object, " + typeName(ctx, repl) + " found");
    }
    std::vector<TemplatePiece> tmpl;
    if (isTemplate && mismatch.empty()) {
        if (raw.find('\\') == std::string::npos) {
            TemplatePiece piece;
            piece.text = raw;
            tmpl.push_back(std::move(piece));
        } else {
            tmpl = parseTemplate(ctx, patObj, raw, cp->isBytes, *cp, repl);
        }
    }
    std::string out;
    out.reserve(s.data.size());
    long long n = 0;
    size_t last = 0;
    long long items = 0;  // the pieces CPython would have joined so far
    if (count >= 0) {
        forEachMatch(ctx, patObj, *cp, s, 0, s.data.size(), [&](const Hit& h) {
            if (count > 0 && n >= count) return false;
            if (h.start() > last) ++items;
            out.append(s.data, last, h.start() - last);
            if (!mismatch.empty()) raiseTypeError(ctx, "sequence item " + std::to_string(items) + ": " + mismatch);
            if (isTemplate) {
                expandInto(out, tmpl, s, h);
            } else {
                const proto::ProtoObject* mo = makeMatch(ctx, patObj, *cp, s, 0, s.ncp, h);
                PythonEnvironment::TransientPin pinMatch(&env, mo);
                const proto::ProtoObject* r = env.callObject(repl, {mo});
                std::string piece;
                if (!r || r == PROTO_NONE || r == env.getNonePrototype()) {
                    --items;  // None: nothing is added, as CPython
                } else if (!cp->isBytes && r->isString(ctx)) {
                    r->asString(ctx)->toUTF8String(ctx, piece);
                } else if (cp->isBytes && bytesOf(ctx, r, piece)) {
                    // bytes result
                } else {
                    const char* want = cp->isBytes ? "a bytes-like object" : "str instance";
                    raiseTypeError(ctx, "sequence item " + std::to_string(items) + ": expected " + want + ", " +
                                            typeName(ctx, r) + " found");
                }
                out += piece;
            }
            last = h.end();
            ++n;
            ++items;
            return true;
        });
    }
    if (n == 0 && !s.isBytes) return {s.obj, 0};  // no replacement: the subject itself, as CPython
    out.append(s.data, last, std::string::npos);
    return {s.isBytes ? makeBytes(ctx, out.data(), out.size()) : PythonEnvironment::newStr(ctx, out), n};
}

// ---------------------------------------------------------------------------
// Match methods
// ---------------------------------------------------------------------------

#define RE_METHOD(name)                                                                          \
    const proto::ProtoObject* name(proto::ProtoContext* ctx, const proto::ProtoObject* self,     \
                                   const proto::ParentLink*, const proto::ProtoArguments* posArgs, \
                                   const proto::ProtoKeywordArguments* kwArgs)

RE_METHOD(py_match_group) {
    (void)kwArgs;
    const MatchView m = viewOf(ctx, self);
    const proto::proto_ulong n = posArgs ? posArgs->getSize(ctx) : 0;
    if (n == 0) return groupValue(ctx, m, 0, PROTO_NONE);
    if (n == 1) return groupValue(ctx, m, groupIndex(ctx, m, posArgs->getAt(ctx, 0)), PROTO_NONE);
    std::vector<const proto::ProtoObject*> items;
    for (proto::proto_ulong i = 0; i < n; ++i) {
        items.push_back(groupValue(ctx, m, groupIndex(ctx, m, posArgs->getAt(ctx, static_cast<int>(i))), PROTO_NONE));
    }
    return pyTuple(ctx, items);
}

RE_METHOD(py_match_getitem) {
    (void)kwArgs;
    const MatchView m = viewOf(ctx, self);
    if (!posArgs || posArgs->getSize(ctx) < 1) raiseTypeError(ctx, "__getitem__ expected 1 argument");
    return groupValue(ctx, m, groupIndex(ctx, m, posArgs->getAt(ctx, 0)), PROTO_NONE);
}

RE_METHOD(py_match_groups) {
    const MatchView m = viewOf(ctx, self);
    const proto::ProtoObject* dflt = argAt(ctx, posArgs, kwArgs, 0, "default");
    if (!dflt) dflt = PROTO_NONE;
    std::vector<const proto::ProtoObject*> items;
    for (unsigned g = 1; g <= m.ngroups; ++g) items.push_back(groupValue(ctx, m, g, dflt));
    return pyTuple(ctx, items);
}

RE_METHOD(py_match_groupdict) {
    PythonEnvironment& env = envOf(ctx);
    const MatchView m = viewOf(ctx, self);
    const proto::ProtoObject* dflt = argAt(ctx, posArgs, kwArgs, 0, "default");
    if (!dflt) dflt = PROTO_NONE;
    const proto::ProtoObject* d = makeDict(ctx);
    PythonEnvironment::TransientPin pin(&env, d);
    for (const auto& kv : m.cp->tr.names) {
        if (kv.second > m.ngroups) continue;
        const proto::ProtoObject* key = PythonEnvironment::newStr(ctx, kv.first);
        PythonEnvironment::TransientPin pinKey(&env, key);
        const proto::ProtoObject* val = groupValue(ctx, m, kv.second, dflt);
        PythonEnvironment::TransientPin pinVal(&env, val);
        env.callMethod(d, "__setitem__", {key, val});
    }
    return d;
}

void boundsArgs(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ProtoArguments* posArgs,
                long long& a, long long& b) {
    const MatchView m = viewOf(ctx, self);
    const proto::ProtoObject* ref = (posArgs && posArgs->getSize(ctx) >= 1) ? posArgs->getAt(ctx, 0) : nullptr;
    spanOf(ctx, m, groupIndex(ctx, m, ref), a, b);
}

RE_METHOD(py_match_start) {
    (void)kwArgs;
    long long a, b;
    boundsArgs(ctx, self, posArgs, a, b);
    return ctx->fromInteger(a);
}

RE_METHOD(py_match_end) {
    (void)kwArgs;
    long long a, b;
    boundsArgs(ctx, self, posArgs, a, b);
    return ctx->fromInteger(b);
}

RE_METHOD(py_match_span) {
    (void)kwArgs;
    long long a, b;
    boundsArgs(ctx, self, posArgs, a, b);
    return pyTuple(ctx, {ctx->fromInteger(a), ctx->fromInteger(b)});
}

RE_METHOD(py_match_expand) {
    const MatchView m = viewOf(ctx, self);
    const proto::ProtoObject* tmplObj = requiredArg(ctx, posArgs, kwArgs, 0, "template", "expand");
    std::string raw;
    const bool isBytes = !m.subject->isString(ctx);
    if (isBytes ? !bytesOf(ctx, tmplObj, raw) : !tmplObj->isString(ctx))
        raiseTypeError(ctx, isBytes ? "expected a bytes-like object" : "expected str instance");
    if (!isBytes) tmplObj->asString(ctx)->toUTF8String(ctx, raw);
    const std::vector<TemplatePiece> tmpl = parseTemplate(ctx, self, raw, isBytes, *m.cp, tmplObj);
    // Rebuild the byte spans from the code point spans.
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, m.subject, isBytes);
    const Subject& s = *sref;
    Hit hit;
    hit.ov.assign(2 * (static_cast<size_t>(m.cp->tr.groups) + 1), PCRE2_UNSET);
    for (unsigned g = 0; g <= m.ngroups; ++g) {
        long long a, b;
        spanOf(ctx, m, g, a, b);
        hit.ov[2 * g] = a < 0 ? PCRE2_UNSET : s.byteAt(static_cast<size_t>(a));
        hit.ov[2 * g + 1] = a < 0 ? PCRE2_UNSET : s.byteAt(static_cast<size_t>(b));
    }
    std::string out;
    expandInto(out, tmpl, s, hit);
    return isBytes ? makeBytes(ctx, out.data(), out.size()) : PythonEnvironment::newStr(ctx, out);
}

RE_METHOD(py_match_repr) {
    (void)posArgs;
    (void)kwArgs;
    const MatchView m = viewOf(ctx, self);
    long long a, b;
    spanOf(ctx, m, 0, a, b);
    const std::string text = "<re.Match object; span=(" + std::to_string(a) + ", " + std::to_string(b) +
                             "), match=" + PythonEnvironment::reprObject(ctx, groupValue(ctx, m, 0, PROTO_NONE)) + ">";
    return PythonEnvironment::newStr(ctx, text);
}

// __copy__ / __deepcopy__: patterns and matches are immutable, a copy is
// the object itself. copy.copy calls it on the class with the object as its
// argument; copy.deepcopy on the object with the memo.
RE_METHOD(py_identity) {
    (void)kwArgs;
    ReState& rs = state(ctx);
    const bool instance = attr(ctx, self, rs.compiled) || attr(ctx, self, rs.spans);
    if (!instance && posArgs && posArgs->getSize(ctx) >= 1) return posArgs->getAt(ctx, 0);
    return self;
}

// Patterns are equal when compiled from the same source with the same flags.
bool samePattern(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ProtoArguments* posArgs) {
    const CompiledPattern* a = compiledOf(ctx, self);
    const CompiledPattern* b = (posArgs && posArgs->getSize(ctx) >= 1) ? compiledOf(ctx, posArgs->getAt(ctx, 0)) : nullptr;
    if (!a || !b) return false;
    return a == b || (a->isBytes == b->isBytes && a->tr.flags == b->tr.flags && a->source == b->source);
}

RE_METHOD(py_pattern_eq) {
    (void)kwArgs;
    return samePattern(ctx, self, posArgs) ? PROTO_TRUE : PROTO_FALSE;
}

RE_METHOD(py_pattern_ne) {
    (void)kwArgs;
    return samePattern(ctx, self, posArgs) ? PROTO_FALSE : PROTO_TRUE;
}

RE_METHOD(py_pattern_hash) {
    (void)posArgs;
    (void)kwArgs;
    const CompiledPattern* cp = compiledOf(ctx, self);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const std::uint64_t h = cacheKey(cp->isBytes, cp->tr.flags, cp->source);
    return ctx->fromInteger(static_cast<long long>(h >> 9));  // within the 56-bit SmallInteger range
}

// ---------------------------------------------------------------------------
// Pattern methods
// ---------------------------------------------------------------------------

RE_METHOD(py_pattern_match) {
    return doMatch(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "string", "match"), kMatch, posArgs, kwArgs, 1, true);
}
RE_METHOD(py_pattern_fullmatch) {
    return doMatch(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "string", "fullmatch"), kFull, posArgs, kwArgs, 1, true);
}
RE_METHOD(py_pattern_search) {
    return doMatch(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "string", "search"), kSearch, posArgs, kwArgs, 1, true);
}
RE_METHOD(py_pattern_findall) {
    return doFindAll(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "string", "findall"), posArgs, kwArgs, 1, true);
}
RE_METHOD(py_pattern_finditer) {
    return doFindIter(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "string", "finditer"), posArgs, kwArgs, 1, true);
}
RE_METHOD(py_pattern_split) {
    return doSplit(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "string", "split"),
                   intArg(ctx, posArgs, kwArgs, 1, "maxsplit", 0));
}
RE_METHOD(py_pattern_sub) {
    return doSub(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "repl", "sub"),
                 requiredArg(ctx, posArgs, kwArgs, 1, "string", "sub"), intArg(ctx, posArgs, kwArgs, 2, "count", 0)).first;
}
RE_METHOD(py_pattern_subn) {
    auto r = doSub(ctx, self, requiredArg(ctx, posArgs, kwArgs, 0, "repl", "subn"),
                   requiredArg(ctx, posArgs, kwArgs, 1, "string", "subn"), intArg(ctx, posArgs, kwArgs, 2, "count", 0));
    PythonEnvironment::TransientPin pin(&envOf(ctx), r.first);
    return pyTuple(ctx, {r.first, ctx->fromInteger(r.second)});
}

RE_METHOD(py_pattern_repr) {
    (void)posArgs;
    (void)kwArgs;
    ReState& rs = state(ctx);
    const proto::ProtoObject* pat = attr(ctx, self, rs.pattern);
    CompiledPattern* cp = compiledOf(ctx, self);
    std::string out = "re.compile(" + (pat ? PythonEnvironment::reprObject(ctx, pat) : std::string("''"));
    long long flags = cp ? cp->tr.flags : 0;
    if (cp && !cp->isBytes) flags &= ~F_U;  // re.UNICODE is implied for a str pattern
    if (flags) {
        static const struct { long long bit; const char* name; } names[] = {
            {F_TEMPLATE, "re.TEMPLATE"}, {F_I, "re.IGNORECASE"}, {F_L, "re.LOCALE"}, {F_M, "re.MULTILINE"},
            {F_S, "re.DOTALL"}, {F_U, "re.UNICODE"}, {F_X, "re.VERBOSE"}, {F_DEBUG, "re.DEBUG"}, {F_A, "re.ASCII"}};
        std::string parts;
        for (const auto& n : names) {
            if (!(flags & n.bit)) continue;
            if (!parts.empty()) parts += "|";
            parts += n.name;
            flags &= ~n.bit;
        }
        if (flags) {
            char b[32];
            std::snprintf(b, sizeof b, "0x%llx", flags);
            if (!parts.empty()) parts += "|";
            parts += b;
        }
        out += ", " + parts;
    }
    out += ")";
    return PythonEnvironment::newStr(ctx, out);
}

// pattern.scanner(string, pos=0, endpos=maxsize): an object whose match()
// and search() return the successive matches, as CPython's SRE scanner.
RE_METHOD(py_pattern_scanner) {
    ReState& rs = state(ctx);
    CompiledPattern* cp = compiledOf(ctx, self);
    if (!cp) raiseTypeError(ctx, "descriptor requires a 're.Pattern' object");
    const proto::ProtoObject* subjectObj = requiredArg(ctx, posArgs, kwArgs, 0, "string", "scanner");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    size_t p = 0, e = s.ncp;
    window(ctx, posArgs, kwArgs, 1, s.ncp, p, e);
    const proto::ProtoObject* proto = attr(ctx, self, rs.scanIterProto);
    if (!proto) throw std::logic_error("re: no scanner prototype");
    const proto::ProtoString* names[6] = {rs.scanPattern, rs.scanString, rs.scanPos, rs.scanEnd, rs.scanAdvance,
                                          rs.pattern};
    const proto::ProtoObject* values[6] = {self, subjectObj, ctx->fromInteger(static_cast<long long>(p)),
                                           ctx->fromInteger(static_cast<long long>(e)), PROTO_FALSE, self};
    return proto->newChild(ctx, true)->setAttributes(ctx, 6, names, values);
}

const proto::ProtoObject* scannerStep(proto::ProtoContext* ctx, const proto::ProtoObject* self, Variant v) {
    ReState& rs = state(ctx);
    if (attr(ctx, self, rs.scanDone) == PROTO_TRUE) return PROTO_NONE;
    const proto::ProtoObject* patObj = attr(ctx, self, rs.scanPattern);
    CompiledPattern* cp = compiledOf(ctx, patObj);
    const proto::ProtoObject* subjectObj = attr(ctx, self, rs.scanString);
    const proto::ProtoObject* posObj = attr(ctx, self, rs.scanPos);
    const proto::ProtoObject* endObj = attr(ctx, self, rs.scanEnd);
    if (!cp || !subjectObj || !posObj || !endObj) raiseTypeError(ctx, "not a scanner object");
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    const size_t p = static_cast<size_t>(posObj->asLong(ctx));
    const size_t e = static_cast<size_t>(endObj->asLong(ctx));
    proto::ProtoObject* me = const_cast<proto::ProtoObject*>(self);
    Hit hit;
    const bool advance = attr(ctx, self, rs.scanAdvance) == PROTO_TRUE;
    if (p > e || !runMatch(ctx, patObj, *cp, v, s, s.byteAt(p), s.byteAt(e), advance ? PCRE2_NOTEMPTY_ATSTART : 0, hit)) {
        me->setAttribute(ctx, rs.scanDone, PROTO_TRUE);
        return PROTO_NONE;
    }
    const proto::ProtoObject* m = makeMatch(ctx, patObj, *cp, s, p, e, hit);
    PythonEnvironment::TransientPin pin(&envOf(ctx), m);
    me->setAttribute(ctx, rs.scanPos, ctx->fromInteger(static_cast<long long>(s.cpAt(hit.end()))));
    me->setAttribute(ctx, rs.scanAdvance, hit.start() == hit.end() ? PROTO_TRUE : PROTO_FALSE);
    return m;
}

RE_METHOD(py_scanner_match) {
    (void)posArgs;
    (void)kwArgs;
    return scannerStep(ctx, self, kMatch);
}
RE_METHOD(py_scanner_search) {
    (void)posArgs;
    (void)kwArgs;
    return scannerStep(ctx, self, kSearch);
}

// ---------------------------------------------------------------------------
// Module-level functions
// ---------------------------------------------------------------------------

// The pattern of a module-level call. A cached pattern was allocated by an
// earlier call, so nothing in this call's context keeps it alive: another
// thread's purge() or cache reset could let it be collected (and its compiled
// code freed) mid-match. The callers pin it for the duration of the call.
const proto::ProtoObject* modulePattern(proto::ProtoContext* ctx, const proto::ProtoObject* module,
                                        const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments* kwArgs,
                                        proto::proto_ulong flagsIdx, const char* func) {
    const proto::ProtoObject* pat = requiredArg(ctx, posArgs, kwArgs, 0, "pattern", func);
    return obtainPattern(ctx, module, pat, intArg(ctx, posArgs, kwArgs, flagsIdx, "flags", 0));
}

RE_METHOD(py_compile) {
    return modulePattern(ctx, self, posArgs, kwArgs, 1, "compile");
}

RE_METHOD(py_search) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 2, "search");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doMatch(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "string", "search"), kSearch, nullptr, nullptr, 0, false);
}
RE_METHOD(py_match) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 2, "match");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doMatch(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "string", "match"), kMatch, nullptr, nullptr, 0, false);
}
RE_METHOD(py_fullmatch) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 2, "fullmatch");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doMatch(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "string", "fullmatch"), kFull, nullptr, nullptr, 0, false);
}
RE_METHOD(py_findall) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 2, "findall");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doFindAll(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "string", "findall"), nullptr, nullptr, 0, false);
}
RE_METHOD(py_finditer) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 2, "finditer");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doFindIter(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "string", "finditer"), nullptr, nullptr, 0, false);
}
RE_METHOD(py_split) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 3, "split");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doSplit(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "string", "split"),
                   intArg(ctx, posArgs, kwArgs, 2, "maxsplit", 0));
}
RE_METHOD(py_sub) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 4, "sub");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    return doSub(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "repl", "sub"),
                 requiredArg(ctx, posArgs, kwArgs, 2, "string", "sub"), intArg(ctx, posArgs, kwArgs, 3, "count", 0)).first;
}
RE_METHOD(py_subn) {
    const proto::ProtoObject* pat = modulePattern(ctx, self, posArgs, kwArgs, 4, "subn");
    PythonEnvironment::TransientPin pinPat(&envOf(ctx), pat);  // see modulePattern
    auto r = doSub(ctx, pat, requiredArg(ctx, posArgs, kwArgs, 1, "repl", "subn"),
                   requiredArg(ctx, posArgs, kwArgs, 2, "string", "subn"), intArg(ctx, posArgs, kwArgs, 3, "count", 0));
    PythonEnvironment::TransientPin pin(&envOf(ctx), r.first);
    return pyTuple(ctx, {r.first, ctx->fromInteger(r.second)});
}

RE_METHOD(py_purge) {
    (void)posArgs;
    (void)kwArgs;
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, state(ctx).cache, ctx->newSparseList()->asObject(ctx));
    return PROTO_NONE;
}

// re.escape(pattern): CPython 3.7+'s set of special characters only.
RE_METHOD(py_escape) {
    const proto::ProtoObject* arg = requiredArg(ctx, posArgs, kwArgs, 0, "pattern", "escape");
    std::string in;
    bool isBytes = false;
    if (arg->isString(ctx)) arg->asString(ctx)->toUTF8String(ctx, in);
    else if (bytesOf(ctx, arg, in)) isBytes = true;
    else raiseTypeError(ctx, "expected string or bytes-like object");
    static const char kSpecial[] = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
    std::string out;
    out.reserve(in.size() * 2);
    for (char c : in) {
        if (c != '\0' && std::strchr(kSpecial, c)) out += '\\';
        out += c;
    }
    return isBytes ? makeBytes(ctx, out.data(), out.size()) : PythonEnvironment::newStr(ctx, out);
}

// ---------------------------------------------------------------------------
// re.Scanner(lexicon, flags=0): scan(string) -> (results, remainder)
// ---------------------------------------------------------------------------

RE_METHOD(py_scanner_new) {
    PythonEnvironment& env = envOf(ctx);
    ReState& rs = state(ctx);
    (void)self;
    // posArgs: cls, lexicon, flags
    const proto::ProtoObject* cls = requiredArg(ctx, posArgs, nullptr, 0, "cls", "Scanner");
    const proto::ProtoObject* lexicon = requiredArg(ctx, posArgs, kwArgs, 1, "lexicon", "Scanner");
    const long long flags = intArg(ctx, posArgs, kwArgs, 2, "flags", 0);
    const proto::ProtoObject* module = env.resolveModule("re", ctx);
    if (!module || module == PROTO_NONE) throw std::logic_error("re: module not found");
    const TupleView seq = PythonEnvironment::tupleView(ctx, lexicon);
    const proto::ProtoObject* data = seq ? nullptr : lexicon->getAttribute(ctx, env.getDataString());
    const proto::ProtoList* lst = (!seq && data && data != PROTO_NONE) ? data->asList(ctx) : nullptr;
    if (!seq && !lst) raiseTypeError(ctx, "Scanner lexicon must be a list or tuple of (phrase, action) pairs");
    const size_t n = seq ? seq.size(ctx) : lst->getSize(ctx);
    std::string combined;
    std::vector<const proto::ProtoObject*> outer, actions;
    unsigned groupsSoFar = 0;
    for (size_t k = 0; k < n; ++k) {
        const proto::ProtoObject* item = seq ? seq.at(ctx, k) : lst->getAt(ctx, static_cast<int>(k));
        const TupleView pair = PythonEnvironment::tupleView(ctx, item);
        const proto::ProtoObject* pd = pair ? nullptr : (item ? item->getAttribute(ctx, env.getDataString()) : nullptr);
        const proto::ProtoList* pl = (!pair && pd && pd != PROTO_NONE) ? pd->asList(ctx) : nullptr;
        const proto::ProtoObject* phrase = pair && pair.size(ctx) >= 2 ? pair.at(ctx, 0) : (pl && pl->getSize(ctx) >= 2 ? pl->getAt(ctx, 0) : nullptr);
        const proto::ProtoObject* action = pair && pair.size(ctx) >= 2 ? pair.at(ctx, 1) : (pl && pl->getSize(ctx) >= 2 ? pl->getAt(ctx, 1) : nullptr);
        if (!phrase || !phrase->isString(ctx)) raiseTypeError(ctx, "Scanner phrases must be str patterns");
        std::string src;
        phrase->asString(ctx)->toUTF8String(ctx, src);
        // The phrase's own groups shift the number of the next phrase's group.
        const proto::ProtoObject* ph = compileNew(ctx, module, phrase, false, std::string(src), flags);
        const unsigned phraseGroups = compiledOf(ctx, ph)->tr.groups;
        if (k) combined += "|";
        combined += "(" + src + ")";
        outer.push_back(ctx->fromInteger(static_cast<long long>(groupsSoFar + 1)));
        actions.push_back(action ? action : PROTO_NONE);
        groupsSoFar += 1 + phraseGroups;
    }
    const proto::ProtoObject* combinedObj = PythonEnvironment::newStr(ctx, combined);
    const proto::ProtoObject* pat = compileNew(ctx, module, combinedObj, false, std::string(combined), flags);
    PythonEnvironment::TransientPin pinPat(&env, pat);
    const proto::ProtoObject* obj = ctx->newObject(true)->addParent(ctx, cls);
    const proto::ProtoString* names[5] = {env.getClassString(), rs.lexicon, rs.scanner, rs.scanOuter, rs.scanActions};
    const proto::ProtoObject* values[5] = {cls, lexicon, pat,
                                           ctx->newList(static_cast<unsigned>(outer.size()), outer.data())->asObject(ctx),
                                           ctx->newList(static_cast<unsigned>(actions.size()), actions.data())->asObject(ctx)};
    return obj->setAttributes(ctx, 5, names, values);
}

RE_METHOD(py_scanner_scan) {
    PythonEnvironment& env = envOf(ctx);
    ReState& rs = state(ctx);
    const proto::ProtoObject* subjectObj = requiredArg(ctx, posArgs, kwArgs, 0, "string", "scan");
    const proto::ProtoObject* patObj = attr(ctx, self, rs.scanner);
    CompiledPattern* cp = compiledOf(ctx, patObj);
    const proto::ProtoObject* outerObj = attr(ctx, self, rs.scanOuter);
    const proto::ProtoObject* actionsObj = attr(ctx, self, rs.scanActions);
    if (!cp || !outerObj || !actionsObj) raiseTypeError(ctx, "not a re.Scanner object");
    const proto::ProtoList* outer = outerObj->asList(ctx);
    const proto::ProtoList* actions = actionsObj->asList(ctx);
    const std::shared_ptr<const Subject> sref = subjectFor(ctx, subjectObj, cp->isBytes);
    const Subject& s = *sref;
    const proto::ProtoList* results = ctx->newList();
    PythonEnvironment::TransientPin pinResults(&env, results->asObject(ctx));
    size_t i = 0;
    Hit hit;
    while (runMatch(ctx, patObj, *cp, kMatch, s, i, s.data.size(), 0, hit)) {
        const size_t j = hit.end();
        if (j == i) break;
        int phrase = -1;
        for (proto::proto_ulong k = 0; k < outer->getSize(ctx); ++k) {
            if (hit.has(static_cast<unsigned>(outer->getAt(ctx, static_cast<int>(k))->asLong(ctx)))) {
                phrase = static_cast<int>(k);
                break;
            }
        }
        const proto::ProtoObject* action = phrase >= 0 ? actions->getAt(ctx, phrase) : PROTO_NONE;
        if (action && action != PROTO_NONE && isCallable(ctx, action)) {
            // The match as CPython's Scanner exposes it: phrase k is group
            // k + 1, and the phrase's own groups keep their numbers within
            // the phrase (1, 2, ...), sharing those of the phrases' groups.
            // Where they share one, SRE records the phrase's start, then the
            // inner group's span, then the phrase's end.
            const unsigned og = static_cast<unsigned>(outer->getAt(ctx, phrase)->asLong(ctx));
            const unsigned nextOg = static_cast<proto::proto_ulong>(phrase) + 1 < outer->getSize(ctx)
                ? static_cast<unsigned>(outer->getAt(ctx, phrase + 1)->asLong(ctx)) : cp->tr.groups + 1;
            Hit view;
            view.ov.assign(hit.ov.size(), PCRE2_UNSET);
            view.ov[0] = hit.ov[0];
            view.ov[1] = hit.ov[1];
            for (unsigned g = 1; og + g < nextOg && 2 * g + 1 < view.ov.size(); ++g) {
                view.ov[2 * g] = hit.ov[2 * (og + g)];
                view.ov[2 * g + 1] = hit.ov[2 * (og + g) + 1];
            }
            const unsigned pg = static_cast<unsigned>(phrase) + 1;
            if (view.ov[2 * pg] == PCRE2_UNSET) view.ov[2 * pg] = hit.ov[2 * og];
            view.ov[2 * pg + 1] = hit.ov[2 * og + 1];
            const proto::ProtoObject* m = makeMatch(ctx, patObj, *cp, s, 0, s.ncp, view, static_cast<int>(pg),
                                                    static_cast<unsigned>(outer->getSize(ctx)));
            PythonEnvironment::TransientPin pinM(&env, m);
            const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, rs.match, m);
            const proto::ProtoObject* token = s.piece(ctx, hit.start(), j);
            PythonEnvironment::TransientPin pinT(&env, token);
            action = env.callObject(action, {self, token});
        }
        if (action && action != PROTO_NONE && action != env.getNonePrototype()) {
            PythonEnvironment::TransientPin pinA(&env, action);
            results = results->appendLast(ctx, action);
            PythonEnvironment::TransientPin pinR(&env, results->asObject(ctx));
            // Keep the newest list pinned for the next callback.
            const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__scan_results__"),
                                                                 results->asObject(ctx));
        }
        i = j;
    }
    const proto::ProtoObject* list = PythonEnvironment::wrapList(ctx, results);
    PythonEnvironment::TransientPin pinList(&env, list);
    return pyTuple(ctx, {list, s.piece(ctx, i, s.data.size())});
}

// re.Pattern / re.Match as classes: type(m) is re.Match and repr() finds
// their __repr__.
const proto::ProtoObject* makeReClass(proto::ProtoContext* ctx, const proto::ProtoObject* cls, const char* name) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !env->getObjectPrototype() || !env->getTypePrototype()) return cls;
    const proto::ProtoObject* objectProto = env->getObjectPrototype();
    cls = cls->addParent(ctx, objectProto);
    cls = cls->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    const proto::ProtoObject* nameObj = PythonEnvironment::getInternedString(ctx, name)->asObject(ctx);
    cls = cls->setAttribute(ctx, env->getNameString(), nameObj);
    cls = cls->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__qualname__"), nameObj);
    cls = cls->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__module__"),
                            PythonEnvironment::getInternedString(ctx, "re")->asObject(ctx));
    cls = cls->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__is_python_class__"), PROTO_TRUE);
    cls = cls->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__bases__"),
                            PythonEnvironment::tupleFromList(ctx, ctx->newList()->appendLast(ctx, objectProto)));
    cls = cls->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__mro__"),
                            PythonEnvironment::tupleFromList(ctx, ctx->newList()->appendLast(ctx, cls)->appendLast(ctx, objectProto)));
    return cls;
}

// re.error: class error(Exception), __module__ 're'.
const proto::ProtoObject* makeErrorClass(proto::ProtoContext* ctx) {
    PythonEnvironment& env = envOf(ctx);
    const proto::ProtoObject* typeFn = env.resolve("type", ctx);
    const proto::ProtoObject* exception = env.resolve("Exception", ctx);
    if (!typeFn || typeFn == PROTO_NONE || !exception || exception == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* ns = makeDict(ctx);
    PythonEnvironment::TransientPin pinNs(&env, ns);
    env.callMethod(ns, "__setitem__", {PythonEnvironment::getInternedString(ctx, "__module__")->asObject(ctx),
                                       PythonEnvironment::getInternedString(ctx, "re")->asObject(ctx)});
    const proto::ProtoObject* bases = pyTuple(ctx, {exception});
    PythonEnvironment::TransientPin pinBases(&env, bases);
    const proto::ProtoObject* cls = env.callObject(typeFn, {PythonEnvironment::getInternedString(ctx, "error")->asObject(ctx), bases, ns});
    return cls ? cls : PROTO_NONE;
}

}  // namespace

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    ReState& rs = state(ctx);
    // Mutable, so the functions bound to it below see every attribute set
    // later (the compile cache in particular).
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    rs.module = mod;
    auto sym = [&](const char* n) { return proto::ProtoString::createSymbol(ctx, n); };
    auto bind = [&](proto::ProtoObject* target, const char* name, proto::ProtoMethod fn) {
        target->setAttribute(ctx, sym(name), ctx->fromMethod(target, fn));
    };

    const proto::ProtoObject* errorCls = makeErrorClass(ctx);

    proto::ProtoObject* matchProto = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    bind(matchProto, "group", py_match_group);
    bind(matchProto, "groups", py_match_groups);
    bind(matchProto, "groupdict", py_match_groupdict);
    bind(matchProto, "start", py_match_start);
    bind(matchProto, "end", py_match_end);
    bind(matchProto, "span", py_match_span);
    bind(matchProto, "expand", py_match_expand);
    bind(matchProto, "__getitem__", py_match_getitem);
    bind(matchProto, "__repr__", py_match_repr);
    bind(matchProto, "__copy__", py_identity);
    bind(matchProto, "__deepcopy__", py_identity);
    matchProto->setAttribute(ctx, rs.error, errorCls);

    proto::ProtoObject* scanIterProto = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    bind(scanIterProto, "match", py_scanner_match);
    bind(scanIterProto, "search", py_scanner_search);

    proto::ProtoObject* patternProto = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    patternProto->setAttribute(ctx, rs.matchProto, matchProto);
    patternProto->setAttribute(ctx, rs.scanIterProto, scanIterProto);
    patternProto->setAttribute(ctx, rs.error, errorCls);
    bind(patternProto, "match", py_pattern_match);
    bind(patternProto, "fullmatch", py_pattern_fullmatch);
    bind(patternProto, "search", py_pattern_search);
    bind(patternProto, "sub", py_pattern_sub);
    bind(patternProto, "subn", py_pattern_subn);
    bind(patternProto, "findall", py_pattern_findall);
    bind(patternProto, "finditer", py_pattern_finditer);
    bind(patternProto, "split", py_pattern_split);
    bind(patternProto, "scanner", py_pattern_scanner);
    bind(patternProto, "__repr__", py_pattern_repr);
    bind(patternProto, "__copy__", py_identity);
    bind(patternProto, "__eq__", py_pattern_eq);
    bind(patternProto, "__ne__", py_pattern_ne);
    bind(patternProto, "__hash__", py_pattern_hash);
    bind(patternProto, "__deepcopy__", py_identity);

    mod->setAttribute(ctx, rs.matchProto, matchProto);
    mod->setAttribute(ctx, rs.patternProto, patternProto);
    mod->setAttribute(ctx, rs.error, errorCls);
    mod->setAttribute(ctx, rs.cache, ctx->newSparseList()->asObject(ctx));
    mod->setAttribute(ctx, sym("error"), errorCls);
    mod->setAttribute(ctx, sym("PatternError"), errorCls);

    bind(mod, "compile", py_compile);
    bind(mod, "escape", py_escape);
    bind(mod, "match", py_match);
    bind(mod, "search", py_search);
    bind(mod, "fullmatch", py_fullmatch);
    bind(mod, "findall", py_findall);
    bind(mod, "finditer", py_finditer);
    bind(mod, "sub", py_sub);
    bind(mod, "subn", py_subn);
    bind(mod, "split", py_split);
    bind(mod, "purge", py_purge);

    static const struct { const char* name; long long value; } kFlags[] = {
        {"NOFLAG", 0}, {"TEMPLATE", F_TEMPLATE}, {"T", F_TEMPLATE}, {"IGNORECASE", F_I}, {"I", F_I},
        {"LOCALE", F_L}, {"L", F_L}, {"MULTILINE", F_M}, {"M", F_M}, {"DOTALL", F_S}, {"S", F_S},
        {"UNICODE", F_U}, {"U", F_U}, {"VERBOSE", F_X}, {"X", F_X}, {"DEBUG", F_DEBUG},
        {"ASCII", F_A}, {"A", F_A}};
    for (const auto& f : kFlags) mod->setAttribute(ctx, sym(f.name), ctx->fromInteger(f.value));

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;
    proto::ProtoObject* scannerType = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    if (objectProto && objectProto != PROTO_NONE) scannerType->addParent(ctx, objectProto);
    scannerType->setAttribute(ctx, sym("__name__"), PythonEnvironment::getInternedString(ctx, "Scanner")->asObject(ctx));
    scannerType->setAttribute(ctx, sym("__new__"), ctx->fromMethod(nullptr, py_scanner_new));
    scannerType->setAttribute(ctx, sym("scan"), ctx->fromMethod(nullptr, py_scanner_scan));
    if (objectProto) {
        scannerType->setAttribute(ctx, sym("__bases__"),
                                  PythonEnvironment::tupleFromList(ctx, ctx->newList()->appendLast(ctx, objectProto)));
        scannerType->setAttribute(ctx, sym("__mro__"),
                                  PythonEnvironment::tupleFromList(ctx, ctx->newList()->appendLast(ctx, scannerType)->appendLast(ctx, objectProto)));
    }
    mod->setAttribute(ctx, sym("Scanner"), scannerType);

    mod->setAttribute(ctx, sym("Pattern"), makeReClass(ctx, patternProto, "Pattern"));
    mod->setAttribute(ctx, sym("Match"), makeReClass(ctx, matchProto, "Match"));
    return mod;
}

}  // namespace re
}  // namespace protoPython
