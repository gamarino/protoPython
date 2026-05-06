#ifndef PROTOPYTHON_TOKENIZER_H
#define PROTOPYTHON_TOKENIZER_H

#include <string>
#include <cstddef>
#include <vector>

namespace protoPython {

enum class TokenType {
    Number,
    String,
    Bytes,
    FString,
    Name,
    Plus,
    Minus,
    Star,
    Slash,
    LParen,
    RParen,
    Comma,
    Newline,
    EndOfFile,
    // Step 5: attribute, subscript, literals, control flow
    Dot,
    LSquare,
    RSquare,
    LCurly,
    RCurly,
    Colon,
    Assign,
    EqEqual,  /* == */
    For,
    In,
    If,
    Elif,
    Else,
    Global,
    Def,
    Pass,
    Indent,
    Dedent,
    Import,
    From,
    Class,
    Return,
    While,
    True,
    False,
    None,
    And,
    Or,
    Not,
    Try,
    Except,
    Finally,
    Raise,
    Break,
    Continue,
    Lambda,
    With,
    As,
    Is,
    IsNot,
    NotIn,
    Modulo,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Yield,
    Ellipsis,
    Error,
    Semicolon,
    PlusAssign,    /* += */
    MinusAssign,   /* -= */
    StarAssign,    /* *= */
    SlashAssign,   /* /= */
    Tilde,         /* ~ */
    Del,
    Assert,
    Nonlocal,
    At,            /* @ */
    DoubleStar,    /* ** */
    Async,
    Await,
    BitAnd,        /* & */
    BitOr,         /* | */
    BitXor,        /* ^ */
    LShift,        /* << */
    RShift,        /* >> */
    AndAssign,     /* &= */
    OrAssign,      /* |= */
    XorAssign,     /* ^= */
    LShiftAssign,  /* <<= */
    RShiftAssign,  /* >>= */
    Arrow,         /* -> */
    DoubleSlash,    /* // */
    DoubleSlashAssign, /* //= */
    Walrus,        /* := */
    AtAssign,      /* @= */
    ModuloAssign,  /* %= */
    DoubleStarAssign, /* **= */
    Type,          /* type (PEP 695) */
    Match,         /* match (soft keyword) */
    Case,          /* case (soft keyword) */
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string value;
    double numValue = 0.0;
    bool isInteger = false;
    long long intValue = 0;
    int line = 1;
    int column = 1;
    // Set when the integer literal overflows int64 and we want the
    // compiler to emit a bignum constant instead.  `bigDigits` is the
    // raw digit run (with underscores stripped, prefix stripped for
    // 0x/0o/0b), `bigBase` is 2/8/10/16.
    std::string bigDigits;
    int bigBase = 10;
    // Non-empty when the lexer wants the compiler to emit a SyntaxWarning
    // *as if* it were attached to this token's source position.  Used for
    // the "invalid <kind> literal" warning emitted when a numeric literal
    // is immediately followed by a Python keyword (e.g. "9and x") — see
    // CPython test_end_of_numerical_literals.  Empty for all other tokens.
    std::string pendingWarning;
    // True when this is an FString token whose source prefix included
    // 't' (a PEP 750 template string).  Distinguishes t"..." from f"..."
    // for code paths that need to report the runtime type — most
    // visibly the missed-comma SyntaxWarning analyzer, which formats
    // t-strings as "string.templatelib.Template".  Set only when
    // type == FString.
    bool isTString = false;
};

/** Minimal Python tokenizer for expressions and simple statements. */
class Tokenizer {
public:
    explicit Tokenizer(const std::string& source);
    Token next();
    const Token& peek();
    bool hasNext() const;

    int getLine() const { return line_; }
    int getColumn() const { return pos_ - lineStartPos_ + 1; }

private:
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    size_t lineStartPos_ = 0;
    Token peeked_;
    bool hasPeeked_ = false;
    bool atLineStart_ = true;
    std::vector<int> indentStack_;
    int nestingLevel_ = 0;
    void skipWhitespace();
    void skipWhitespaceNoNewline();
    void skipComment();
    Token scanNumber();
    Token scanString(char quote, const std::string& prefix = "");
    Token scanNameOrKeyword();
    Token makeToken(TokenType type);
    // Defined in Tokenizer.cpp.  Inspects the character at pos_ after
    // scanNumber consumed a numeric literal: if it is an identifier-start
    // char or non-ASCII, sets t.pendingWarning (keyword follow-on) or
    // t.type=Error (non-keyword identifier or non-ASCII).  Returns true
    // when the caller should bail with the Error token.  See definition
    // for the full message-format contract.
    bool diagnoseNumberFollowOn(Token& t, const char* kindName);
};

} // namespace protoPython

#endif
