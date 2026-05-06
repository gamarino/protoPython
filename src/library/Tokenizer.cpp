#include <protoPython/Tokenizer.h>
#include <protoPython/DiagUtils.h>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace protoPython {

Tokenizer::Tokenizer(const std::string& source) : source_(source) {
    indentStack_.push_back(0);
}

void Tokenizer::skipWhitespace() {
    while (pos_ < source_.size()) {
        if (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\r')
            pos_++;
        else if (source_[pos_] == '#')
            skipComment();
        else
            break;
    }
}

void Tokenizer::skipWhitespaceNoNewline() {
    while (pos_ < source_.size()) {
        if (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\r')
            pos_++;
        else if (source_[pos_] == '#')
            skipComment();
        else
            break;
    }
}

void Tokenizer::skipComment() {
    while (pos_ < source_.size() && source_[pos_] != '\n')
        pos_++;
}

// CPython keyword check (Python 3.14 grammar).  Used by scanNumber to
// distinguish "number followed by keyword" (warning, e.g. `9and x`) from
// "number followed by non-keyword identifier" (error, e.g. `9spam`).
// Mirrors `keyword.kwlist` plus soft-keywords match/case/_ in 3.14.
static bool isPyKeyword(const std::string& s) {
    static const std::unordered_set<std::string> kw = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
        "while", "with", "yield",
    };
    return kw.count(s) != 0;
}

// Decode the next UTF-8 character starting at source_[off] as a 32-bit
// codepoint.  Used to format the "invalid character '<x>' (U+XXXX)"
// message that CPython emits when a non-ASCII char immediately follows a
// numeric literal — e.g. test_end_of_numerical_literals's `9⁄7` case
// (U+2044 FRACTION SLASH).  Returns the codepoint and writes the byte
// length consumed into *bytesOut.  On invalid UTF-8 returns the raw byte
// and bytesOut=1 so the caller still produces a sensible message.
static unsigned decodeUtf8Codepoint(const std::string& s, size_t off, int* bytesOut) {
    unsigned char c0 = static_cast<unsigned char>(s[off]);
    if (c0 < 0x80) { *bytesOut = 1; return c0; }
    auto cont = [&](size_t i) -> int {
        if (i >= s.size()) return -1;
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) return -1;
        return c & 0x3F;
    };
    if ((c0 & 0xE0) == 0xC0) {
        int c1 = cont(off + 1);
        if (c1 < 0) { *bytesOut = 1; return c0; }
        *bytesOut = 2;
        return ((c0 & 0x1F) << 6) | c1;
    }
    if ((c0 & 0xF0) == 0xE0) {
        int c1 = cont(off + 1), c2 = cont(off + 2);
        if (c1 < 0 || c2 < 0) { *bytesOut = 1; return c0; }
        *bytesOut = 3;
        return ((c0 & 0x0F) << 12) | (c1 << 6) | c2;
    }
    if ((c0 & 0xF8) == 0xF0) {
        int c1 = cont(off + 1), c2 = cont(off + 2), c3 = cont(off + 3);
        if (c1 < 0 || c2 < 0 || c3 < 0) { *bytesOut = 1; return c0; }
        *bytesOut = 4;
        return ((c0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3;
    }
    *bytesOut = 1;
    return c0;
}

// After successfully scanning a numeric literal at [start, pos_), classify
// what follows.  CPython rules (Python 3.14 PEP 3131 + PEP 8 spirit):
//   - Whitespace, EOF, operator, punctuation: clean exit, no diagnostic.
//   - ASCII identifier-start (alpha or '_'): the user wrote
//     `<num><ident>` with no separator.  Read the identifier, decide:
//       * keyword (and/or/in/not/if/else/for/is/...) → SyntaxWarning
//         "invalid <kind> literal" — parser will tokenise the keyword
//         normally, so the program may still be valid (just suspicious).
//       * non-keyword                           → SyntaxError, same text.
//   - Non-ASCII (>= U+0080): SyntaxError "invalid character '<c>' (U+XXXX)".
// `kindName` is "decimal" / "hexadecimal" / "octal" / "binary" — selected
// by the caller from the parsing path that matched.
//
// Mutates `t` in place, sets t.type=Error or t.pendingWarning as needed.
// Does NOT advance pos_ for warnings (so the keyword tokenises normally on
// the next scan).  Returns true if t.type was set to Error (caller should
// return immediately from scanNumber).
bool Tokenizer::diagnoseNumberFollowOn(Token& t, const char* kindName) {
    if (pos_ >= source_.size()) return false;
    unsigned char nx = static_cast<unsigned char>(source_[pos_]);
    if (std::isalpha(nx) || nx == '_') {
        std::string ident;
        size_t look = pos_;
        while (look < source_.size()) {
            unsigned char c = static_cast<unsigned char>(source_[look]);
            if (std::isalnum(c) || c == '_') { ident.push_back(static_cast<char>(c)); look++; }
            else break;
        }
        std::string msg = std::string("invalid ") + kindName + " literal";
        if (isPyKeyword(ident)) {
            t.pendingWarning = msg;
            return false;
        }
        t.type = TokenType::Error;
        t.value = msg;
        return true;
    }
    if (nx >= 0x80) {
        int nbytes = 1;
        unsigned cp = decodeUtf8Codepoint(source_, pos_, &nbytes);
        // Emit raw UTF-8 of the offending character for the '<x>' field
        // and the formal U+XXXX form afterwards — matches the CPython
        // message "invalid character '⁄' (U+2044)".
        std::string raw(source_.c_str() + pos_, nbytes);
        char hexbuf[16];
        std::snprintf(hexbuf, sizeof(hexbuf), "%04X", cp);
        t.type = TokenType::Error;
        t.value = std::string("invalid character '") + raw + "' (U+" + hexbuf + ")";
        return true;
    }
    return false;
}

Token Tokenizer::scanNumber() {
    Token t = makeToken(TokenType::Number);
    size_t start = pos_;
    bool isFloat = false;
    bool isComplex = false;

    // Check for base prefixes: 0x, 0o, 0b
    if (pos_ + 1 < source_.size() && source_[pos_] == '0') {
        char nextC = source_[pos_ + 1];
        if (nextC == 'x' || nextC == 'X' || nextC == 'o' || nextC == 'O' || nextC == 'b' || nextC == 'B') {
            pos_ += 2;
            int base = (nextC == 'x' || nextC == 'X') ? 16 : (nextC == 'o' || nextC == 'O') ? 8 : 2;
            const char* baseName = (base == 16 ? "hexadecimal" : base == 8 ? "octal" : "binary");

            // Consume only valid digits + underscore for this base.  Stop
            // at the first non-digit character — we'll classify it via
            // diagnoseNumberFollowOn below.  Previously we greedily ate
            // everything alphanumeric so we could emit "invalid digit 'X'
            // in <base> literal", but that message format does not match
            // CPython's `r'invalid \w+ literal'` regex contract used by
            // test_end_of_numerical_literals.
            auto isBaseDigit = [&](unsigned char c) -> bool {
                if (base == 16) return std::isxdigit(c) != 0;
                if (base == 8)  return c >= '0' && c <= '7';
                return c == '0' || c == '1';
            };
            while (pos_ < source_.size()) {
                unsigned char c = static_cast<unsigned char>(source_[pos_]);
                if (isBaseDigit(c) || c == '_') pos_++;
                else break;
            }
            t.value = source_.substr(start, pos_ - start);

            // Per CPython: a leading/trailing underscore and adjacent
            // underscores inside a base-prefixed literal are invalid.
            // The digit section must start and end with an actual digit
            // and must not contain "__".
            auto invalidUnderscore = [&](const std::string& s) -> bool {
                if (s.empty()) return true;
                if (s.back() == '_') return true;
                bool anyDigit = false;
                for (size_t i = 0; i < s.size(); ++i) {
                    if (s[i] == '_') {
                        if (i + 1 < s.size() && s[i + 1] == '_') return true;
                    } else {
                        anyDigit = true;
                    }
                }
                return !anyDigit;
            };
            // If the loop stopped at a *decimal digit* that's invalid for
            // this base (e.g. `0b12`, `0o18`, `0o1_8`), CPython's
            // test_bad_numerical_literals expects the more specific
            // "invalid digit 'X' in <base> literal" message.  This branch
            // must run BEFORE the trailing-underscore / empty-digits
            // checks below — `0b1_2` reaches `2` after a trailing `_`, and
            // the underscore check would otherwise hijack the message.
            if (pos_ < source_.size()) {
                unsigned char nx = static_cast<unsigned char>(source_[pos_]);
                if (std::isdigit(nx)) {
                    t.type = TokenType::Error;
                    t.value = std::string("invalid digit '") + static_cast<char>(nx)
                            + "' in " + baseName + " literal";
                    return t;
                }
            }
            std::string afterPrefix = t.value.substr(2);
            if (afterPrefix.empty() || invalidUnderscore(afterPrefix)) {
                t.type = TokenType::Error;
                t.value = std::string("invalid ") + baseName + " literal";
                return t;
            }
            // Diagnose what follows: keyword → warning, identifier → error,
            // non-ASCII → error.  Returns true on Error (so we bail).
            if (diagnoseNumberFollowOn(t, baseName)) {
                return t;
            }
            std::string cleanValue = t.value;
            cleanValue.erase(std::remove(cleanValue.begin(), cleanValue.end(), '_'), cleanValue.end());
            try {
                if (base == 10) {
                    t.intValue = std::stoll(cleanValue, nullptr, 10);
                    t.numValue = static_cast<double>(t.intValue);
                    t.isInteger = true;
                } else {
                    std::string digits = cleanValue.substr(2);
                    if (digits.empty()) {
                        t.type = TokenType::Error;
                        t.value = "Invalid numerical literal: " + t.value;
                        return t;
                    }
                    // Detect values that don't fit signed int64.  stoull
                    // accepts up to 2^64-1 silently; casting to signed
                    // would wrap into a negative.  Route those (and any
                    // out_of_range) through the bignum path.
                    unsigned long long uv = std::stoull(digits, nullptr, base);
                    if (uv > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
                        t.bigBase = base;
                        t.bigDigits = digits;
                        t.intValue = 0;
                        t.numValue = 0;
                    } else {
                        t.intValue = static_cast<long long>(uv);
                        t.numValue = static_cast<double>(t.intValue);
                    }
                    t.isInteger = true;
                }
            } catch (const std::out_of_range&) {
                std::string digits = (base == 10) ? cleanValue : cleanValue.substr(2);
                t.bigBase = base;
                t.bigDigits = digits;
                t.intValue = 0;
                t.numValue = 0;
                t.isInteger = true;
            } catch (...) {
                t.type = TokenType::Error;
                t.value = "Invalid numerical literal: " + t.value;
            }
            return t;
        }
    }

    // Normal numbers (dec, float, complex)
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == '.') {
            if (isFloat) break; // Second dot
            isFloat = true;
            pos_++;
        } else if (c == 'e' || c == 'E') {
            // CPython requires a float exponent to have at least one digit
            // after the optional sign.  Three cases (cf. test_grammar.py
            // test_float_exponent_tokenization and test_bad_numerical_literals
            // case "1e+"):
            //   1. e + digit, or e + sign + digit  -> consume normally.
            //   2. e + sign + non-digit (e.g. "1e+", "1e-x")  -> consume the
            //      e and the sign so the post-loop validator can attribute the
            //      error to "invalid decimal literal".
            //   3. e + non-digit, non-sign (e.g. "1else", "1ef")  -> backtrack:
            //      do not consume the 'e'; the literal ends here and the
            //      remaining text is tokenized as a name/keyword.
            size_t la = pos_ + 1;
            bool sawSign = false;
            if (la < source_.size() && (source_[la] == '+' || source_[la] == '-')) {
                sawSign = true;
                la++;
            }
            bool hasDigit = la < source_.size() && std::isdigit(static_cast<unsigned char>(source_[la]));
            if (hasDigit) {
                isFloat = true;
                pos_++;
                if (sawSign) pos_++;
            } else if (sawSign) {
                isFloat = true;
                pos_++;
                pos_++;
                break;
            } else {
                break;
            }
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '_') {
            pos_++;
        } else if (c == 'j' || c == 'J') {
            isComplex = true;
            pos_++;
            break;
        } else {
            break;
        }
    }
    
    t.value = source_.substr(start, pos_ - start);
    // CPython rejects decimal literals with:
    //   - a trailing underscore (1_, 1.2_, 1e2_)
    //   - leading zeros on non-zero decimals (012, 09_99) — historical
    //     octal syntax that 3.x forbids
    //   - adjacent underscores (1__0, 0.1__4)
    //   - underscore immediately before/after '.' or 'e'/'E' or sign
    {
        const std::string& v = t.value;
        auto badUnderscore = [&]() -> bool {
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] != '_') continue;
                if (i + 1 >= v.size()) return true; // trailing
                char nxt = v[i + 1];
                if (nxt == '_' || nxt == '.' || nxt == 'j' || nxt == 'J' ||
                    nxt == 'e' || nxt == 'E' || nxt == '+' || nxt == '-')
                    return true;
                if (i == 0) return true; // leading (shouldn't happen for nums, but guard)
                char prv = v[i - 1];
                if (prv == '.' || prv == 'e' || prv == 'E' || prv == '+' || prv == '-')
                    return true;
            }
            return false;
        };
        bool leadingZero = false;
        if (!isFloat && !isComplex && v.size() >= 2 && v[0] == '0') {
            // Allow pure zeros: 0, 00, 0_0, 0_0_0
            bool hasNonZero = false;
            for (char c : v) if (c != '0' && c != '_') { hasNonZero = true; break; }
            if (hasNonZero) leadingZero = true;
        }
        // Exponent ended without any digits ("1e+", "1e-", or bare "1e"
        // produced by case 2 of the e/E lookahead above).  Catch it via
        // the trailing character of the consumed literal.
        bool badExponent = false;
        if (isFloat && !v.empty()) {
            char last = v.back();
            if (last == 'e' || last == 'E' || last == '+' || last == '-') {
                badExponent = true;
            }
        }
        if (badUnderscore() || leadingZero || badExponent) {
            t.type = TokenType::Error;
            t.value = isFloat || isComplex
                ? std::string("invalid decimal literal")
                : (leadingZero
                    ? std::string("leading zeros in decimal integer literals are not permitted; use an 0o prefix for octal integers")
                    : std::string("invalid decimal literal"));
            return t;
        }
    }
    // Diagnose what follows for decimal/float/complex literals — same
    // contract as the base-prefixed branch above.  `9and x` → warning,
    // `9spam` → error, `9⁄7` → error.  Skipped silently when the next
    // char is whitespace / operator / EOF.
    if (diagnoseNumberFollowOn(t, "decimal")) {
        return t;
    }
    std::string cleanValue = t.value;
    cleanValue.erase(std::remove(cleanValue.begin(), cleanValue.end(), '_'), cleanValue.end());
    try {
        if (isComplex) {
            // Complex handled as float for now in the stub (just the real part or imaginary part as value)
            if (cleanValue.back() == 'j' || cleanValue.back() == 'J') cleanValue.pop_back();
            t.numValue = std::stod(cleanValue);
            t.isInteger = false;
        } else if (isFloat) {
            t.numValue = std::stod(cleanValue);
            t.isInteger = false;
        } else {
            try {
                t.intValue = std::stoll(cleanValue);
                t.numValue = static_cast<double>(t.intValue);
                t.isInteger = true;
            } catch (const std::out_of_range&) {
                // Literal exceeds int64 — keep `isInteger=true` and hand
                // the raw digits to the compiler so it can emit a bignum
                // constant via ProtoContext::fromString.
                t.bigBase = 10;
                t.bigDigits = cleanValue;
                t.intValue = 0;
                t.numValue = 0;
                t.isInteger = true;
            }
        }
    } catch (...) {
        t.type = TokenType::Error;
        t.value = "Invalid numerical literal: " + t.value;
    }
    return t;
}

