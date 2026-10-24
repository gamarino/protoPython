#include "DapJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace protoPython {
namespace dap {

namespace {

const Json& nullJson() {
    static const Json n;
    return n;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Length of the valid UTF-8 sequence at s[i], or 0 when it is not one.
std::size_t utf8SequenceLength(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len;
    uint32_t min;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) { len = 2; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { len = 3; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { len = 4; min = 0x10000; }
    else return 0;
    if (i + len > s.size()) return 0;
    uint32_t cp = c & (0xFF >> (len + 1));
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char d = static_cast<unsigned char>(s[i + k]);
        if ((d & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (d & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return len;
}

void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) {
            const std::size_t len = utf8SequenceLength(s, i);
            if (len == 0) {
                out += "\xEF\xBF\xBD";  // U+FFFD
                ++i;
            } else {
                out.append(s, i, len);
                i += len;
            }
            continue;
        }
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
        ++i;
    }
    out += '"';
}

class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}

    bool parseDocument(Json& out) {
        if (!parseValue(out, 0)) return false;
        skipWs();
        return p_ == t_.size();
    }

private:
    void skipWs() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_;
    }
    bool literal(const char* word) {
        std::size_t n = 0;
        while (word[n]) ++n;
        if (t_.compare(p_, n, word) != 0) return false;
        p_ += n;
        return true;
    }
    bool hex4(uint32_t& v) {
        if (p_ + 4 > t_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool parseString(std::string& out) {
        if (p_ >= t_.size() || t_[p_] != '"') return false;
        ++p_;
        while (p_ < t_.size()) {
            const char c = t_[p_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p_ >= t_.size()) return false;
            const char e = t_[p_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p_ + 1 < t_.size() && t_[p_] == '\\' && t_[p_ + 1] == 'u') {
                        p_ += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { appendUtf8(out, 0xFFFD); cp = lo; }
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                    appendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool parseValue(Json& out, int depth) {
        if (depth > 200) return false;
        skipWs();
        if (p_ >= t_.size()) return false;
        const char c = t_[p_];
        if (c == '{') {
            ++p_;
            out = Json::object();
            skipWs();
            if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
            for (;;) {
                skipWs();
                std::string key;
                if (!parseString(key)) return false;
                skipWs();
                if (p_ >= t_.size() || t_[p_] != ':') return false;
                ++p_;
                Json v;
                if (!parseValue(v, depth + 1)) return false;
                out[key] = std::move(v);
                skipWs();
                if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
                if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p_;
            out = Json::array();
            skipWs();
            if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
            for (;;) {
                Json v;
                if (!parseValue(v, depth + 1)) return false;
                out.push(std::move(v));
                skipWs();
                if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
                if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = Json(std::move(s));
            return true;
        }
        if (literal("true")) { out = Json(true); return true; }
        if (literal("false")) { out = Json(false); return true; }
        if (literal("null")) { out = Json(); return true; }
        // number
        const std::size_t start = p_;
        if (p_ < t_.size() && (t_[p_] == '-' || t_[p_] == '+')) ++p_;
        while (p_ < t_.size() && ((t_[p_] >= '0' && t_[p_] <= '9') || t_[p_] == '.' || t_[p_] == 'e' ||
                                  t_[p_] == 'E' || t_[p_] == '-' || t_[p_] == '+'))
            ++p_;
        if (p_ == start) return false;
        const std::string num = t_.substr(start, p_ - start);
        char* end = nullptr;
        const double d = std::strtod(num.c_str(), &end);
        if (!end || *end != '\0') return false;
        out = Json(d);
        return true;
    }

    const std::string& t_;
    std::size_t p_ = 0;
};

} // namespace

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) { *this = object(); }
    for (auto& m : members_)
        if (m.first == key) return m.second;
    members_.emplace_back(key, Json());
    return members_.back().second;
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}

const Json& Json::get(const std::string& key) const {
    const Json* j = find(key);
    return j ? *j : nullJson();
}

std::string Json::getString(const std::string& key, const std::string& def) const {
    const Json* j = find(key);
    return (j && j->isString()) ? j->str_ : def;
}

long long Json::getInt(const std::string& key, long long def) const {
    const Json* j = find(key);
    return (j && j->isNumber()) ? static_cast<long long>(j->num_) : def;
}

bool Json::getBool(const std::string& key, bool def) const {
    const Json* j = find(key);
    return (j && j->isBool()) ? j->bool_ : def;
}

void Json::push(Json v) {
    if (type_ != Type::Array) { *this = array(); }
    items_.push_back(std::move(v));
}

std::size_t Json::size() const { return type_ == Type::Array ? items_.size() : 0; }

const Json& Json::at(std::size_t i) const { return i < items_.size() ? items_[i] : nullJson(); }

void Json::dumpTo(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[32];
            if (std::isfinite(num_) && num_ == std::floor(num_) && std::fabs(num_) < 9e15)
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(num_));
            else if (std::isfinite(num_))
                std::snprintf(buf, sizeof buf, "%.17g", num_);
            else
                std::snprintf(buf, sizeof buf, "null");
            out += buf;
            break;
        }
        case Type::String: dumpString(str_, out); break;
        case Type::Array: {
            out += '[';
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i) out += ',';
                items_[i].dumpTo(out);
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            for (std::size_t i = 0; i < members_.size(); ++i) {
                if (i) out += ',';
                dumpString(members_[i].first, out);
                out += ':';
                members_[i].second.dumpTo(out);
            }
            out += '}';
            break;
        }
    }
}

std::string Json::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

std::optional<Json> Json::parse(const std::string& text) {
    Json j;
    Parser p(text);
    if (!p.parseDocument(j)) return std::nullopt;
    return j;
}

} // namespace dap
} // namespace protoPython
