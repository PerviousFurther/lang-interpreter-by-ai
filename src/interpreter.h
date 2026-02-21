#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "ast.h"
#include "value.h"

/* Symbol table entry */
typedef struct EnvEntry {
    char  *name;
    Value *val;
    struct EnvEntry *next;
} EnvEntry;

/* Environment (linked list of scopes) */
struct Env {
    EnvEntry *entries;
    struct Env *parent;
    int       ref_count;
};

Env   *env_new(Env *parent);
void   env_incref(Env *e);
void   env_decref(Env *e);
Value *env_get(Env *e, const char *name);
void   env_set(Env *e, const char *name, Value *val);   /* sets in nearest scope that has it, or current */
void   env_def(Env *e, const char *name, Value *val);   /* defines in current scope */

/* Control flow signals */
typedef enum {
    SIG_NONE,
    SIG_RETURN,
    SIG_BREAK,
    SIG_YIELD,
    SIG_ERROR,
} Signal;

typedef struct {
    Signal  sig;
    Value  *val;
    char    error_msg[256];
} EvalResult;

EvalResult eval(AstNode *node, Env *env);
EvalResult eval_block(AstNode *block, Env *env);

/* Top-level interpreter */
typedef struct {
    Env *global;
    char error_msg[256];
    int  had_error;
} Interpreter;

void interp_init(Interpreter *interp);
void interp_run(Interpreter *interp, AstNode *program);
void interp_free(Interpreter *interp);

#endif /* INTERPRETER_H */
