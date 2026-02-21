#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    // Keywords
    TK_FN, TK_VAR, TK_PAT, TK_IMPORT, TK_PUB,
    TK_FOR, TK_WHILE, TK_SWITCH, TK_CASE, TK_DEFAULT,
    TK_BREAK, TK_YIELD, TK_RETURN,
    TK_COPY, TK_MOVE,
    TK_NULL,
    TK_AS, TK_OF,
    TK_STATIC, TK_CONST, TK_CONSTEXPR,

    // Literals
    TK_INT_LIT, TK_FLOAT_LIT, TK_STR_LIT,

    // Identifiers
    TK_IDENT,

    // Statement terminators
    TK_NEWLINE, TK_SEMI,

    // Brackets
    TK_LBRACE, TK_RBRACE,
    TK_LPAREN, TK_RPAREN,
    TK_LBRACKET, TK_RBRACKET,
    TK_LT, TK_GT,

    // Punctuation
    TK_COMMA, TK_DOT, TK_COLON, TK_DCOLON, TK_ARROW,

    // Assignment
    TK_EQ,

    // Arithmetic operators
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,

    // Comparison operators
    TK_LEQ, TK_GEQ, TK_EQEQ, TK_NEQ,

    // Bitwise operators
    TK_AMP, TK_PIPE, TK_CARET, TK_TILDE,
    TK_LSHIFT, TK_RSHIFT,

    // Logical operators
    TK_BANG, TK_ANDAND, TK_OROR,

    // Conditional
    TK_QUESTION,

    // Custom operator (quoted, e.g. "+>")
    TK_OP_CUSTOM,

    TK_EOF,
    TK_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    char *value;   /* heap-allocated string representation */
    int line;
    int col;
} Token;

typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
    /* depth counters for smart newline handling */
    int paren_depth;
    int bracket_depth;
    int brace_depth;
    /* last real token type (for newline-as-terminator logic) */
    TokenType last_real;
    /* peek buffer */
    Token peek_buf;
    int has_peek;
} Lexer;

void lexer_init(Lexer *lex, const char *src);
Token lexer_next(Lexer *lex);
Token lexer_peek(Lexer *lex);
void token_free(Token *tok);
const char *token_type_str(TokenType t);

#endif /* LEXER_H */
