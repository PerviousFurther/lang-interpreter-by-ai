#define _POSIX_C_SOURCE 200809L
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void lexer_init(Lexer *lex, const char *src) {
    lex->src = src;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->paren_depth = 0;
    lex->bracket_depth = 0;
    lex->brace_depth = 0;
    lex->last_real = TK_EOF;
    lex->has_peek = 0;
}

static char cur(Lexer *lex) {
    return lex->src[lex->pos];
}

static char peek_ch(Lexer *lex) {
    if (lex->src[lex->pos] == '\0') return '\0';
    return lex->src[lex->pos + 1];
}

static char advance(Lexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else lex->col++;
    return c;
}

static Token make_tok(TokenType t, const char *val, int line, int col) {
    Token tok;
    tok.type = t;
    tok.value = val ? strdup(val) : NULL;
    tok.line = line;
    tok.col = col;
    return tok;
}

static Token make_tok_char(TokenType t, char c, int line, int col) {
    char buf[2] = {c, 0};
    return make_tok(t, buf, line, col);
}

/* tokens that can end a statement */
static int can_end_stmt(TokenType t) {
    switch (t) {
        case TK_INT_LIT: case TK_FLOAT_LIT: case TK_STR_LIT:
        case TK_IDENT: case TK_NULL:
        case TK_RBRACE: case TK_RPAREN: case TK_RBRACKET: case TK_GT:
        case TK_BREAK: case TK_YIELD: case TK_RETURN:
            return 1;
        default:
            return 0;
    }
}

static void skip_line_comment(Lexer *lex) {
    while (cur(lex) != '\0' && cur(lex) != '\n') advance(lex);
}

static void skip_block_comment(Lexer *lex) {
    /* skip block comment delimited by slash-star ... star-slash */
    advance(lex); advance(lex); /* consume / * */
    while (cur(lex) != '\0') {
        if (cur(lex) == '*' && peek_ch(lex) == '/') {
            advance(lex); advance(lex);
            return;
        }
        advance(lex);
    }
}

static Token lex_string(Lexer *lex, char quote) {
    int line = lex->line, col = lex->col;
    advance(lex); /* consume opening quote */
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    while (cur(lex) != '\0' && cur(lex) != quote) {
        char c = cur(lex);
        if (c == '\\') {
            advance(lex);
            char esc = advance(lex);
            switch (esc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                default: c = esc; break;
            }
        } else {
            advance(lex);
        }
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = c;
    }
    if (cur(lex) == quote) advance(lex);
    buf[len] = '\0';
    Token tok;
    tok.type = TK_STR_LIT;
    tok.value = buf;
    tok.line = line;
    tok.col = col;
    return tok;
}

static Token lex_number(Lexer *lex) {
    int line = lex->line, col = lex->col;
    size_t cap = 32, len = 0;
    char *buf = malloc(cap);
    int is_float = 0;
    while (isdigit(cur(lex))) {
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }
    if (cur(lex) == '.' && isdigit(peek_ch(lex))) {
        is_float = 1;
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
        while (isdigit(cur(lex))) {
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = advance(lex);
        }
    }
    /* optional exponent */
    if ((cur(lex) == 'e' || cur(lex) == 'E')) {
        is_float = 1;
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
        if (cur(lex) == '+' || cur(lex) == '-') {
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = advance(lex);
        }
        while (isdigit(cur(lex))) {
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = advance(lex);
        }
    }
    buf[len] = '\0';
    Token tok;
    tok.type = is_float ? TK_FLOAT_LIT : TK_INT_LIT;
    tok.value = buf;
    tok.line = line;
    tok.col = col;
    return tok;
}

static Token lex_ident_or_kw(Lexer *lex) {
    int line = lex->line, col = lex->col;
    size_t cap = 32, len = 0;
    char *buf = malloc(cap);
    while (isalnum(cur(lex)) || cur(lex) == '_') {
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }
    buf[len] = '\0';

    static const struct { const char *kw; TokenType t; } kws[] = {
        {"fn", TK_FN}, {"var", TK_VAR}, {"pat", TK_PAT},
        {"import", TK_IMPORT}, {"pub", TK_PUB},
        {"for", TK_FOR}, {"while", TK_WHILE},
        {"switch", TK_SWITCH}, {"case", TK_CASE},
        {"default", TK_DEFAULT}, {"break", TK_BREAK},
        {"yield", TK_YIELD}, {"return", TK_RETURN},
        {"copy", TK_COPY}, {"move", TK_MOVE},
        {"null", TK_NULL},
        {"as", TK_AS}, {"of", TK_OF},
        {"static", TK_STATIC}, {"const", TK_CONST},
        {"constexpr", TK_CONSTEXPR},
        {NULL, TK_EOF}
    };
    for (int i = 0; kws[i].kw; i++) {
        if (strcmp(buf, kws[i].kw) == 0) {
            Token tok = make_tok(kws[i].t, buf, line, col);
            free(buf);
            return tok;
        }
    }
    Token tok;
    tok.type = TK_IDENT;
    tok.value = buf;
    tok.line = line;
    tok.col = col;
    return tok;
}

