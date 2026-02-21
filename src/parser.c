#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* forward declarations */
static AstNode *parse_stmt(Parser *p);
static AstNode *parse_fn_decl(Parser *p, int is_pub);
static AstNode *parse_var_decl(Parser *p, int is_pub);
static AstNode *parse_pat_decl(Parser *p, int is_pub);
static AstNode *parse_import_decl(Parser *p);
static AstNode *parse_scope(Parser *p);
static AstNode *parse_for(Parser *p);
static AstNode *parse_while(Parser *p);
static AstNode *parse_switch(Parser *p);
static AstNode *parse_expr_prec(Parser *p, int min_prec);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_postfix(Parser *p, AstNode *base);
static AstNode *parse_primary(Parser *p);
static AstNode *parse_template_args(Parser *p);
static AstNode *parse_template_decl(Parser *p);
static AstNode *parse_type_ann(Parser *p);

/* ------------------------------------------------------------------ helpers */

static void parser_error(Parser *p, const char *msg) {
    if (!p->had_error) {
        snprintf(p->error_msg, sizeof(p->error_msg),
                 "Error at line %d col %d: %s (got %s)",
                 p->cur.line, p->cur.col, msg,
                 token_type_str(p->cur.type));
        p->had_error = 1;
    }
}

static void advance(Parser *p) {
    token_free(&p->cur);
    p->cur = lexer_next(p->lex);
}

static void skip_terminators(Parser *p) {
    while (p->cur.type == TK_NEWLINE || p->cur.type == TK_SEMI) advance(p);
}

static int check(Parser *p, TokenType t) { return p->cur.type == t; }

static int match(Parser *p, TokenType t) {
    if (check(p, t)) { advance(p); return 1; }
    return 0;
}

static void expect(Parser *p, TokenType t) {
    if (!check(p, t)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected '%s'", token_type_str(t));
        parser_error(p, buf);
        return;
    }
    advance(p);
}

/* Consume any trailing attribute keywords (static, const, constexpr) and set
   the corresponding flags on node.  Called after a '::' has already been consumed. */
static void parse_attrs(Parser *p, AstNode *node) {
    while (check(p, TK_STATIC) || check(p, TK_CONST) || check(p, TK_CONSTEXPR)) {
        if (check(p, TK_STATIC))    { node->is_static    = 1; advance(p); }
        else if (check(p, TK_CONST))     { node->is_const     = 1; advance(p); }
        else if (check(p, TK_CONSTEXPR)) { node->is_constexpr = 1; advance(p); }
    }
}

/* ------------------------------------------------------------------ init */

void parser_init(Parser *p, Lexer *lex) {
    p->lex = lex;
    p->had_error = 0;
    p->error_msg[0] = '\0';
    p->cur.value = NULL;
    advance(p); /* prime first token */
}

/* ------------------------------------------------------------------ program */

AstNode *parse_program(Parser *p) {
    AstNode *prog = ast_new(AST_PROGRAM, 1, 1);
    skip_terminators(p);
    while (!check(p, TK_EOF) && !p->had_error) {
        AstNode *stmt = parse_stmt(p);
        if (stmt) ast_add_child(prog, stmt);
        /* consume terminators between statements */
        while (check(p, TK_NEWLINE) || check(p, TK_SEMI)) advance(p);
    }
    return prog;
}

/* ------------------------------------------------------------------ stmt */

