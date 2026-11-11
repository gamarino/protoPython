// The format specification mini-language for int and float, as CPython
// 3.14's Python/formatter_unicode.c and Python/pystrtod.c implement it. See
// include/protoPython/NumberFormat.h.

#include <protoPython/NumberFormat.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace protoPython {
namespace numfmt {

namespace {

// --- the spec ---------------------------------------------------------------

struct Spec {
    std::string fill;            // one code point (UTF-8); empty when not given
    char align = '\0';
    char sign = '\0';
    bool noNegZero = false;      // 'z'
    bool alternate = false;      // '#'
    int width = -1;
    char grouping = '\0';        // ',' or '_' for the integer part
    int precision = -1;
    char fracGrouping = '\0';    // ',' or '_' for the fraction (3.14)
    std::string type;            // the presentation type: one code point, or empty
};

size_t codePointLength(const std::string& s, size_t at) {
    const unsigned char c = static_cast<unsigned char>(s[at]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    return std::min(n, s.size() - at);
}

size_t codePointCount(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i += codePointLength(s, i)) ++n;
    return n;
}

unsigned codePointValue(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) return c;
    const size_t n = codePointLength(s, 0);
    unsigned cp = n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[k]) & 0x3F);
    return cp;
}

bool isAlign(char c) { return c == '<' || c == '>' || c == '=' || c == '^'; }

Result fail(Error kind, const std::string& msg) {
    Result r;
    r.err = kind;
    r.message = msg;
    return r;
}

// "Unknown format code 'x' for object of type 'T'", with CPython's spelling
// of a code that is not printable ASCII.
std::string shownCode(const std::string& type) {
    const unsigned cp = codePointValue(type);
    if (cp > 32 && cp < 128) return type;
    char buf[16];
    std::snprintf(buf, sizeof buf, "\\x%x", cp);
    return buf;
}

std::string unknownCode(const std::string& type, const char* typeName) {
    return "Unknown format code '" + shownCode(type) + "' for object of type '" + typeName + "'";
}

// Parses `text`; on an error fills `err` and returns false.
bool parseSpec(const std::string& text, Spec& s, Result& err, const char* typeName, char defaultAlign = '>',
               const char* defaultType = "") {
    size_t i = 0;
    const size_t n = text.size();
    if (n > 0) {
        const size_t first = codePointLength(text, 0);
        if (first < n && isAlign(text[first])) {
            s.fill = text.substr(0, first);
            s.align = text[first];
            i = first + 1;
        } else if (isAlign(text[0])) {
            s.align = text[0];
            i = 1;
        }
    }
    if (i < n && (text[i] == '+' || text[i] == '-' || text[i] == ' ')) s.sign = text[i++];
    if (i < n && text[i] == 'z') { s.noNegZero = true; ++i; }
    if (i < n && text[i] == '#') { s.alternate = true; ++i; }
    if (s.fill.empty() && i < n && text[i] == '0') {
        s.fill = "0";
        if (s.align == '\0' && defaultAlign == '>') s.align = '=';
        ++i;
    }
    if (i < n && text[i] >= '0' && text[i] <= '9') {
        s.width = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') {
            if (s.width > 100000000) { err = fail(Error::Value, "Too many decimal digits in format string"); return false; }
            s.width = s.width * 10 + (text[i++] - '0');
        }
    }
    if (i < n && (text[i] == ',' || text[i] == '_')) {
        s.grouping = text[i++];
        if (i < n && (text[i] == ',' || text[i] == '_')) {
            err = fail(Error::Value, text[i] == s.grouping
                ? std::string("Cannot specify '") + s.grouping + "' with '" + s.grouping + "'."
                : "Cannot specify both ',' and '_'.");
            return false;
        }
    }
    if (i < n && text[i] == '.') {
        ++i;
        if (i < n && text[i] >= '0' && text[i] <= '9') {
            s.precision = 0;
            while (i < n && text[i] >= '0' && text[i] <= '9') {
                if (s.precision > 100000000) { err = fail(Error::Value, "Too many decimal digits in format string"); return false; }
                s.precision = s.precision * 10 + (text[i++] - '0');
            }
        }
        if (i < n && (text[i] == ',' || text[i] == '_')) {
            s.fracGrouping = text[i++];
            if (i < n && (text[i] == ',' || text[i] == '_')) {
                err = fail(Error::Value, text[i] == s.fracGrouping
                    ? std::string("Cannot specify '") + s.fracGrouping + "' with '" + s.fracGrouping + "'."
                    : "Cannot specify both ',' and '_'.");
                return false;
            }
        }
        if (s.precision < 0 && s.fracGrouping == '\0') {
            err = fail(Error::Value, "Format specifier missing precision");
            return false;
        }
    }
    if (i < n) {
        const size_t len = codePointLength(text, i);
        if (i + len < n) {
            err = fail(Error::Value, "Invalid format specifier '" + text + "' for object of type '" + typeName + "'");
            return false;
        }
        s.type = text.substr(i, len);
    }
    // Grouping is checked against the type at parse time, as in CPython.
    const std::string type = s.type.empty() ? std::string(defaultType) : s.type;
    if (const char g = s.grouping) {
        const char t = type.size() == 1 ? type[0] : '\x01';
        const bool always = type.empty() || t == 'd' || t == 'e' || t == 'f' || t == 'g' || t == 'E'
                            || t == 'G' || t == '%' || t == 'F';
        const bool underscoreOnly = t == 'b' || t == 'o' || t == 'x' || t == 'X';
        if (!always && !(underscoreOnly && g == '_')) {
            err = fail(Error::Value, std::string("Cannot specify '") + g + "' with '" + shownCode(type) + "'.");
            return false;
        }
    }
    // Of the fraction's grouping, only 'n' is refused there.
    if (s.fracGrouping && s.type == "n") {
        err = fail(Error::Value, std::string("Cannot specify '") + s.fracGrouping + "' with 'n'.");
        return false;
    }
    return true;
}

