#pragma once
// A small JSON value for the Debug Adapter Protocol: parse a message, build and
// serialize a reply. Objects keep their keys in insertion order. Serialization
// replaces bytes that are not UTF-8 with U+FFFD, so a script printing arbitrary
// bytes never produces an invalid message.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace protoPython {
namespace dap {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int n) : type_(Type::Number), num_(n) {}
    Json(long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    Json(long long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    Json(unsigned long long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    Json(unsigned long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    Json(unsigned n) : type_(Type::Number), num_(n) {}
    Json(double d) : type_(Type::Number), num_(d) {}
    Json(const char* s) : type_(Type::String), str_(s) {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isBool() const { return type_ == Type::Bool; }

    // Object access. operator[] inserts (turning a null into an object).
    Json& operator[](const std::string& key);
    const Json* find(const std::string& key) const;
    bool contains(const std::string& key) const { return find(key) != nullptr; }
    std::string getString(const std::string& key, const std::string& def = "") const;
    long long getInt(const std::string& key, long long def = 0) const;
    bool getBool(const std::string& key, bool def = false) const;
    const Json& get(const std::string& key) const;  // a null Json when absent

    // Array access.
    void push(Json v);
    std::size_t size() const;
    const Json& at(std::size_t i) const;

    const std::vector<std::pair<std::string, Json>>& members() const { return members_; }

    const std::string& str() const { return str_; }
    double num() const { return num_; }
    bool boolean() const { return bool_; }

    std::string dump() const;
    static std::optional<Json> parse(const std::string& text);

private:
    void dumpTo(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};

} // namespace dap
} // namespace protoPython