static AstNode *parse_stmt(Parser *p) {
    int pub = 0;
    if (check(p, TK_PUB)) { advance(p); pub = 1; }

    switch (p->cur.type) {
        case TK_FN:     return parse_fn_decl(p, pub);
        case TK_VAR:    return parse_var_decl(p, pub);
        case TK_PAT:    return parse_pat_decl(p, pub);
        case TK_IMPORT: if (pub) parser_error(p, "import cannot be pub"); return parse_import_decl(p);
        case TK_FOR:    return parse_for(p);
        case TK_WHILE:  return parse_while(p);
        case TK_SWITCH: return parse_switch(p);
        case TK_BREAK: {
            int line = p->cur.line, col = p->cur.col;
            advance(p);
            return ast_new(AST_BREAK, line, col);
        }
        case TK_YIELD: {
            int line = p->cur.line, col = p->cur.col;
            advance(p);
            AstNode *n = ast_new(AST_YIELD, line, col);
            if (!check(p, TK_NEWLINE) && !check(p, TK_SEMI) && !check(p, TK_EOF) && !check(p, TK_RBRACE))
                n->init = parse_expr(p);
            return n;
        }
        case TK_RETURN: {
            int line = p->cur.line, col = p->cur.col;
            advance(p);
            AstNode *n = ast_new(AST_RETURN, line, col);
            if (!check(p, TK_NEWLINE) && !check(p, TK_SEMI) && !check(p, TK_EOF) && !check(p, TK_RBRACE))
                n->init = parse_expr(p);
            return n;
        }
        case TK_LBRACE: return parse_scope(p);
        default: {
            if (pub) { parser_error(p, "expected declaration after pub"); return NULL; }
            return parse_expr(p);
        }
    }
}

AstNode *parse_declaration(Parser *p) { return parse_stmt(p); }

/* ------------------------------------------------------------------ template decl */

static AstNode *parse_template_decl(Parser *p) {
    /* Syntax:  <Param[:<type>][:<num>][=default], …>
     *  or      <Param::[<num>][=default], …>   (type omitted, variadic)
     *
     * TK_DCOLON (::) seen directly after the name means the type is omitted
     * and the second ':' (variadic marker) follows immediately.
     */
    if (!check(p, TK_LT)) return NULL;
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume < */
    AstNode *tmpl = ast_new(AST_TEMPLATE_DECL, line, col);
    while (!check(p, TK_GT) && !check(p, TK_EOF)) {
        if (check(p, TK_IDENT)) {
            AstNode *param = ast_new(AST_PARAM, p->cur.line, p->cur.col);
            param->name = strdup(p->cur.value);
            advance(p);

            if (match(p, TK_DCOLON)) {
                /* T:: or T::num — variadic, type omitted */
                param->is_variadic = 1;
                if (check(p, TK_IDENT) || check(p, TK_INT_LIT)) {
                    /* optional variadic count */
                    advance(p);
                }
            } else if (match(p, TK_COLON)) {
                /* T:type or T:type:num */
                if (check(p, TK_IDENT) || check(p, TK_VAR)) {
                    /* type constraint — store in type_ann */
                    AstNode *ta = ast_new(AST_TYPE_ANN, p->cur.line, p->cur.col);
                    ta->data.str_val = strdup(p->cur.value);
                    param->type_ann = ta;
                    advance(p);
                }
                /* optional second ':' for variadic count */
                if (match(p, TK_COLON)) {
                    param->is_variadic = 1;
                    if (check(p, TK_IDENT) || check(p, TK_INT_LIT)) {
                        advance(p); /* optional count */
                    }
                }
            }

            if (match(p, TK_EQ)) {
                param->init = parse_expr(p);
            }
            ast_add_child(tmpl, param);
        }
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_GT);
    return tmpl;
}

/* ------------------------------------------------------------------ fn */