// --- layout -------------------------------------------------------------------

// Groups `digits` by threes from the right with `sep`.
std::string groupLeft(const std::string& digits, char sep, size_t every = 3) {
    if (!sep || digits.size() <= every) return digits;
    std::string out;
    size_t lead = digits.size() % every;
    if (lead == 0) lead = every;
    out.append(digits, 0, lead);
    for (size_t k = lead; k < digits.size(); k += every) {
        out.push_back(sep);
        out.append(digits, k, every);
    }
    return out;
}

// Groups fraction digits by threes from the left.
std::string groupRight(const std::string& digits, char sep) {
    if (!sep || digits.size() <= 3) return digits;
    std::string out;
    for (size_t k = 0; k < digits.size(); k += 3) {
        if (k) out.push_back(sep);
        out.append(digits, k, 3);
    }
    return out;
}

// A number split as CPython lays it out: sign, prefix (0x...), the integer
// digits (grouped, and zero-padded for '0='), then the rest.
std::string layout(const Spec& s, const std::string& signStr, const std::string& prefix,
                   const std::string& intDigits, const std::string& rest, size_t groupEvery) {
    const std::string fill = s.fill.empty() ? " " : s.fill;
    const char align = s.align ? s.align : '>';
    const int width = s.width < 0 ? 0 : s.width;
    std::string digits = groupLeft(intDigits, s.grouping, groupEvery);
    const size_t fixedLen = signStr.size() + prefix.size() + codePointCount(rest);
    if (align == '=' && fill == "0" && s.grouping && width > 0) {
        // The padding is digits too, grouped with them.
        std::string padded = intDigits;
        while (static_cast<int>(fixedLen + groupLeft(padded, s.grouping, groupEvery).size()) < width)
            padded.insert(padded.begin(), '0');
        digits = groupLeft(padded, s.grouping, groupEvery);
        if (!digits.empty() && digits[0] == s.grouping) digits.insert(digits.begin(), '0');
        return signStr + prefix + digits + rest;
    }
    const size_t len = fixedLen + digits.size();
    if (static_cast<int>(len) >= width) return signStr + prefix + digits + rest;
    const size_t pad = static_cast<size_t>(width) - len;
    auto repeat = [&](size_t k) {
        std::string r;
        for (size_t j = 0; j < k; ++j) r += fill;
        return r;
    };
    switch (align) {
        case '<': return signStr + prefix + digits + rest + repeat(pad);
        case '^': return repeat(pad / 2) + signStr + prefix + digits + rest + repeat(pad - pad / 2);
        case '=': return signStr + prefix + repeat(pad) + digits + rest;
        default:  return repeat(pad) + signStr + prefix + digits + rest;
    }
}

std::string signFor(const Spec& s, bool negative) {
    if (negative) return "-";
    if (s.sign == '+') return "+";
    if (s.sign == ' ') return " ";
    return "";
}

