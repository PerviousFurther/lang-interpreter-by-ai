#include "ast.h"
#include <stdlib.h>
#include <string.h>

AstNode *ast_new(AstNodeType type, int line, int col) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = type;
    n->line = line;
    n->col  = col;
    return n;
}

void ast_add_child(AstNode *parent, AstNode *child) {
    if (!child) return;
    if (parent->child_count >= parent->child_cap) {
        parent->child_cap = parent->child_cap ? parent->child_cap * 2 : 4;
        parent->children = realloc(parent->children,
                                   sizeof(AstNode *) * (size_t)parent->child_cap);
    }
    parent->children[parent->child_count++] = child;
}

void ast_free(AstNode *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) ast_free(node->children[i]);
    free(node->children);
    /* Only free data.str_val for node types that store a heap string there */
    switch (node->type) {
        case AST_STR_LIT:
        case AST_TYPE_ANN:
            free(node->data.str_val);
            break;
        default:
            break;
    }
    free(node->name);
    free(node->op);
    ast_free(node->type_ann);
    ast_free(node->init);
    ast_free(node->body);
    ast_free(node->cond);
    ast_free(node->alt);
    ast_free(node->tmpl);
    free(node);
}