static AstNode *parse_fn_decl(Parser *p, int is_pub) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume fn */
    AstNode *fn = ast_new(AST_FN_DECL, line, col);
    fn->is_pub = is_pub;

    /* optional template */
    fn->tmpl = parse_template_decl(p);

    /* name: identifier or quoted custom operator */
    if (check(p, TK_IDENT)) {
        fn->name = strdup(p->cur.value);
        advance(p);
    } else if (check(p, TK_OP_CUSTOM)) {
        fn->name = strdup(p->cur.value);
        advance(p);
    } else {
        parser_error(p, "expected function name");
        return fn;
    }

    /* parameter list */
    expect(p, TK_LPAREN);
    while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
        AstNode *param = ast_new(AST_PARAM, p->cur.line, p->cur.col);
        /* copy/move qualifier */
        if (check(p, TK_COPY)) { param->is_const = 1; advance(p); }
        else if (check(p, TK_MOVE)) { param->is_static = 1; advance(p); }
        if (check(p, TK_IDENT)) {
            param->name = strdup(p->cur.value);
            advance(p);
        }
        /* param::attrs — type omitted, attributes present */
        if (match(p, TK_DCOLON)) {
            parse_attrs(p, param);
        } else if (match(p, TK_COLON)) {
            /* param:type or param:type::attrs */
            param->type_ann = parse_type_ann(p);
            if (match(p, TK_DCOLON)) {
                parse_attrs(p, param);
            }
        }
        /* optional default value: = expr */
        if (match(p, TK_EQ)) {
            param->init = parse_expr(p);
        }
        ast_add_child(fn, param);
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_RPAREN);

    /* optional return type annotation: :(name:type, ...) */
    if (match(p, TK_COLON)) {
        if (check(p, TK_LPAREN)) {
            /* return type tuple: :(name:type, ...) */
            int ret_line = p->cur.line, ret_col = p->cur.col;
            advance(p); /* consume ( */
            AstNode *ret_tuple = ast_new(AST_TUPLE, ret_line, ret_col);
            while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
                AstNode *ta = parse_type_ann(p);
                if (ta) ast_add_child(ret_tuple, ta);
                if (!match(p, TK_COMMA)) break;
            }
            expect(p, TK_RPAREN);
            fn->type_ann = ret_tuple;
        } else if (!check(p, TK_LBRACE) && !check(p, TK_NEWLINE) && !check(p, TK_SEMI)) {
            fn->type_ann = parse_type_ann(p);
        }
    }
    /* optional function-level attributes after ::
     * This may appear with OR without a preceding return-type annotation:
     *   fn foo() : (r:i32) :: constexpr { … }   — after return type
     *   fn foo() ::                      { … }   — no return type, bare ::
     */
    if (match(p, TK_DCOLON)) {
        parse_attrs(p, fn);
    }

    /* body */
    skip_terminators(p);
    if (check(p, TK_LBRACE)) fn->body = parse_scope(p);

    return fn;
}

/* ------------------------------------------------------------------ var */

static AstNode *parse_var_decl(Parser *p, int is_pub) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume var */
    AstNode *vd = ast_new(AST_VAR_DECL, line, col);
    vd->is_pub = is_pub;

    /* optional template */
    vd->tmpl = parse_template_decl(p);

    /* name */
    if (!check(p, TK_IDENT)) { parser_error(p, "expected variable name"); return vd; }
    vd->name = strdup(p->cur.value);
    advance(p);

    /* optional type annotation and/or attributes.
     *
     * Forms:
     *   name:type          — type only
     *   name:type::attrs   — type + attributes  (TK_COLON then type, then TK_DCOLON)
     *   name::attrs        — no type, attributes (TK_DCOLON directly)
     *
     * When type is omitted (the '::' case), the initializer '=' must be present
     * so the type can be inferred.
     */
    if (match(p, TK_DCOLON)) {
        /* name::attrs — type completely omitted */
        parse_attrs(p, vd);
        if (!check(p, TK_EQ)) {
            parser_error(p, "type omitted with '::' but no '=' initializer to infer type from");
        }
    } else if (match(p, TK_COLON)) {
        /* name:type or name:type::attrs */
        if (!check(p, TK_EQ) && !check(p, TK_NEWLINE) && !check(p, TK_SEMI)
                && !check(p, TK_EOF) && !check(p, TK_DCOLON)) {
            vd->type_ann = parse_type_ann(p);
        }
        if (match(p, TK_DCOLON)) {
            parse_attrs(p, vd);
        }
    }

    /* initializer */
    if (match(p, TK_EQ)) {
        vd->init = parse_expr(p);
    }

    return vd;
}

/* ------------------------------------------------------------------ pat */

