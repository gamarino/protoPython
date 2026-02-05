#include <protoPython/Tokenizer.h>
#include <cctype>
#include <stdexcept>

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
    if (t.value == "for") t.type = TokenType::For;
    else if (t.value == "in") t.type = TokenType::In;
    else if (t.value == "if") t.type = TokenType::If;
    else if (t.value == "else") t.type = TokenType::Else;
    else if (t.value == "global") t.type = TokenType::Global;
    else if (t.value == "def") t.type = TokenType::Def;
    else if (t.value == "pass") t.type = TokenType::Pass;
    return t;
}

Token Tokenizer::next() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }
    /* After a newline we are at line start: count indentation and emit Indent/Dedent. */
    if (atLineStart_) {
        atLineStart_ = false;
        if (pos_ >= source_.size()) {
            Token t;
            t.type = TokenType::EndOfFile;
            return t;
        }
        int indent = 0;
        while (pos_ < source_.size() && (source_[pos_] == ' ' || source_[pos_] == '\t')) {
            indent += (source_[pos_] == '\t') ? 1 : 1;
            pos_++;
        }
        if (pos_ < source_.size() && source_[pos_] == '#') {
            /* Comment-only line: skip to newline and re-enter at line start. */
            skipComment();
            if (pos_ < source_.size() && source_[pos_] == '\n') {
                pos_++;
                atLineStart_ = true;
            }
            return next();
        }
        if (indent > indentStack_.back()) {
            indentStack_.push_back(indent);
            Token t;
            t.type = TokenType::Indent;
            return t;
        }
        if (indent < indentStack_.back()) {
            if (indentStack_.size() > 1)
                indentStack_.pop_back();
            Token t;
            t.type = TokenType::Dedent;
            return t;
        }
        /* Same indent: fall through to read the next token (no skipWhitespace, we're past indent). */
    } else {
        skipWhitespace();
    }
    if (pos_ >= source_.size()) {
        Token t;
        t.type = TokenType::EndOfFile;
        return t;
    }
    char c = source_[pos_];
    if (c == '\n') {
        pos_++;
        atLineStart_ = true;
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
    if (c == '.') { pos_++; Token t; t.type = TokenType::Dot; return t; }
    if (c == '[') { pos_++; Token t; t.type = TokenType::LSquare; return t; }
    if (c == ']') { pos_++; Token t; t.type = TokenType::RSquare; return t; }
    if (c == '{') { pos_++; Token t; t.type = TokenType::LCurly; return t; }
    if (c == '}') { pos_++; Token t; t.type = TokenType::RCurly; return t; }
    if (c == ':') { pos_++; Token t; t.type = TokenType::Colon; return t; }
    if (c == '=' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        pos_ += 2; Token t; t.type = TokenType::EqEqual; return t;
    }
    if (c == '=') { pos_++; Token t; t.type = TokenType::Assign; return t; }
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
