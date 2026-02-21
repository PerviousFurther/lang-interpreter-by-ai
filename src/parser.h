#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer *lex;
    Token  cur;
    int    had_error;
    char   error_msg[256];
} Parser;

void    parser_init(Parser *p, Lexer *lex);
AstNode *parse_program(Parser *p);

/* Exposed for module loader */
AstNode *parse_declaration(Parser *p);
AstNode *parse_expr(Parser *p);

#endif /* PARSER_H */