Token Tokenizer::scanString(char quote, const std::string& prefix) {
    Token t = makeToken(TokenType::String);
    bool isRaw = false;
    bool isF = false;
    bool isB = false;
    for (char c : prefix) {
        char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lc == 'r') isRaw = true;
        if (lc == 'f') isF = true;
        if (lc == 'b') isB = true;
        if (lc == 't') { /* Handle Template String prefix if needed, for now just allow it */ }
    }
    if (isF) t.type = TokenType::FString;
    else if (isB) t.type = TokenType::Bytes;
    
    bool triple = false;
    if (pos_ + 2 < source_.size() && source_[pos_ + 1] == quote && source_[pos_ + 2] == quote) {
        triple = true;
        pos_ += 3;
    } else {
        pos_++;
    }
    std::string s;
    while (pos_ < source_.size()) {
        if (triple && pos_ + 2 < source_.size() && source_[pos_] == quote && source_[pos_ + 1] == quote && source_[pos_ + 2] == quote) {
            pos_ += 3;
            break;
        } else if (!triple && source_[pos_] == quote) {
            pos_++;
            break;
        }
        char c = source_[pos_++];

        // PEP 701: inside f-string expressions, nested quotes are allowed.
        // When we see '{' that starts an expression, skip over any nested string
        // literals inside the expression so we don't break on their quote chars.
        // We let the raw source text flow through as-is into 's'; parseFString
        // handles {{, }}, and {expr} in the token value.
        if (isF && c == '{') {
            s += '{';
            if (pos_ < source_.size() && source_[pos_] == '{') {
                // {{ escape — add the second { and continue
                s += source_[pos_++];
                continue;
            }
            // Real expression: depth-track and skip embedded string literals
            int depth = 1;
            while (pos_ < source_.size() && depth > 0) {
                char ec = source_[pos_++];
                s += ec;
                if (ec == '{') {
                    depth++;
                } else if (ec == '}') {
                    depth--;
                } else if ((ec == '\'' || ec == '"') && depth > 0) {
                    // Nested string literal inside expression — scan to its end
                    char innerQuote = ec;
                    bool innerTriple = false;
                    if (pos_ + 1 < source_.size()
                        && source_[pos_] == innerQuote
                        && source_[pos_+1] == innerQuote) {
                        innerTriple = true;
                        s += source_[pos_++];
                        s += source_[pos_++];
                    }
                    while (pos_ < source_.size()) {
                        if (innerTriple && pos_ + 2 < source_.size()
                            && source_[pos_] == innerQuote
                            && source_[pos_+1] == innerQuote
                            && source_[pos_+2] == innerQuote) {
                            s += source_[pos_++];
                            s += source_[pos_++];
                            s += source_[pos_++];
                            break;
                        } else if (!innerTriple && source_[pos_] == innerQuote) {
                            s += source_[pos_++];
                            break;
                        }
                        char nc = source_[pos_++];
                        s += nc;
                        if (nc == '\\' && pos_ < source_.size()) {
                            s += source_[pos_++];
                        }
                    }
                } else if (ec == '\\' && pos_ < source_.size()) {
                    s += source_[pos_++];
                }
            }
            continue;
        }

        if (c == '\\' && pos_ < source_.size()) {
            if (!isRaw) {
                // Helper: encode a Unicode codepoint into UTF-8 bytes
                // appended to `s` (used for str literals, where each
                // numeric escape names a codepoint, not a raw byte).
                auto append_utf8 = [&](unsigned int cp) {
                    if (cp < 0x80) {
                        s += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        s += static_cast<char>(0xC0 | (cp >> 6));
                        s += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        s += static_cast<char>(0xE0 | (cp >> 12));
                        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        s += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x110000) {
                        s += static_cast<char>(0xF0 | (cp >> 18));
                        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        s += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                };
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                char e = source_[pos_++];
                if (e == 'n') s += '\n';
                else if (e == 't') s += '\t';
                else if (e == 'r') s += '\r';
                else if (e == 'a') s += '\a';
                else if (e == 'b') s += '\b';
                else if (e == 'f') s += '\f';
                else if (e == 'v') s += '\v';
                else if (e == '0' || e == '1' || e == '2' || e == '3' ||
                         e == '4' || e == '5' || e == '6' || e == '7') {
                    // Octal escape: \ooo (1 to 3 octal digits)
                    int v = e - '0';
                    int n = 1;
                    while (n < 3 && pos_ < source_.size()
                           && source_[pos_] >= '0' && source_[pos_] <= '7') {
                        v = v * 8 + (source_[pos_++] - '0');
                        ++n;
                    }
                    // bytes: store as raw byte; str: emit UTF-8 codepoint
                    if (isB) s += static_cast<char>(static_cast<unsigned char>(v & 0xff));
                    else append_utf8(static_cast<unsigned int>(v));
                }
                else if (e == 'x' && pos_ + 1 < source_.size()) {
                    int h1 = hex(source_[pos_]);
                    int h2 = hex(source_[pos_ + 1]);
                    if (h1 >= 0 && h2 >= 0) {
                        unsigned int cp = static_cast<unsigned int>((h1 << 4) | h2);
                        if (isB) s += static_cast<char>(cp);
                        else append_utf8(cp);
                        pos_ += 2;
                    } else {
                        s += '\\'; s += e;
                    }
                }
                // \uHHHH — 4-hex-digit Unicode escape (str only).
                else if (!isB && e == 'u' && pos_ + 3 < source_.size()) {
                    int h1 = hex(source_[pos_]);
                    int h2 = hex(source_[pos_ + 1]);
                    int h3 = hex(source_[pos_ + 2]);
                    int h4 = hex(source_[pos_ + 3]);
                    if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                        unsigned int cp = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                        append_utf8(cp);
                        pos_ += 4;
                    } else {
                        s += '\\'; s += e;
                    }
                }
                // \UHHHHHHHH — 8-hex-digit Unicode escape (str only).
                else if (!isB && e == 'U' && pos_ + 7 < source_.size()) {
                    unsigned int cp = 0;
                    bool ok = true;
                    for (int i = 0; i < 8; ++i) {
                        int h = hex(source_[pos_ + i]);
                        if (h < 0) { ok = false; break; }
                        cp = (cp << 4) | static_cast<unsigned int>(h);
                    }
                    if (ok && cp < 0x110000) {
                        append_utf8(cp);
                        pos_ += 8;
                    } else {
                        s += '\\'; s += e;
                    }
                }
                else if (e == quote) s += quote;
                else if (e == '\\') s += '\\';
                else if (e == '\n') {
                    line_++;
                    lineStartPos_ = pos_;
                }
                // \N{NAME} requires a Unicode name database; preserve
                // the backslash so the literal isn't silently mangled.
                else if (e == 'N') { s += '\\'; s += e; }
                else s += e;
            } else {
                // In raw strings, a backslash followed by a quote does NOT end the string.
                if (source_[pos_] == quote || source_[pos_] == '\\') {
                    s += '\\';
                    s += source_[pos_++];
                } else {
                    s += '\\';
                }
            }
        } else {
            if (c == '\n') {
                line_++;
                lineStartPos_ = pos_;
            }
            s += c;
        }
    }
    t.value = s;
    return t;
}

