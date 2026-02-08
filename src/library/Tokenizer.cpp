#include <protoPython/Tokenizer.h>
#include <cctype>
#include <stdexcept>
#include <iostream>

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
    Token t = makeToken(TokenType::Number);
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
    Token t = makeToken(TokenType::String);
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
        if (c == '\\' && pos_ < source_.size()) {
            char e = source_[pos_++];
            if (e == 'n') s += '\n';
            else if (e == 't') s += '\t';
            else if (e == 'r') s += '\r';
            else if (e == quote) s += quote;
            else if (e == '\\') s += '\\';
            else s += e;
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
    else if (t.value == "lambda") t.type = TokenType::Lambda;
    else if (t.value == "with") t.type = TokenType::With;
    else if (t.value == "as") t.type = TokenType::As;
    else if (t.value == "is") t.type = TokenType::Is;
    else if (t.value == "yield") t.type = TokenType::Yield;
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
        return makeToken(TokenType::EndOfFile);
    }
    char c = source_[pos_];
    if (c == '\n') {
        Token t = makeToken(TokenType::Newline);
        pos_++;
        line_++;
        lineStartPos_ = pos_;
        atLineStart_ = true;
        return t;
    }
    if (c == '+') { Token t = makeToken(TokenType::Plus); pos_++; return t; }
    if (c == '-') { Token t = makeToken(TokenType::Minus); pos_++; return t; }
    if (c == '*') { Token t = makeToken(TokenType::Star); pos_++; return t; }
    if (c == '/') { Token t = makeToken(TokenType::Slash); pos_++; return t; }
    if (c == '(') { Token t = makeToken(TokenType::LParen); pos_++; return t; }
    if (c == ')') { Token t = makeToken(TokenType::RParen); pos_++; return t; }
    if (c == ',') { Token t = makeToken(TokenType::Comma); pos_++; return t; }
    if (c == '.') { Token t = makeToken(TokenType::Dot); pos_++; return t; }
    if (c == '[') { Token t = makeToken(TokenType::LSquare); pos_++; return t; }
    if (c == ']') { Token t = makeToken(TokenType::RSquare); pos_++; return t; }
    if (c == '{') { Token t = makeToken(TokenType::LCurly); pos_++; return t; }
    if (c == '}') { Token t = makeToken(TokenType::RCurly); pos_++; return t; }
    if (c == ':') { Token t = makeToken(TokenType::Colon); pos_++; return t; }
    if (c == ';') { Token t = makeToken(TokenType::Semicolon); pos_++; return t; }
    if (c == '%') { Token t = makeToken(TokenType::Modulo); pos_++; return t; }
    if (c == '!' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        Token t = makeToken(TokenType::NotEqual); pos_ += 2; return t;
    }
    if (c == '<' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        Token t = makeToken(TokenType::LessEqual); pos_ += 2; return t;
    }
    if (c == '<') { Token t = makeToken(TokenType::Less); pos_++; return t; }
    if (c == '>' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        Token t = makeToken(TokenType::GreaterEqual); pos_ += 2; return t;
    }
    if (c == '>') { Token t = makeToken(TokenType::Greater); pos_++; return t; }
    if (c == '=' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        Token t = makeToken(TokenType::EqEqual); pos_ += 2; return t;
    }
    if (c == '=') { Token t = makeToken(TokenType::Assign); pos_++; return t; }
    if (c == '"' || c == '\'')
        return scanString(c);
    if (std::isdigit(static_cast<unsigned char>(c)))
        return scanNumber();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        return scanNameOrKeyword();
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
