#ifndef VALUE_H
#define VALUE_H

#include <stddef.h>

/* Forward declarations */
typedef struct AstNode AstNode;
typedef struct Env Env;
typedef struct Value Value;
typedef struct PatDef PatDef;

typedef enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_TUPLE,
    VAL_VARIANT,
    VAL_FUNCTION,
    VAL_PAT_INST,
    VAL_SCOPE,
    VAL_BUILTIN_FN,
    VAL_OPTIONAL,
    VAL_TYPE,
    VAL_MODULE,
} ValueType;

/* Pattern definition (like a struct descriptor) */
struct PatDef {
    char  *name;
    char **field_names;
    int    field_count;
    Env   *methods;   /* method environment */
    int    ref_count;
};

typedef Value *(*BuiltinFn)(Value **args, int argc);

struct Value {
    ValueType type;
    int ref_count;
    union {
        long long  int_val;
        double     float_val;
        char      *str_val;
        int        bool_val;
        struct {
            Value **elems;
            int     count;
            char  **names;   /* may be NULL for unnamed tuples */
        } tuple;
        struct {
            int    tag;
            Value *val;
        } variant;
        struct {
            AstNode *ast;
            Env     *closure;
            char    *name;
        } fn;
        struct {
            Value   **fields;
            int       count;
            PatDef   *def;
        } pat_inst;
        struct {
            Env *env;
        } scope;
        struct {
            BuiltinFn fn;
            char     *name;
        } builtin;
        struct {
            Value *val;    /* the actual value when present */
            int    present;
        } optional;
        struct {
            char *type_name;
        } type_val;
        struct {
            Env  *env;
            char *name;
            PatDef *patdef;  /* non-null if this module is a pattern constructor */
        } module;
    };
};

/* Lifecycle */
Value *value_new_null(void);
Value *value_new_int(long long v);
Value *value_new_float(double v);
Value *value_new_string(const char *s);
Value *value_new_bool(int v);
Value *value_new_tuple(int count);
Value *value_new_function(AstNode *ast, Env *closure, const char *name);
Value *value_new_builtin(BuiltinFn fn, const char *name);
Value *value_new_pat_inst(PatDef *def, int field_count);
Value *value_new_scope(Env *env);
Value *value_new_module(const char *name, Env *env);
Value *value_new_type(const char *type_name);
Value *value_new_optional(Value *val, int present);

void   value_incref(Value *v);
void   value_decref(Value *v);
Value *value_copy(Value *v);

/* Conversion / printing */
char  *value_to_string(Value *v);
int    value_is_truthy(Value *v);
int    value_equals(Value *a, Value *b);

/* PatDef */
PatDef *patdef_new(const char *name, int field_count);
void    patdef_incref(PatDef *p);
void    patdef_decref(PatDef *p);

#endif /* VALUE_H */