/* lex a quoted custom operator name like "+" or "+>" */
static Token lex_custom_op(Lexer *lex) {
    int line = lex->line, col = lex->col;
    advance(lex); /* consume " */
    size_t cap = 16, len = 0;
    char *buf = malloc(cap);
    while (cur(lex) != '"' && cur(lex) != '\0') {
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }
    if (cur(lex) == '"') advance(lex);
    buf[len] = '\0';
    Token tok;
    tok.type = TK_OP_CUSTOM;
    tok.value = buf;
    tok.line = line;
    tok.col = col;
    return tok;
}

static Token lex_raw(Lexer *lex) {
    /* skip whitespace (not newlines) */
    while (cur(lex) == ' ' || cur(lex) == '\t' || cur(lex) == '\r') advance(lex);

    int line = lex->line, col = lex->col;

    if (cur(lex) == '\0') return make_tok(TK_EOF, "", line, col);

    /* line comments */
    if (cur(lex) == '/' && peek_ch(lex) == '/') {
        skip_line_comment(lex);
        return lex_raw(lex);
    }
    /* block comments */
    if (cur(lex) == '/' && peek_ch(lex) == '*') {
        skip_block_comment(lex);
        return lex_raw(lex);
    }

    /* newline */
    if (cur(lex) == '\n') {
        int depth = lex->paren_depth + lex->bracket_depth + lex->brace_depth;
        if (depth == 0 && can_end_stmt(lex->last_real)) {
            advance(lex);
            return make_tok(TK_NEWLINE, "\n", line, col);
        }
        advance(lex);
        return lex_raw(lex);
    }

    /* numbers */
    if (isdigit(cur(lex))) return lex_number(lex);

    /* identifiers / keywords */
    if (isalpha(cur(lex)) || cur(lex) == '_') return lex_ident_or_kw(lex);

    /* string literals (single or double quote, but double quote after fn context
       might be custom op — handled below) */
    if (cur(lex) == '\'') return lex_string(lex, '\'');
    /* double-quoted string or custom operator name:
       if the last real token was TK_FN or could start an operator declaration,
       treat as custom op; otherwise string literal */
    if (cur(lex) == '"') {
        if (lex->last_real == TK_FN) {
            return lex_custom_op(lex);
        }
        return lex_string(lex, '"');
    }

    char c = cur(lex);

    /* two-char operators */
    char n = peek_ch(lex);
    if (c == '<' && n == '<') { advance(lex); advance(lex); return make_tok(TK_LSHIFT, "<<", line, col); }
    if (c == '>' && n == '>') { advance(lex); advance(lex); return make_tok(TK_RSHIFT, ">>", line, col); }
    if (c == '<' && n == '=') { advance(lex); advance(lex); return make_tok(TK_LEQ, "<=", line, col); }
    if (c == '>' && n == '=') { advance(lex); advance(lex); return make_tok(TK_GEQ, ">=", line, col); }
    if (c == '=' && n == '=') { advance(lex); advance(lex); return make_tok(TK_EQEQ, "==", line, col); }
    if (c == '!' && n == '=') { advance(lex); advance(lex); return make_tok(TK_NEQ, "!=", line, col); }
    if (c == '&' && n == '&') { advance(lex); advance(lex); return make_tok(TK_ANDAND, "&&", line, col); }
    if (c == '|' && n == '|') { advance(lex); advance(lex); return make_tok(TK_OROR, "||", line, col); }
    if (c == ':' && n == ':') { advance(lex); advance(lex); return make_tok(TK_DCOLON, "::", line, col); }
    if (c == '-' && n == '>') { advance(lex); advance(lex); return make_tok(TK_ARROW, "->", line, col); }

    advance(lex);
    switch (c) {
        case '{': return make_tok_char(TK_LBRACE,   c, line, col);
        case '}': return make_tok_char(TK_RBRACE,   c, line, col);
        case '(': return make_tok_char(TK_LPAREN,   c, line, col);
        case ')': return make_tok_char(TK_RPAREN,   c, line, col);
        case '[': return make_tok_char(TK_LBRACKET, c, line, col);
        case ']': return make_tok_char(TK_RBRACKET, c, line, col);
        case '<': return make_tok_char(TK_LT,       c, line, col);
        case '>': return make_tok_char(TK_GT,       c, line, col);
        case ',': return make_tok_char(TK_COMMA,    c, line, col);
        case '.': return make_tok_char(TK_DOT,      c, line, col);
        case ':': return make_tok_char(TK_COLON,    c, line, col);
        case ';': return make_tok_char(TK_SEMI,     c, line, col);
        case '=': return make_tok_char(TK_EQ,       c, line, col);
        case '+': return make_tok_char(TK_PLUS,     c, line, col);
        case '-': return make_tok_char(TK_MINUS,    c, line, col);
        case '*': return make_tok_char(TK_STAR,     c, line, col);
        case '/': return make_tok_char(TK_SLASH,    c, line, col);
        case '%': return make_tok_char(TK_PERCENT,  c, line, col);
        case '&': return make_tok_char(TK_AMP,      c, line, col);
        case '|': return make_tok_char(TK_PIPE,     c, line, col);
        case '^': return make_tok_char(TK_CARET,    c, line, col);
        case '~': return make_tok_char(TK_TILDE,    c, line, col);
        case '!': return make_tok_char(TK_BANG,     c, line, col);
        case '?': return make_tok_char(TK_QUESTION, c, line, col);
        default: {
            char buf[2] = {c, 0};
            return make_tok(TK_ERROR, buf, line, col);
        }
    }
}

