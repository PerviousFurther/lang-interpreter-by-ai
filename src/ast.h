#ifndef AST_H
#define AST_H

#include <stddef.h>

typedef enum {
    /* Declarations */
    AST_PROGRAM,
    AST_FN_DECL,
    AST_VAR_DECL,
    AST_PAT_DECL,
    AST_IMPORT_DECL,
    AST_IMPORT_ITEM,

    /* Expressions */
    AST_IDENT,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_STR_LIT,
    AST_NULL_LIT,
    AST_BINOP,
    AST_UNOP,
    AST_CALL,
    AST_MEMBER,
    AST_INDEX,
    AST_TUPLE,
    AST_SCOPE,
    AST_TEMPLATE_INST,

    /* Control flow */
    AST_FOR,
    AST_WHILE,
    AST_SWITCH,
    AST_CASE,
    AST_BREAK,
    AST_YIELD,
    AST_RETURN,
    AST_OPTIONAL,  /* ?: */

    /* Other */
    AST_COPY,
    AST_MOVE,
    AST_ASSIGN,
    AST_MULTI_ASSIGN,
    AST_TEMPLATE_DECL,
    AST_PARAM,
    AST_TYPE_ANN,
    AST_BLOCK,
} AstNodeType;

typedef struct AstNode AstNode;

struct AstNode {
    AstNodeType type;
    int line, col;

    /* Generic children array */
    AstNode **children;
    int child_count;
    int child_cap;

    /* Node-specific data */
    union {
        long long  int_val;
        double     float_val;
        char      *str_val;   /* ident name, operator string, string literal, etc. */
    } data;

    /* Extra flags/fields */
    int is_pub;        /* for pub declarations */
    int is_static;
    int is_const;
    int is_constexpr;
    int is_variadic;   /* for template parameters: Param:: or Param:type: */
    char *name;        /* declaration name */
    char *op;          /* operator string for BINOP/UNOP */
    AstNode *type_ann; /* type annotation */
    AstNode *init;     /* initializer expression */
    AstNode *body;     /* function / loop body */
    AstNode *cond;     /* condition */
    AstNode *alt;      /* else-branch of optional */
    AstNode *tmpl;     /* template parameter list */
};

AstNode *ast_new(AstNodeType type, int line, int col);
void ast_add_child(AstNode *parent, AstNode *child);
void ast_free(AstNode *node);

#endif /* AST_H */
