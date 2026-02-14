#include <protoPython/Tokenizer.h>
#include <cctype>
#include <stdexcept>
#include <iostream>
#include <algorithm>

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
    bool isComplex = false;
    
    // Check for base prefixes: 0x, 0o, 0b
    if (pos_ + 1 < source_.size() && source_[pos_] == '0') {
        char nextC = source_[pos_ + 1];
        if (nextC == 'x' || nextC == 'X' || nextC == 'o' || nextC == 'O' || nextC == 'b' || nextC == 'B') {
            pos_ += 2;
            int base = (nextC == 'x' || nextC == 'X') ? 16 : (nextC == 'o' || nextC == 'O') ? 8 : 2;
            while (pos_ < source_.size()) {
                char c = source_[pos_];
                bool valid = false;
                if (base == 16) valid = std::isxdigit(static_cast<unsigned char>(c));
                else if (base == 8) valid = (c >= '0' && c <= '7');
                else if (base == 2) valid = (c == '0' || c == '1');
                
                if (valid || c == '_') {
                    pos_++;
                } else break;
            }
            t.value = source_.substr(start, pos_ - start);
            std::string cleanValue = t.value;
            cleanValue.erase(std::remove(cleanValue.begin(), cleanValue.end(), '_'), cleanValue.end());
            try {
                if (base == 10) {
                    t.intValue = std::stoll(cleanValue, nullptr, 10);
                } else {
                    t.intValue = static_cast<long long>(std::stoull(cleanValue, nullptr, 0));
                }
                t.numValue = static_cast<double>(t.intValue);
                t.isInteger = true;
            } catch (const std::out_of_range&) {
                // Manual conversion to double for huge non-decimal literals
                double val = 0;
                size_t i = 2; // skip 0x, 0o, 0b
                for (; i < cleanValue.size(); ++i) {
                    char c = cleanValue[i];
                    int digit = 0;
                    if (c >= '0' && c <= '9') digit = c - '0';
                    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    val = val * base + digit;
                }
                t.numValue = val;
                t.isInteger = false;
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
            isFloat = true;
            pos_++;
            if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) pos_++;
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
                // Fallback to double if it exceeds 64-bit int
                t.numValue = std::stod(cleanValue);
                t.isInteger = false;
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
    for (char c : prefix) {
        char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lc == 'r') isRaw = true;
        if (lc == 'f') isF = true;
    }
    if (isF) t.type = TokenType::FString;
    
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
            if (!isRaw) {
                char e = source_[pos_++];
                if (e == 'n') s += '\n';
                else if (e == 't') s += '\t';
                else if (e == 'r') s += '\r';
                else if (e == quote) s += quote;
                else if (e == '\\') s += '\\';
                else if (e == '\n') {
                    line_++;
                    lineStartPos_ = pos_;
                }
                else s += e;
            } else {
                // In raw strings, a backslash followed by a quote does NOT end the string.
                // Both characters are added to the value.
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
        return makeToken(TokenType::EndOfFile);
    }
    char c = source_[pos_];
    if (std::getenv("PROTO_ENV_DIAG")) {
        // Only print every 100 tokens to avoid flooding if it's just slow
        static int count = 0;
        if (count++ % 100 == 0) {
            std::cerr << "[proto-diag] Tokenizer::next: pos=" << pos_ << "/" << source_.size() << " char='" << c << "'\n";
        }
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
            if (p == 'f' || p == 'r' || p == 'b' || p == 'u') tempPos++;
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