static AstNode *parse_pat_decl(Parser *p, int is_pub) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume pat */
    AstNode *pd = ast_new(AST_PAT_DECL, line, col);
    pd->is_pub = is_pub;

    pd->tmpl = parse_template_decl(p);

    if (!check(p, TK_IDENT)) { parser_error(p, "expected pattern name"); return pd; }
    pd->name = strdup(p->cur.value);
    advance(p);

    /* optional base patterns and/or attributes.
     *
     * Forms:
     *   pat Name:Base        — single base
     *   pat Name:Base|Base2  — composed bases
     *   pat Name:Base::attrs — base + attributes
     *   pat Name::attrs      — no base, attributes only (TK_DCOLON directly)
     */
    if (match(p, TK_DCOLON)) {
        /* no base, attributes only */
        parse_attrs(p, pd);
    } else if (match(p, TK_COLON)) {
        /* consume base pat names separated by | */
        do {
            AstNode *base = ast_new(AST_IDENT, p->cur.line, p->cur.col);
            if (check(p, TK_IDENT)) { base->name = strdup(p->cur.value); advance(p); }
            ast_add_child(pd, base);
        } while (match(p, TK_PIPE));
        /* optional attributes after :: */
        if (match(p, TK_DCOLON)) {
            parse_attrs(p, pd);
        }
    }

    skip_terminators(p);
    if (check(p, TK_LBRACE)) pd->body = parse_scope(p);

    return pd;
}

/* ------------------------------------------------------------------ import */

static AstNode *parse_import_decl(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume import */
    AstNode *imp = ast_new(AST_IMPORT_DECL, line, col);

    /* module path: ident[.ident]* */
    if (!check(p, TK_IDENT)) { parser_error(p, "expected module name"); return imp; }
    size_t cap = 128, len = 0;
    char *path = malloc(cap);
    path[0] = '\0';
    while (check(p, TK_IDENT)) {
        size_t n = strlen(p->cur.value);
        if (len + n + 2 >= cap) { cap *= 2; path = realloc(path, cap); }
        if (len > 0) { path[len++] = '.'; }
        memcpy(path + len, p->cur.value, n);
        len += n;
        path[len] = '\0';
        advance(p);
        if (!match(p, TK_DOT)) break;
    }
    imp->name = path;

    /* optional 'as' alias */
    if (match(p, TK_AS)) {
        if (check(p, TK_IDENT)) { imp->op = strdup(p->cur.value); advance(p); }
    }

    /* optional 'of' items */
    if (match(p, TK_OF)) {
        int has_brace = match(p, TK_LBRACE);
        do {
            AstNode *item = ast_new(AST_IMPORT_ITEM, p->cur.line, p->cur.col);
            if (check(p, TK_IDENT)) { item->name = strdup(p->cur.value); advance(p); }
            if (match(p, TK_AS)) {
                if (check(p, TK_IDENT)) { item->op = strdup(p->cur.value); advance(p); }
            }
            ast_add_child(imp, item);
            if (!match(p, TK_COMMA)) break;
        } while (!check(p, TK_RBRACE) && !check(p, TK_EOF));
        if (has_brace) expect(p, TK_RBRACE);
    }

    return imp;
}

/* ------------------------------------------------------------------ type annotation */

static AstNode *parse_type_ann(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    AstNode *ta = ast_new(AST_TYPE_ANN, line, col);

    /* named return value: name:type */
    if (check(p, TK_IDENT) && lexer_peek(p->lex).type == TK_COLON) {
        ta->name = strdup(p->cur.value);
        advance(p);
        advance(p); /* consume : */
    }

    /* type name, possibly with template args */
    if (check(p, TK_IDENT)) {
        ta->data.str_val = strdup(p->cur.value);
        advance(p);
        /* template instantiation */
        if (check(p, TK_LT)) {
            ta->init = parse_template_args(p);
        }
    } else if (check(p, TK_NULL)) {
        ta->data.str_val = strdup("null");
        advance(p);
    }

    return ta;
}

/* ------------------------------------------------------------------ scope */

static AstNode *parse_scope(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    expect(p, TK_LBRACE);
    AstNode *sc = ast_new(AST_SCOPE, line, col);
    skip_terminators(p);
    while (!check(p, TK_RBRACE) && !check(p, TK_EOF) && !p->had_error) {
        AstNode *stmt = parse_stmt(p);
        if (stmt) ast_add_child(sc, stmt);
        while (check(p, TK_NEWLINE) || check(p, TK_SEMI)) advance(p);
    }
    expect(p, TK_RBRACE);
    return sc;
}

/* ------------------------------------------------------------------ for */