Token Tokenizer::scanNameOrKeyword() {
    size_t start = pos_;
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            pos_++;
        else
            break;
    }
    Token t = makeToken(TokenType::Name);
    t.value = source_.substr(start, pos_ - start);
    if (t.value == "for") t.type = TokenType::For;
    else if (t.value == "in") t.type = TokenType::In;
    else if (t.value == "if") t.type = TokenType::If;
    else if (t.value == "elif") t.type = TokenType::Elif;
    else if (t.value == "else") t.type = TokenType::Else;
    else if (t.value == "global") t.type = TokenType::Global;
    else if (t.value == "def") t.type = TokenType::Def;
    else if (t.value == "import") t.type = TokenType::Import;
    else if (t.value == "from") t.type = TokenType::From;
    else if (t.value == "class") t.type = TokenType::Class;
    else if (t.value == "return") t.type = TokenType::Return;
    else if (t.value == "while") t.type = TokenType::While;
    else if (t.value == "True") t.type = TokenType::True;
    else if (t.value == "False") t.type = TokenType::False;
    else if (t.value == "None") t.type = TokenType::None;
    else if (t.value == "and") t.type = TokenType::And;
    else if (t.value == "or") t.type = TokenType::Or;
    else if (t.value == "not") t.type = TokenType::Not;
    else if (t.value == "try") t.type = TokenType::Try;
    else if (t.value == "except") t.type = TokenType::Except;
    else if (t.value == "finally") t.type = TokenType::Finally;
    else if (t.value == "raise") t.type = TokenType::Raise;
    else if (t.value == "break") t.type = TokenType::Break;
    else if (t.value == "continue") t.type = TokenType::Continue;
    else if (t.value == "type") t.type = TokenType::Type;
    else if (t.value == "match") t.type = TokenType::Match;
    else if (t.value == "case") t.type = TokenType::Case;
    else if (t.value == "lambda") t.type = TokenType::Lambda;
    else if (t.value == "with") t.type = TokenType::With;
    else if (t.value == "as") t.type = TokenType::As;
    else if (t.value == "is") t.type = TokenType::Is;
    else if (t.value == "yield") t.type = TokenType::Yield;
    else if (t.value == "type") t.type = TokenType::Type;
    else if (t.value == "pass") t.type = TokenType::Pass;
    else if (t.value == "del") t.type = TokenType::Del;
    else if (t.value == "assert") t.type = TokenType::Assert;
    else if (t.value == "nonlocal") t.type = TokenType::Nonlocal;
    else if (t.value == "async") t.type = TokenType::Async;
    else if (t.value == "await") t.type = TokenType::Await;
    return t;
}