// --- float digits ---------------------------------------------------------------

// v (finite, >= 0) with `precision` digits after the point, in fixed or
// exponent notation, exactly as printf's %.*f / %.*e (and %#.*f / %#.*e
// with `alt`): std::to_chars, correctly rounded, without printf's locale
// and parsing; a precision too large for the stack buffer goes to the heap.
std::string fixedOrExp(double v, int precision, bool exponent, bool alt) {
    const std::chars_format form = exponent ? std::chars_format::scientific : std::chars_format::fixed;
    char stack[512];
    std::string heap;
    char* first = stack;
    char* last = stack + sizeof stack;
    const size_t need = static_cast<size_t>(precision) + (exponent ? 16 : 330);
    if (need > sizeof stack) {
        heap.resize(need);
        first = &heap[0];
        last = first + need;
    }
    const auto r = std::to_chars(first, last, v, form, precision);
    std::string out(first, r.ptr);
    if (alt && precision == 0) {
        // '#' keeps the point: "2." and "1.e+00".
        const size_t e = out.find('e');
        out.insert(e == std::string::npos ? out.size() : e, ".");
    }
    return out;
}

// The decimal digits of a finite v >= 0 rounded to `significant` digits
// (or the shortest round-trip digits when significant <= 0), and the
// position of the decimal point: v ~= 0.D * 10**decpt.
void decimalDigits(double v, int significant, std::string& digits, int& decpt) {
    std::string sci;
    if (significant <= 0) {
        char buf[64];
        auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
        sci.assign(buf, r.ptr);
    } else {
        sci = fixedOrExp(v, significant - 1, true, false);
    }
    const size_t e = sci.find('e');
    digits.clear();
    for (size_t k = 0; k < e; ++k)
        if (sci[k] != '.') digits.push_back(sci[k]);
    decpt = std::atoi(sci.c_str() + e + 1) + 1;
    if (v == 0.0) decpt = 1;
}

void stripTrailingZeros(std::string& digits) {
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
}

// Lays out digits D with decimal point position decpt, in fixed or
// exponent notation, as CPython's format_float_short does. `minFrac` pads
// the fraction with zeros; `dotAlways` keeps a lone '.'.
void placeDigits(const std::string& D, int decpt, bool useExp, char expChar, size_t minFrac, bool dotAlways,
                 bool addDot0, std::string& intPart, std::string& rest) {
    std::string frac;
    if (useExp) {
        intPart = D.substr(0, 1);
        frac = D.substr(1);
    } else if (decpt <= 0) {
        intPart = "0";
        frac = std::string(static_cast<size_t>(-decpt), '0') + D;
    } else if (static_cast<size_t>(decpt) >= D.size()) {
        intPart = D + std::string(static_cast<size_t>(decpt) - D.size(), '0');
    } else {
        intPart = D.substr(0, static_cast<size_t>(decpt));
        frac = D.substr(static_cast<size_t>(decpt));
    }
    if (frac.size() < minFrac) frac.append(minFrac - frac.size(), '0');
    rest.clear();
    if (!frac.empty()) rest = "." + frac;
    else if (addDot0 && !useExp) rest = ".0";
    else if (dotAlways) rest = ".";
    if (useExp) {
        const int x = decpt - 1;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%c%c%02d", expChar, x < 0 ? '-' : '+', x < 0 ? -x : x);
        rest += buf;
    }
}

// Splits printf-style output "123.456e+07" into its integer digits and the rest.
void splitFixed(const std::string& text, std::string& intPart, std::string& rest) {
    size_t k = 0;
    while (k < text.size() && text[k] >= '0' && text[k] <= '9') ++k;
    intPart = text.substr(0, k);
    rest = text.substr(k);
}

// Groups the fraction digits in `rest` (".dddde+XX" / ".ddd%") from the left.
std::string groupFraction(const std::string& rest, char sep) {
    if (!sep || rest.empty() || rest[0] != '.') return rest;
    size_t k = 1;
    while (k < rest.size() && rest[k] >= '0' && rest[k] <= '9') ++k;
    return "." + groupRight(rest.substr(1, k - 1), sep) + rest.substr(k);
}

bool allZeroDigits(const std::string& a, const std::string& b) {
    for (char c : a) if (c >= '1' && c <= '9') return false;
    for (char c : b) {
        if (c == 'e' || c == 'E') break;
        if (c >= '1' && c <= '9') return false;
    }
    return true;
}