static AstNode *parse_for(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume for */
    AstNode *fn = ast_new(AST_FOR, line, col);
    expect(p, TK_LPAREN);
    /* val : range */
    AstNode *var = ast_new(AST_IDENT, p->cur.line, p->cur.col);
    if (check(p, TK_IDENT)) { var->name = strdup(p->cur.value); advance(p); }
    fn->init = var;
    expect(p, TK_COLON);
    fn->cond = parse_expr(p);
    expect(p, TK_RPAREN);
    /* optional type/attrs */
    if (match(p, TK_COLON)) {
        while (check(p, TK_COLON) || check(p, TK_IDENT)) advance(p);
    }
    skip_terminators(p);
    fn->body = parse_scope(p);
    return fn;
}

/* ------------------------------------------------------------------ while */

static AstNode *parse_while(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    AstNode *wn = ast_new(AST_WHILE, line, col);

    /* optional leading while(cond) */
    if (check(p, TK_WHILE)) {
        advance(p);
        expect(p, TK_LPAREN);
        wn->cond = parse_expr(p);
        expect(p, TK_RPAREN);
    }

    skip_terminators(p);
    wn->body = parse_scope(p);

    /* optional trailing while(cond) */
    if (check(p, TK_WHILE)) {
        advance(p);
        expect(p, TK_LPAREN);
        wn->alt = parse_expr(p);
        expect(p, TK_RPAREN);
    }

    return wn;
}

/* ------------------------------------------------------------------ switch */

static AstNode *parse_switch(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume switch */
    AstNode *sw = ast_new(AST_SWITCH, line, col);
    expect(p, TK_LPAREN);
    sw->cond = parse_expr(p);
    expect(p, TK_RPAREN);
    /* optional type/attrs */
    if (match(p, TK_COLON)) {
        while (!check(p, TK_LBRACE) && !check(p, TK_EOF)) advance(p);
    }
    expect(p, TK_LBRACE);
    skip_terminators(p);
    while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        if (check(p, TK_CASE)) {
            int cl = p->cur.line, cc = p->cur.col;
            advance(p);
            AstNode *cas = ast_new(AST_CASE, cl, cc);
            cas->cond = parse_expr(p);
            expect(p, TK_COLON);
            int has_brace = 0;
            if (check(p, TK_LBRACE)) { has_brace = 1; advance(p); }
            skip_terminators(p);
            while (!check(p, TK_BREAK) && !check(p, TK_CASE) && !check(p, TK_DEFAULT)
                   && !check(p, TK_RBRACE) && !check(p, TK_EOF)) {
                AstNode *s = parse_stmt(p);
                if (s) ast_add_child(cas, s);
                while (check(p, TK_NEWLINE) || check(p, TK_SEMI)) advance(p);
            }
            if (has_brace && check(p, TK_RBRACE)) advance(p);
            if (check(p, TK_BREAK)) advance(p);
            ast_add_child(sw, cas);
        } else if (check(p, TK_DEFAULT)) {
            int cl = p->cur.line, cc = p->cur.col;
            advance(p);
            expect(p, TK_COLON);
            AstNode *cas = ast_new(AST_CASE, cl, cc);
            cas->cond = NULL; /* default */
            int has_brace = 0;
            if (check(p, TK_LBRACE)) { has_brace = 1; advance(p); }
            skip_terminators(p);
            while (!check(p, TK_BREAK) && !check(p, TK_RBRACE) && !check(p, TK_EOF)) {
                AstNode *s = parse_stmt(p);
                if (s) ast_add_child(cas, s);
                while (check(p, TK_NEWLINE) || check(p, TK_SEMI)) advance(p);
            }
            if (has_brace && check(p, TK_RBRACE)) advance(p);
            if (check(p, TK_BREAK)) advance(p);
            ast_add_child(sw, cas);
        } else {
            break;
        }
        while (check(p, TK_NEWLINE) || check(p, TK_SEMI)) advance(p);
    }
    expect(p, TK_RBRACE);
    return sw;
}

/* ------------------------------------------------------------------ template args (instantiation) */

static AstNode *parse_template_args(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    advance(p); /* consume < */
    AstNode *ta = ast_new(AST_TEMPLATE_INST, line, col);
    while (!check(p, TK_GT) && !check(p, TK_EOF)) {
        ast_add_child(ta, parse_expr(p));
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_GT);
    return ta;
}

