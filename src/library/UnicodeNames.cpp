// UnicodeNames.cpp — see include/protoPython/UnicodeNames.h.
#include <protoPython/UnicodeNames.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

namespace protoPython {
namespace unicode_names {
namespace {

#include "UnicodeNameTable.inc"

// Hangul syllables: U+AC00 + (L * 21 + V) * 28 + T (The Unicode Standard,
// section 3.12).  The names are "HANGUL SYLLABLE " + the short names of the
// three jamo, as CPython's unicodedata computes them.
constexpr uint32_t kHangulBase = 0xAC00;
constexpr int kHangulL = 19, kHangulV = 21, kHangulT = 28;
const char* const kJamoL[kHangulL] = {
    "G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS", "", "J", "JJ",
    "C", "K", "T", "P", "H"};
const char* const kJamoV[kHangulV] = {
    "A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA", "WAE", "OE",
    "YO", "U", "WEO", "WE", "WI", "YU", "EU", "YI", "I"};
const char* const kJamoT[kHangulT] = {
    "", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB", "LS",
    "LT", "LP", "LH", "M", "B", "BS", "S", "SS", "NG", "J", "C", "K", "T",
    "P", "H"};
constexpr std::string_view kHangulPrefix = "HANGUL SYLLABLE ";
constexpr std::string_view kCjkPrefix = "CJK UNIFIED IDEOGRAPH-";

// The longest jamo short name of `table` that starts `s`; its index, or -1.
int matchJamo(std::string_view s, const char* const* table, int count, size_t* used) {
    int best = -1;
    size_t bestLen = 0;
    for (int i = 0; i < count; ++i) {
        const size_t len = std::strlen(table[i]);
        if (s.substr(0, len) == table[i] && (best < 0 || len > bestLen)) {
            best = i;
            bestLen = len;
        }
    }
    *used = bestLen;
    return best;
}

int32_t lookupHangul(std::string_view s) {
    size_t n = 0;
    const int l = matchJamo(s, kJamoL, kHangulL, &n);
    s.remove_prefix(n);
    const int v = matchJamo(s, kJamoV, kHangulV, &n);
    s.remove_prefix(n);
    const int t = matchJamo(s, kJamoT, kHangulT, &n);
    s.remove_prefix(n);
    if (l < 0 || v < 0 || t < 0 || !s.empty()) return -1;
    return static_cast<int32_t>(kHangulBase + (l * kHangulV + v) * kHangulT + t);
}

// `digits` as upper-case hexadecimal, or -1.
int64_t parseHex(std::string_view digits) {
    if (digits.empty() || digits.size() > 6) return -1;
    int64_t v = 0;
    for (char c : digits) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

bool inRange(int prefix, int64_t cp) {
    for (const NameRange& r : kNameRanges)
        if (r.prefix == prefix && cp >= r.first && cp <= r.last) return true;
    return false;
}

std::string_view word(unsigned i) {
    return std::string_view(kLexicon + kLexiconOffsets[i], kLexiconOffsets[i + 1] - kLexiconOffsets[i]);
}

// The number of the lexicon word `w`, or -1 (binary search by spelling).
int wordNumber(std::string_view w) {
    const uint16_t* first = kWordsBySpelling;
    const uint16_t* last = kWordsBySpelling + kWordCount;
    const uint16_t* it = std::lower_bound(first, last, w,
        [](uint16_t i, std::string_view key) { return word(i) < key; });
    if (it == last || word(*it) != w) return -1;
    return *it;
}

void appendWordCode(std::string& code, unsigned i) {
    if (i < kOneByteWords) {
        code.push_back(static_cast<char>(i));
    } else {
        i -= kOneByteWords;
        code.push_back(static_cast<char>(kOneByteWords + (i >> 8)));
        code.push_back(static_cast<char>(i & 0xFF));
    }
}

}  // namespace

const char* unicodeVersion() { return kUnicodeVersion; }

int32_t lookup(std::string_view name) {
    // Computed names, upper case only (as CPython's unicodedata).
    if (name.substr(0, kHangulPrefix.size()) == kHangulPrefix)
        return lookupHangul(name.substr(kHangulPrefix.size()));
    if (name.substr(0, kCjkPrefix.size()) == kCjkPrefix) {
        const std::string_view hex = name.substr(kCjkPrefix.size());
        const int64_t cp = (hex.size() == 4 || hex.size() == 5) ? parseHex(hex) : -1;
        const auto cjk = std::find(std::begin(kRangePrefixes), std::end(kRangePrefixes),
                                   std::string_view(kCjkPrefix)) - std::begin(kRangePrefixes);
        return cp >= 0 && inRange(static_cast<int>(cjk), cp) ? static_cast<int32_t>(cp) : -1;
    }

    std::string upper(name);
    for (char& c : upper)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

    // The other PREFIX-XXXX names: the canonical spelling (%04X) only.
    for (size_t p = 0; p < std::size(kRangePrefixes); ++p) {
        const std::string_view prefix = kRangePrefixes[p];
        if (prefix == kCjkPrefix || upper.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string_view hex = std::string_view(upper).substr(prefix.size());
        const int64_t cp = parseHex(hex);
        if (cp < 0 || !inRange(static_cast<int>(p), cp)) continue;
        char canonical[8];
        std::snprintf(canonical, sizeof canonical, "%04X", static_cast<unsigned>(cp));
        if (hex == canonical) return static_cast<int32_t>(cp);
    }

    // Stored names and aliases: encode the words, then compare entries.
    std::string code;
    size_t start = 0;
    for (;;) {
        const size_t space = upper.find(' ', start);
        const std::string_view w = std::string_view(upper).substr(
            start, space == std::string::npos ? std::string::npos : space - start);
        const int i = wordNumber(w);
        if (i < 0) return -1;
        appendWordCode(code, static_cast<unsigned>(i));
        if (space == std::string::npos) break;
        start = space + 1;
    }
    const unsigned char* p = kNameData;
    const unsigned char* end = kNameData + sizeof kNameData;
    while (p < end) {
        const size_t len = p[0];
        if (len == code.size() && std::memcmp(p + 4, code.data(), len) == 0)
            return static_cast<int32_t>(((p[1] & 0x7F) << 16) | (p[2] << 8) | p[3]);
        p += 4 + len;
    }
    return -1;
}

}  // namespace unicode_names
}  // namespace protoPython