// The float body for type t ('\0' for none) and precision p (-1 none).
void floatBody(double a, char t, int p, bool alt, std::string& intPart, std::string& rest) {
    std::string D;
    int decpt = 0;
    switch (t) {
        case 'f': case 'F':
            splitFixed(fixedOrExp(a, p < 0 ? 6 : p, false, alt), intPart, rest);
            return;
        case 'e': case 'E': {
            std::string s = fixedOrExp(a, p < 0 ? 6 : p, true, alt);
            if (t == 'E') for (char& c : s) if (c == 'e') c = 'E';
            splitFixed(s, intPart, rest);
            return;
        }
        case '%': {
            // CPython multiplies in binary floating point, then formats as 'f'.
            splitFixed(fixedOrExp(a * 100.0, p < 0 ? 6 : p, false, alt), intPart, rest);
            rest += "%";
            return;
        }
        case 'g': case 'G': case 'n': {
            const int pp = p < 0 ? 6 : (p == 0 ? 1 : p);
            decimalDigits(a, pp, D, decpt);
            if (!alt) stripTrailingZeros(D);
            const bool useExp = decpt <= -4 || decpt > pp;
            placeDigits(D, decpt, useExp, t == 'G' ? 'E' : 'e', 0, alt, false, intPart, rest);
            return;
        }
        default: {
            // No type: repr's digits without a precision, else 'g' with at
            // least one digit after the point.
            if (p < 0) {
                decimalDigits(a, 0, D, decpt);
                const bool useExp = decpt <= -4 || decpt > 16;
                placeDigits(D, decpt, useExp, 'e', 0, alt, true, intPart, rest);
            } else {
                const int pp = p == 0 ? 1 : p;
                decimalDigits(a, pp, D, decpt);
                if (!alt) stripTrailingZeros(D);
                const bool useExp = decpt <= -4 || decpt > pp - 1;
                placeDigits(D, decpt, useExp, 'e', 0, alt, true, intPart, rest);
            }
            return;
        }
    }
}

Result formatFloatSpec(double value, const Spec& s) {
    const char t = s.type.empty() ? '\0' : s.type[0];
    const bool upper = t == 'E' || t == 'F' || t == 'G';
    const bool negative = std::signbit(value) && !std::isnan(value);
    const double a = std::fabs(value);
    std::string intPart, rest;
    Spec eff = s;
    bool neg = negative;
    // '%' scales first: a finite value can overflow to inf.
    if (std::isnan(value) || std::isinf(value) || (t == '%' && std::isinf(value * 100.0))) {
        rest = std::isnan(value) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        if (t == '%') rest += "%";
        eff.grouping = '\0';
    } else {
        floatBody(a, t, s.precision, s.alternate, intPart, rest);
        rest = groupFraction(rest, s.fracGrouping);
        if (t == 'n') eff.grouping = '\0';
        if (neg && s.noNegZero && allZeroDigits(intPart, rest)) neg = false;
    }
    Result r;
    r.text = layout(eff, signFor(s, neg), "", intPart, rest, 3);
    return r;
}

bool floatTypeAllowed(const std::string& type) {
    if (type.empty()) return true;
    if (type.size() != 1) return false;
    switch (type[0]) {
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'n': case '%': return true;
        default: return false;
    }
}

} // namespace

std::string floatRepr(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    std::string D;
    int decpt = 0;
    decimalDigits(std::fabs(value), 0, D, decpt);
    std::string intPart, rest;
    placeDigits(D, decpt, decpt <= -4 || decpt > 16, 'e', 0, false, true, intPart, rest);
    return (std::signbit(value) ? "-" : "") + intPart + rest;
}

Result formatFloat(double value, const std::string& spec) {
    if (spec.empty()) {
        Result r;
        r.text = floatRepr(value);
        return r;
    }
    Spec s;
    Result err;
    if (!parseSpec(spec, s, err, "float")) return err;
    if (!floatTypeAllowed(s.type)) return fail(Error::Value, unknownCode(s.type, "float"));
    return formatFloatSpec(value, s);
}