/* ------------------------------------------------------------------ expressions */

/* Operator precedence (higher = tighter binding) */
static int binop_prec(TokenType t) {
    switch (t) {
        case TK_OROR:    return 1;
        case TK_ANDAND:  return 2;
        case TK_PIPE:    return 3;
        case TK_CARET:   return 4;
        case TK_AMP:     return 5;
        case TK_EQEQ: case TK_NEQ:              return 6;
        case TK_LT: case TK_GT:
        case TK_LEQ: case TK_GEQ:               return 7;
        case TK_LSHIFT: case TK_RSHIFT:          return 8;
        case TK_PLUS: case TK_MINUS:             return 9;
        case TK_STAR: case TK_SLASH: case TK_PERCENT: return 10;
        default: return -1;
    }
}

static const char *tok_op_str(TokenType t) {
    switch (t) {
        case TK_PLUS: return "+"; case TK_MINUS: return "-";
        case TK_STAR: return "*"; case TK_SLASH: return "/";
        case TK_PERCENT: return "%";
        case TK_LT: return "<"; case TK_GT: return ">";
        case TK_LEQ: return "<="; case TK_GEQ: return ">=";
        case TK_EQEQ: return "=="; case TK_NEQ: return "!=";
        case TK_AMP: return "&"; case TK_PIPE: return "|";
        case TK_CARET: return "^"; case TK_LSHIFT: return "<<";
        case TK_RSHIFT: return ">>"; case TK_ANDAND: return "&&";
        case TK_OROR: return "||";
        default: return "?";
    }
}

AstNode *parse_expr(Parser *p) {
    return parse_expr_prec(p, 0);
}

static AstNode *parse_expr_prec(Parser *p, int min_prec) {
    AstNode *left = parse_unary(p);
    if (!left) return NULL;

    /* assignment (lowest precedence, right-associative) */
    if (min_prec == 0 && check(p, TK_EQ)) {
        int line = p->cur.line, col = p->cur.col;
        advance(p);
        AstNode *right = parse_expr(p);
        AstNode *assign = ast_new(AST_ASSIGN, line, col);
        assign->init = left;
        assign->body = right;
        left = assign;
    }

    for (;;) {
        int prec = binop_prec(p->cur.type);
        if (prec < min_prec + 1) break;
        const char *op = tok_op_str(p->cur.type);
        int line = p->cur.line, col = p->cur.col;
        advance(p);
        AstNode *right = parse_expr_prec(p, prec);
        AstNode *bin = ast_new(AST_BINOP, line, col);
        bin->op = strdup(op);
        ast_add_child(bin, left);
        ast_add_child(bin, right);
        left = bin;
    }

    /* ternary ?: — parsed after all binary ops so `a < b ? c : d` = `(a<b) ? c : d` */
    if (min_prec == 0 && check(p, TK_QUESTION)) {
        int line = p->cur.line, col = p->cur.col;
        advance(p);
        AstNode *opt = ast_new(AST_OPTIONAL, line, col);
        opt->cond = left;
        opt->init = parse_expr(p);
        if (match(p, TK_COLON)) opt->alt = parse_expr(p);
        return opt;
    }

    return left;
}

static AstNode *parse_unary(Parser *p) {
    if (check(p, TK_MINUS) || check(p, TK_BANG) || check(p, TK_TILDE)) {
        int line = p->cur.line, col = p->cur.col;
        const char *op = tok_op_str(p->cur.type);
        if (p->cur.type == TK_BANG) op = "!";
        if (p->cur.type == TK_TILDE) op = "~";
        advance(p);
        AstNode *n = ast_new(AST_UNOP, line, col);
        n->op = strdup(op);
        n->init = parse_unary(p);
        return n;
    }
    if (check(p, TK_COPY)) {
        int line = p->cur.line, col = p->cur.col;
        advance(p);
        AstNode *n = ast_new(AST_COPY, line, col);
        n->init = parse_unary(p);
        return n;
    }
    if (check(p, TK_MOVE)) {
        int line = p->cur.line, col = p->cur.col;
        advance(p);
        AstNode *n = ast_new(AST_MOVE, line, col);
        n->init = parse_unary(p);
        return n;
    }

    AstNode *base = parse_primary(p);
    return parse_postfix(p, base);
}

