#include <protoPython/Tokenizer.h>
#include <cctype>
#include <stdexcept>

namespace protoPython {

Tokenizer::Tokenizer(const std::string& source) : source_(source) {}

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

void Tokenizer::skipComment() {
    while (pos_ < source_.size() && source_[pos_] != '\n')
        pos_++;
}

Token Tokenizer::scanNumber() {
    Token t;
    t.type = TokenType::Number;
    size_t start = pos_;
    bool isFloat = false;
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == '.') {
            isFloat = true;
            pos_++;
        } else if (std::isdigit(static_cast<unsigned char>(c)))
            pos_++;
        else
            break;
    }
    t.value = source_.substr(start, pos_ - start);
    if (isFloat) {
        t.numValue = std::stod(t.value);
        t.isInteger = false;
    } else {
        t.intValue = std::stoll(t.value);
        t.numValue = static_cast<double>(t.intValue);
        t.isInteger = true;
    }
    return t;
}

Token Tokenizer::scanString(char quote) {
    Token t;
    t.type = TokenType::String;
    pos_++; // consume opening quote
    std::string s;
    while (pos_ < source_.size()) {
        char c = source_[pos_++];
        if (c == quote)
            break;
        if (c == '\\' && pos_ < source_.size()) {
            char e = source_[pos_++];
            if (e == 'n') s += '\n';
            else if (e == 't') s += '\t';
            else if (e == 'r') s += '\r';
            else if (e == '"' || e == '\'') s += e;
            else s += e;
        } else
            s += c;
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
    Token t;
    t.type = TokenType::Name;
    t.value = source_.substr(start, pos_ - start);
    return t;
}

Token Tokenizer::next() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }
    skipWhitespace();
    if (pos_ >= source_.size()) {
        Token t;
        t.type = TokenType::EndOfFile;
        return t;
    }
    char c = source_[pos_];
    if (c == '\n') {
        pos_++;
        Token t;
        t.type = TokenType::Newline;
        return t;
    }
    if (c == '+') { pos_++; Token t; t.type = TokenType::Plus; return t; }
    if (c == '-') { pos_++; Token t; t.type = TokenType::Minus; return t; }
    if (c == '*') { pos_++; Token t; t.type = TokenType::Star; return t; }
    if (c == '/') { pos_++; Token t; t.type = TokenType::Slash; return t; }
    if (c == '(') { pos_++; Token t; t.type = TokenType::LParen; return t; }
    if (c == ')') { pos_++; Token t; t.type = TokenType::RParen; return t; }
    if (c == ',') { pos_++; Token t; t.type = TokenType::Comma; return t; }
    if (c == '"' || c == '\'')
        return scanString(c);
    if (std::isdigit(static_cast<unsigned char>(c)))
        return scanNumber();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        return scanNameOrKeyword();
    pos_++;
    Token t;
    t.type = TokenType::EndOfFile;
    return t;
}

const Token& Tokenizer::peek() {
    if (!hasPeeked_) {
        peeked_ = next();
        hasPeeked_ = true;
    }
    return peeked_;
}

bool Tokenizer::hasNext() const {
    return pos_ < source_.size() || hasPeeked_;
}

} // namespace protoPython