Result formatInt(bool negative, const std::function<std::string(int)>& digits,
                 const std::function<bool(double&)>& toDouble, const std::string& spec, bool isBool,
                 bool boolValue) {
    if (spec.empty()) {
        Result r;
        if (isBool) r.text = boolValue ? "True" : "False";
        else r.text = (negative ? "-" : "") + digits(10);
        return r;
    }
    Spec s;
    Result err;
    if (!parseSpec(spec, s, err, "int")) return err;
    const char t = s.type.size() == 1 ? s.type[0] : (s.type.empty() ? 'd' : '\x01');
    switch (t) {
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case '%': {
            double d = 0.0;
            if (!toDouble(d)) return fail(Error::Overflow, "int too large to convert to float");
            return formatFloatSpec(d, s);
        }
        case 'd': case 'n': case 'b': case 'o': case 'x': case 'X': case 'c': break;
        default:
            return fail(Error::Value, unknownCode(s.type, "int"));
    }
    if (s.precision >= 0) return fail(Error::Value, "Precision not allowed in integer format specifier");
    if (s.noNegZero) return fail(Error::Value, "Negative zero coercion (z) not allowed in integer format specifier");
    if (t == 'c') {
        if (s.sign) return fail(Error::Value, "Sign not allowed with integer format specifier 'c'");
        if (s.alternate) return fail(Error::Value, "Alternate form (#) not allowed with integer format specifier 'c'");
        const std::string dec = digits(10);
        // C long: -2**63 .. 2**63-1.
        const char* limit = negative ? "9223372036854775808" : "9223372036854775807";
        if (dec.size() > 19 || (dec.size() == 19 && dec > limit))
            return fail(Error::Overflow, "Python int too large to convert to C long");
        const unsigned long long cp = std::strtoull(dec.c_str(), nullptr, 10);
        if (negative || cp > 0x10FFFF) return fail(Error::Overflow, "%c arg not in range(0x110000)");
        std::string ch;
        if (cp < 0x80) ch.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            ch.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            ch.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            ch.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            ch.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            ch.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            ch.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            ch.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            ch.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            ch.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        Result r;
        r.text = layout(s, "", "", "", ch, 3);
        return r;
    }
    const int base = t == 'b' ? 2 : t == 'o' ? 8 : (t == 'x' || t == 'X') ? 16 : 10;
    std::string d = digits(base);
    if (t == 'X') for (char& c : d) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
    std::string prefix;
    if (s.alternate) {
        if (t == 'b') prefix = "0b";
        else if (t == 'o') prefix = "0o";
        else if (t == 'x') prefix = "0x";
        else if (t == 'X') prefix = "0X";
    }
    Spec eff = s;
    if (t == 'n') eff.grouping = '\0';
    Result r;
    r.text = layout(eff, signFor(s, negative), prefix, d, "", base == 10 ? 3 : 4);
    return r;
}

Result formatString(const std::string& value, const std::string& spec) {
    Result r;
    if (spec.empty()) { r.text = value; return r; }
    Spec s;
    if (!parseSpec(spec, s, r, "str", '<', "s")) return r;
    if (!s.type.empty() && s.type != "s") return fail(Error::Value, unknownCode(s.type, "str"));
    if (s.sign) return fail(Error::Value, "Sign not allowed in string format specifier");
    if (s.noNegZero) return fail(Error::Value, "Negative zero coercion (z) not allowed in string format specifier");
    if (s.alternate) return fail(Error::Value, "Alternate form (#) not allowed in string format specifier");
    if (s.align == '=') return fail(Error::Value, "'=' alignment not allowed in string format specifier");
    // The precision is a number of code points.
    std::string text = value;
    if (s.precision >= 0) {
        size_t at = 0;
        for (int k = 0; k < s.precision && at < text.size(); ++k) at += codePointLength(text, at);
        text.resize(at);
    }
    if (!s.align) s.align = '<';
    r.text = layout(s, "", "", "", text, 3);
    return r;
}

std::string percentFloat(double value, const std::string& flags, int width, int precision, char conversion) {
    Spec s;
    const bool left = flags.find('-') != std::string::npos;
    if (flags.find('+') != std::string::npos) s.sign = '+';
    else if (flags.find(' ') != std::string::npos) s.sign = ' ';
    s.alternate = flags.find('#') != std::string::npos;
    if (left) s.align = '<';
    else if (flags.find('0') != std::string::npos && std::isfinite(value)) { s.fill = "0"; s.align = '='; }
    s.width = width;
    s.precision = precision < 0 ? 6 : precision;
    s.type = std::string(1, conversion);
    return formatFloatSpec(value, s).text;
}

} // namespace numfmt
} // namespace protoPython