static AstNode *parse_primary(Parser *p) {
    int line = p->cur.line, col = p->cur.col;

    if (check(p, TK_INT_LIT)) {
        AstNode *n = ast_new(AST_INT_LIT, line, col);
        n->data.int_val = strtoll(p->cur.value, NULL, 10);
        advance(p);
        return n;
    }
    if (check(p, TK_FLOAT_LIT)) {
        AstNode *n = ast_new(AST_FLOAT_LIT, line, col);
        n->data.float_val = strtod(p->cur.value, NULL);
        advance(p);
        return n;
    }
    if (check(p, TK_STR_LIT)) {
        AstNode *n = ast_new(AST_STR_LIT, line, col);
        n->data.str_val = strdup(p->cur.value);
        advance(p);
        return n;
    }
    if (check(p, TK_NULL)) {
        advance(p);
        return ast_new(AST_NULL_LIT, line, col);
    }
    if (check(p, TK_IDENT)) {
        AstNode *n = ast_new(AST_IDENT, line, col);
        n->name = strdup(p->cur.value);
        advance(p);
        return n;
    }
    if (check(p, TK_LPAREN)) {
        advance(p);
        /* could be a tuple literal: (name: expr, ...) or (expr, ...) */
        AstNode *expr = parse_expr(p);
        /* named element: (name: value, ...) — ident followed by ':' */
        if (check(p, TK_COLON) && expr && expr->type == AST_IDENT) {
            AstNode *tuple = ast_new(AST_TUPLE, line, col);
            for (;;) {
                /* consume ':' and parse value */
                advance(p); /* consume ':' */
                AstNode *elem = ast_new(AST_PARAM, expr->line, expr->col);
                elem->name = expr->name; /* transfer name ownership */
                expr->name = NULL;       /* prevent double-free in ast_free */
                ast_free(expr);
                elem->init = parse_expr(p);
                ast_add_child(tuple, elem);
                if (!match(p, TK_COMMA)) break;
                if (check(p, TK_RPAREN)) break; /* trailing comma */
                expr = parse_expr(p);
                /* subsequent element may or may not be named */
                if (!check(p, TK_COLON) || !expr || expr->type != AST_IDENT) {
                    ast_add_child(tuple, expr);
                    while (match(p, TK_COMMA)) {
                        if (check(p, TK_RPAREN)) break;
                        ast_add_child(tuple, parse_expr(p));
                    }
                    break;
                }
            }
            expect(p, TK_RPAREN);
            return tuple;
        }
        if (check(p, TK_COMMA) || (expr && expr->type == AST_ASSIGN)) {
            /* Unnamed tuple: (a, b, ...) */
            AstNode *tuple = ast_new(AST_TUPLE, line, col);
            ast_add_child(tuple, expr);
            while (match(p, TK_COMMA)) {
                if (check(p, TK_RPAREN)) break; /* trailing comma */
                ast_add_child(tuple, parse_expr(p));
            }
            expect(p, TK_RPAREN);
            return tuple;
        }
        /* named single: (name: expr) is also a 1-tuple */
        if (expr && expr->type == AST_TYPE_ANN) {
            /* treat as 1-elem tuple */
            AstNode *tuple = ast_new(AST_TUPLE, line, col);
            ast_add_child(tuple, expr);
            expect(p, TK_RPAREN);
            return tuple;
        }
        expect(p, TK_RPAREN);
        return expr;
    }
    if (check(p, TK_LBRACE)) {
        return parse_scope(p);
    }
    /* template instantiation: <Type>(...) */
    if (check(p, TK_LT)) {
        AstNode *ti = ast_new(AST_TEMPLATE_INST, line, col);
        advance(p); /* consume < */
        while (!check(p, TK_GT) && !check(p, TK_EOF)) {
            ast_add_child(ti, parse_type_ann(p));
            if (!match(p, TK_COMMA)) break;
        }
        expect(p, TK_GT);
        return parse_postfix(p, ti);
    }

    /* Unexpected token */
    if (!check(p, TK_EOF) && !check(p, TK_RBRACE) && !check(p, TK_RPAREN)
        && !check(p, TK_RBRACKET) && !check(p, TK_SEMI) && !check(p, TK_NEWLINE)) {
        parser_error(p, "unexpected token in expression");
        advance(p);
    }
    return NULL;
}