Token Tokenizer::next() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }
    /* After a newline we are at line start: count indentation and emit Indent/Dedent. */
    if (atLineStart_ && nestingLevel_ == 0) {
        size_t tempPos = pos_;
        int indent = 0;
        while (tempPos < source_.size() && (source_[tempPos] == ' ' || source_[tempPos] == '\t')) {
            indent += 1;
            tempPos++;
        }

        if (tempPos >= source_.size()) {
            if (indentStack_.size() > 1) {
                indentStack_.pop_back();
                atLineStart_ = true;
                return makeToken(TokenType::Dedent);
            }
            pos_ = tempPos;
            return makeToken(TokenType::EndOfFile);
        }

        char nextC = source_[tempPos];
        if (nextC == '\n' || nextC == '\r' || nextC == '#') {
            pos_ = tempPos;
            if (nextC == '#') skipComment();
            if (pos_ < source_.size() && source_[pos_] == '\r') pos_++;
            if (pos_ < source_.size() && source_[pos_] == '\n') {
                pos_++;
                line_++;
                lineStartPos_ = pos_;
            }
            atLineStart_ = true;
            return next();
        }

        if (indent > indentStack_.back()) {
            pos_ = tempPos;
            indentStack_.push_back(indent);
            atLineStart_ = false;
            return makeToken(TokenType::Indent);
        }
        if (indent < indentStack_.back()) {
            indentStack_.pop_back();
            atLineStart_ = true;
            return makeToken(TokenType::Dedent);
        }
        // Same indent
        pos_ = tempPos;
        atLineStart_ = false;
    } else {
        skipWhitespace();
    }
    if (pos_ >= source_.size()) {
        // Emit synthetic DEDENTs for any outstanding indentation levels (as CPython does).
        if (nestingLevel_ == 0 && indentStack_.size() > 1) {
            indentStack_.pop_back();
            return makeToken(TokenType::Dedent);
        }
        return makeToken(TokenType::EndOfFile);
    }
    char c = source_[pos_];
    if (get_env_diag()) {
    }
    if (c == '\n') {
        pos_++;
        line_++;
        lineStartPos_ = pos_;
        if (nestingLevel_ > 0) {
            return next();
        }
        atLineStart_ = true;
        return makeToken(TokenType::Newline);
    }
    if (c == '+') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::PlusAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Plus); pos_++; return t;
    }
    if (c == '-') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::MinusAssign); pos_ += 2; return t;
        }
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
            Token t = makeToken(TokenType::Arrow); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Minus); pos_++; return t;
    }
    if (c == '*') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '*') {
            if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=') {
                Token t = makeToken(TokenType::DoubleStarAssign); pos_ += 3; return t;
            }
            Token t = makeToken(TokenType::DoubleStar); pos_ += 2; return t;
        }
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::StarAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Star); pos_++; return t;
    }
    if (c == '/') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
            if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=') {
                Token t = makeToken(TokenType::DoubleSlashAssign); pos_ += 3; return t;
            }
            Token t = makeToken(TokenType::DoubleSlash); pos_ += 2; return t;
        }
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::SlashAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Slash); pos_++; return t;
    }
    if (c == '(') { nestingLevel_++; Token t = makeToken(TokenType::LParen); pos_++; return t; }
    if (c == ')') { if (nestingLevel_ > 0) nestingLevel_--; Token t = makeToken(TokenType::RParen); pos_++; return t; }
    if (c == ',') { Token t = makeToken(TokenType::Comma); pos_++; return t; }
    if (c == '.') {
        if (pos_ + 1 < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_ + 1]))) {
            return scanNumber();
        }
        if (pos_ + 2 < source_.size() && source_[pos_ + 1] == '.' && source_[pos_ + 2] == '.') {
            Token t = makeToken(TokenType::Ellipsis);
            pos_ += 3;
            return t;
        }
        Token t = makeToken(TokenType::Dot); pos_++; return t;
    }
    if (c == '[') { nestingLevel_++; Token t = makeToken(TokenType::LSquare); pos_++; return t; }
    if (c == ']') { if (nestingLevel_ > 0) nestingLevel_--; Token t = makeToken(TokenType::RSquare); pos_++; return t; }
    if (c == '{') { nestingLevel_++; Token t = makeToken(TokenType::LCurly); pos_++; return t; }
    if (c == '}') { if (nestingLevel_ > 0) nestingLevel_--; Token t = makeToken(TokenType::RCurly); pos_++; return t; }
    if (c == ':') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::Walrus); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Colon); pos_++; return t;
    }
    if (c == ';') { Token t = makeToken(TokenType::Semicolon); pos_++; return t; }
    if (c == '%') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::ModuloAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Modulo); pos_++; return t;
    }
    if (c == '~') { Token t = makeToken(TokenType::Tilde); pos_++; return t; }
    if (c == '@') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::AtAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::At); pos_++; return t;
    }
    if (c == '!' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        Token t = makeToken(TokenType::NotEqual); pos_ += 2; return t;
    }
    if (c == '<') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '<') {
            if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=') {
                Token t = makeToken(TokenType::LShiftAssign); pos_ += 3; return t;
            }
            Token t = makeToken(TokenType::LShift); pos_ += 2; return t;
        }
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::LessEqual); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Less); pos_++; return t;
    }
    if (c == '>') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
            if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=') {
                Token t = makeToken(TokenType::RShiftAssign); pos_ += 3; return t;
            }
            Token t = makeToken(TokenType::RShift); pos_ += 2; return t;
        }
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::GreaterEqual); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Greater); pos_++; return t;
    }
    if (c == '=') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::EqEqual); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::Assign); pos_++; return t;
    }
    if (c == '|') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::OrAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::BitOr); pos_++; return t;
    }
    if (c == '&') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::AndAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::BitAnd); pos_++; return t;
    }
    if (c == '^') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            Token t = makeToken(TokenType::XorAssign); pos_ += 2; return t;
        }
        Token t = makeToken(TokenType::BitXor); pos_++; return t;
    }
    if (c == '"' || c == '\'')
        return scanString(c, "");
    if (std::isdigit(static_cast<unsigned char>(c)))
        return scanNumber();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        size_t tempPos = pos_;
        while (tempPos < source_.size()) {
            char p = static_cast<char>(std::tolower(static_cast<unsigned char>(source_[tempPos])));
            if (p == 'f' || p == 'r' || p == 'b' || p == 'u' || p == 't') tempPos++;
            else break;
        }
        if (tempPos < source_.size() && (source_[tempPos] == '"' || source_[tempPos] == '\'')) {
            char quote = source_[tempPos];
            std::string prefix = source_.substr(pos_, tempPos - pos_);
            pos_ = tempPos;
            return scanString(quote, prefix);
        }
        return scanNameOrKeyword();
    }
    if (c == '\\') {
        pos_++;
        skipWhitespaceNoNewline();
        if (pos_ < source_.size() && source_[pos_] == '\n') {
            pos_++;
            line_++;
            lineStartPos_ = pos_;
            // Do NOT set atLineStart_ = true; we stay on the same logical line.
            return next();
        }
        // If not followed by newline, it's an error
        Token t = makeToken(TokenType::Error);
        t.value = "Unexpected character: '\\'";
        return t;
    }
    pos_++;
    Token t = makeToken(TokenType::Error);
    t.value = "Unexpected character: '";
    t.value += c;
    t.value += "'";
    return t;
}

const Token& Tokenizer::peek() {
    if (!hasPeeked_) {
        peeked_ = next();
        hasPeeked_ = true;
    }
    return peeked_;
}

Token Tokenizer::makeToken(TokenType type) {
    Token t;
    t.type = type;
    t.line = line_;
    t.column = getColumn();
    return t;
}

} // namespace protoPython