static void update_depth(Lexer *lex, Token *tok) {
    switch (tok->type) {
        case TK_LPAREN:   lex->paren_depth++;   break;
        case TK_RPAREN:   lex->paren_depth--;   if (lex->paren_depth < 0) lex->paren_depth = 0; break;
        case TK_LBRACKET: lex->bracket_depth++; break;
        case TK_RBRACKET: lex->bracket_depth--; if (lex->bracket_depth < 0) lex->bracket_depth = 0; break;
        case TK_LBRACE:   lex->brace_depth++;   break;
        case TK_RBRACE:   lex->brace_depth--;   if (lex->brace_depth < 0) lex->brace_depth = 0; break;
        default: break;
    }
    if (tok->type != TK_NEWLINE && tok->type != TK_SEMI) {
        lex->last_real = tok->type;
    }
}

Token lexer_next(Lexer *lex) {
    if (lex->has_peek) {
        lex->has_peek = 0;
        update_depth(lex, &lex->peek_buf);
        return lex->peek_buf;
    }
    Token tok = lex_raw(lex);
    update_depth(lex, &tok);
    return tok;
}

Token lexer_peek(Lexer *lex) {
    if (!lex->has_peek) {
        /* save depth state, lex one token, restore depth */
        int pd = lex->paren_depth, bd = lex->bracket_depth, brd = lex->brace_depth;
        TokenType lr = lex->last_real;
        lex->peek_buf = lex_raw(lex);
        /* don't update depth yet; restore */
        lex->paren_depth = pd;
        lex->bracket_depth = bd;
        lex->brace_depth = brd;
        lex->last_real = lr;
        lex->has_peek = 1;
    }
    return lex->peek_buf;
}

void token_free(Token *tok) {
    free(tok->value);
    tok->value = NULL;
}

const char *token_type_str(TokenType t) {
    switch (t) {
        case TK_FN: return "fn"; case TK_VAR: return "var"; case TK_PAT: return "pat";
        case TK_IMPORT: return "import"; case TK_PUB: return "pub";
        case TK_FOR: return "for"; case TK_WHILE: return "while";
        case TK_SWITCH: return "switch"; case TK_CASE: return "case";
        case TK_DEFAULT: return "default"; case TK_BREAK: return "break";
        case TK_YIELD: return "yield"; case TK_RETURN: return "return";
        case TK_COPY: return "copy"; case TK_MOVE: return "move";
        case TK_NULL: return "null"; case TK_AS: return "as"; case TK_OF: return "of";
        case TK_STATIC: return "static"; case TK_CONST: return "const";
        case TK_CONSTEXPR: return "constexpr";
        case TK_INT_LIT: return "<int>"; case TK_FLOAT_LIT: return "<float>";
        case TK_STR_LIT: return "<string>"; case TK_IDENT: return "<ident>";
        case TK_NEWLINE: return "<newline>"; case TK_SEMI: return ";";
        case TK_LBRACE: return "{"; case TK_RBRACE: return "}";
        case TK_LPAREN: return "("; case TK_RPAREN: return ")";
        case TK_LBRACKET: return "["; case TK_RBRACKET: return "]";
        case TK_LT: return "<"; case TK_GT: return ">";
        case TK_COMMA: return ","; case TK_DOT: return ".";
        case TK_COLON: return ":"; case TK_DCOLON: return "::";
        case TK_ARROW: return "->"; case TK_EQ: return "=";
        case TK_PLUS: return "+"; case TK_MINUS: return "-";
        case TK_STAR: return "*"; case TK_SLASH: return "/";
        case TK_PERCENT: return "%"; case TK_LEQ: return "<=";
        case TK_GEQ: return ">="; case TK_EQEQ: return "==";
        case TK_NEQ: return "!="; case TK_AMP: return "&";
        case TK_PIPE: return "|"; case TK_CARET: return "^";
        case TK_TILDE: return "~"; case TK_BANG: return "!";
        case TK_QUESTION: return "?"; case TK_LSHIFT: return "<<";
        case TK_RSHIFT: return ">>"; case TK_ANDAND: return "&&";
        case TK_OROR: return "||"; case TK_OP_CUSTOM: return "<custom_op>";
        case TK_EOF: return "<eof>"; case TK_ERROR: return "<error>";
        default: return "?";
    }
}