static AstNode *parse_postfix(Parser *p, AstNode *base) {
    if (!base) return NULL;
    for (;;) {
        if (check(p, TK_DOT)) {
            int line = p->cur.line, col = p->cur.col;
            advance(p);
            /* allow newline before member name */
            skip_terminators(p);
            AstNode *mem = ast_new(AST_MEMBER, line, col);
            mem->init = base;
            if (check(p, TK_IDENT)) {
                mem->name = strdup(p->cur.value);
                advance(p);
            }
            base = mem;
        } else if (check(p, TK_LPAREN)) {
            int line = p->cur.line, col = p->cur.col;
            advance(p); /* consume ( */
            /* allow newline before args */
            skip_terminators(p);
            AstNode *call = ast_new(AST_CALL, line, col);
            call->init = base;
            while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
                ast_add_child(call, parse_expr(p));
                if (!match(p, TK_COMMA)) break;
            }
            expect(p, TK_RPAREN);
            base = call;
        } else if (check(p, TK_LBRACKET)) {
            int line = p->cur.line, col = p->cur.col;
            advance(p);
            AstNode *idx = ast_new(AST_INDEX, line, col);
            idx->init = base;
            ast_add_child(idx, parse_expr(p));
            expect(p, TK_RBRACKET);
            base = idx;
        } else if (check(p, TK_LT)) {
            /* could be template instantiation: ident<Type>(...) */
            /* heuristic: only if next token after > is ( */
            /* We use lexer state save/restore trick via peek — simplified: always try */
            int line = p->cur.line, col = p->cur.col;
            /* save lexer state for backtracking */
            size_t saved_pos = p->lex->pos;
            int saved_line = p->lex->line;
            int saved_col = p->lex->col;
            int saved_pd = p->lex->paren_depth;
            int saved_bd = p->lex->bracket_depth;
            int saved_brd = p->lex->brace_depth;
            TokenType saved_lr = p->lex->last_real;
            int saved_hp = p->lex->has_peek;
            Token saved_peek = p->lex->peek_buf;
            if (saved_hp && saved_peek.value) saved_peek.value = strdup(saved_peek.value);
            Token saved_cur;
            saved_cur.type = p->cur.type;
            saved_cur.value = p->cur.value ? strdup(p->cur.value) : NULL;
            saved_cur.line = p->cur.line;
            saved_cur.col = p->cur.col;

            advance(p); /* consume < */
            AstNode *ti = ast_new(AST_TEMPLATE_INST, line, col);
            int ok = 1;
            while (!check(p, TK_GT) && !check(p, TK_EOF)) {
                AstNode *arg = parse_type_ann(p);
                if (!arg || p->had_error) { ok = 0; break; }
                ast_add_child(ti, arg);
                if (!match(p, TK_COMMA)) break;
            }
            if (ok && check(p, TK_GT)) {
                advance(p); /* consume > */
                ti->init = base;
                base = ti;
            } else {
                /* backtrack */
                ast_free(ti);
                p->had_error = 0;
                p->lex->pos = saved_pos;
                p->lex->line = saved_line;
                p->lex->col = saved_col;
                p->lex->paren_depth = saved_pd;
                p->lex->bracket_depth = saved_bd;
                p->lex->brace_depth = saved_brd;
                p->lex->last_real = saved_lr;
                /* Free current peek buffer before restoring saved state */
                if (p->lex->has_peek) {
                    token_free(&p->lex->peek_buf);
                }
                p->lex->has_peek = saved_hp;
                if (saved_hp) {
                    p->lex->peek_buf = saved_peek;
                }
                /* if !saved_hp, saved_peek.value is uninitialized garbage — do not free */
                token_free(&p->cur);
                p->cur = saved_cur;
                break;
            }
            free(saved_cur.value);
            /* Only free saved_peek.value if it was strdup'd (i.e., was_peek was set) */
            if (saved_hp) free(saved_peek.value);
        } else {
            break;
        }
    }
    return base;
}
