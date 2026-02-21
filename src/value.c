#define _POSIX_C_SOURCE 200809L
#include "value.h"
#include "interpreter.h"  /* for Env definition */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ PatDef */

PatDef *patdef_new(const char *name, int field_count) {
    PatDef *p = calloc(1, sizeof(PatDef));
    p->name = strdup(name);
    p->field_count = field_count;
    p->field_names = calloc((size_t)field_count, sizeof(char *));
    p->ref_count = 1;
    return p;
}

void patdef_incref(PatDef *p) { if (p) p->ref_count++; }

void patdef_decref(PatDef *p) {
    if (!p) return;
    p->ref_count--;
    if (p->ref_count <= 0) {
        free(p->name);
        for (int i = 0; i < p->field_count; i++) free(p->field_names[i]);
        free(p->field_names);
        if (p->methods) env_decref(p->methods);
        free(p);
    }
}

/* ------------------------------------------------------------------ Value allocation */

static Value *value_alloc(ValueType t) {
    Value *v = calloc(1, sizeof(Value));
    v->type = t;
    v->ref_count = 1;
    return v;
}

Value *value_new_null(void)           { return value_alloc(VAL_NULL); }
Value *value_new_int(long long i)     { Value *v = value_alloc(VAL_INT);   v->int_val = i;   return v; }
Value *value_new_float(double d)      { Value *v = value_alloc(VAL_FLOAT); v->float_val = d; return v; }
Value *value_new_bool(int b)          { Value *v = value_alloc(VAL_BOOL);  v->bool_val = !!b; return v; }

Value *value_new_string(const char *s) {
    Value *v = value_alloc(VAL_STRING);
    v->str_val = strdup(s ? s : "");
    return v;
}

Value *value_new_tuple(int count) {
    Value *v = value_alloc(VAL_TUPLE);
    v->tuple.count = count;
    v->tuple.elems = calloc((size_t)count, sizeof(Value *));
    v->tuple.names = NULL;
    return v;
}

Value *value_new_function(AstNode *ast, Env *closure, const char *name) {
    Value *v = value_alloc(VAL_FUNCTION);
    v->fn.ast     = ast;
    v->fn.closure = closure;
    v->fn.name    = name ? strdup(name) : NULL;
    return v;
}

Value *value_new_builtin(BuiltinFn fn, const char *name) {
    Value *v = value_alloc(VAL_BUILTIN_FN);
    v->builtin.fn   = fn;
    v->builtin.name = strdup(name);
    return v;
}

Value *value_new_pat_inst(PatDef *def, int field_count) {
    Value *v = value_alloc(VAL_PAT_INST);
    v->pat_inst.def    = def;
    v->pat_inst.count  = field_count;
    v->pat_inst.fields = calloc((size_t)field_count, sizeof(Value *));
    patdef_incref(def);
    return v;
}

Value *value_new_scope(Env *env) {
    Value *v = value_alloc(VAL_SCOPE);
    v->scope.env = env;
    return v;
}

Value *value_new_module(const char *name, Env *env) {
    Value *v = value_alloc(VAL_MODULE);
    v->module.name = strdup(name);
    v->module.env  = env;
    if (env) env_incref(env);
    return v;
}

Value *value_new_type(const char *type_name) {
    Value *v = value_alloc(VAL_TYPE);
    v->type_val.type_name = strdup(type_name);
    v->type_val.patdef    = NULL;
    return v;
}

Value *value_new_pat_type(const char *type_name, PatDef *def) {
    Value *v = value_alloc(VAL_TYPE);
    v->type_val.type_name = strdup(type_name);
    v->type_val.patdef    = def;
    if (def) patdef_incref(def);
    return v;
}

/* Return a VAL_TYPE that reflects the runtime type of v. */
Value *value_type_of(Value *v) {
    if (!v) return value_new_type("null");
    switch (v->type) {
        case VAL_NULL:       return value_new_type("null");
        case VAL_INT:        return value_new_type("i64");
        case VAL_FLOAT:      return value_new_type("f64");
        case VAL_STRING:     return value_new_type("string");
        case VAL_BOOL:       return value_new_type("bool");
        case VAL_TUPLE:      return value_new_type("tuple");
        case VAL_VARIANT:    return value_new_type("variant");
        case VAL_SCOPE:      return value_new_type("scope");
        case VAL_OPTIONAL:   return value_new_type("optional");
        case VAL_TYPE:       return value_new_type("type");
        case VAL_BUILTIN_FN: return value_new_type("function");
        case VAL_FUNCTION: {
            const char *n = v->fn.name ? v->fn.name : "function";
            return value_new_type(n);
        }
        case VAL_PAT_INST: {
            const char *n = (v->pat_inst.def && v->pat_inst.def->name)
                            ? v->pat_inst.def->name : "pat";
            return value_new_pat_type(n, v->pat_inst.def);
        }
        case VAL_MODULE: {
            const char *n = v->module.name ? v->module.name : "module";
            return value_new_type(n);
        }
        default: return value_new_type("unknown");
    }
}

Value *value_new_optional(Value *val, int present) {
    Value *v = value_alloc(VAL_OPTIONAL);
    v->optional.val     = val;
    v->optional.present = present;
    if (val) value_incref(val);
    return v;
}

/* ------------------------------------------------------------------ Ref counting */

void value_incref(Value *v) { if (v) v->ref_count++; }

void value_decref(Value *v) {
    if (!v) return;
    v->ref_count--;
    if (v->ref_count > 0) return;

    switch (v->type) {
        case VAL_STRING:
            free(v->str_val);
            break;
        case VAL_TUPLE:
            for (int i = 0; i < v->tuple.count; i++) value_decref(v->tuple.elems[i]);
            free(v->tuple.elems);
            if (v->tuple.names) {
                for (int i = 0; i < v->tuple.count; i++) free(v->tuple.names[i]);
                free(v->tuple.names);
            }
            break;
        case VAL_VARIANT:
            value_decref(v->variant.val);
            break;
        case VAL_FUNCTION:
            free(v->fn.name);
            /* ast and closure are borrowed */
            break;
        case VAL_PAT_INST:
            for (int i = 0; i < v->pat_inst.count; i++) value_decref(v->pat_inst.fields[i]);
            free(v->pat_inst.fields);
            patdef_decref(v->pat_inst.def);
            break;
        case VAL_BUILTIN_FN:
            free(v->builtin.name);
            break;
        case VAL_OPTIONAL:
            value_decref(v->optional.val);
            break;
        case VAL_TYPE:
            free(v->type_val.type_name);
            patdef_decref(v->type_val.patdef); /* patdef_decref handles NULL safely */
            break;
        case VAL_MODULE:
            free(v->module.name);
            env_decref(v->module.env);
            patdef_decref(v->module.patdef);
            break;
        case VAL_SCOPE:
            /* env is owned by interpreter stack */
            break;
        default: break;
    }
    free(v);
}

/* Deep copy */
Value *value_copy(Value *v) {
    if (!v) return value_new_null();
    switch (v->type) {
        case VAL_NULL:    return value_new_null();
        case VAL_INT:     return value_new_int(v->int_val);
        case VAL_FLOAT:   return value_new_float(v->float_val);
        case VAL_BOOL:    return value_new_bool(v->bool_val);
        case VAL_STRING:  return value_new_string(v->str_val);
        default:
            value_incref(v);
            return v;
    }
}

/* ------------------------------------------------------------------ Utilities */

char *value_to_string(Value *v) {
    if (!v) return strdup("null");
    char buf[64];
    switch (v->type) {
        case VAL_NULL:  return strdup("null");
        case VAL_INT:   snprintf(buf, sizeof(buf), "%lld", v->int_val); return strdup(buf);
        case VAL_FLOAT: snprintf(buf, sizeof(buf), "%g",   v->float_val); return strdup(buf);
        case VAL_BOOL:  return strdup(v->bool_val ? "true" : "false");
        case VAL_STRING: return strdup(v->str_val);
        case VAL_FUNCTION:
            snprintf(buf, sizeof(buf), "<fn:%s>", v->fn.name ? v->fn.name : "?");
            return strdup(buf);
        case VAL_BUILTIN_FN:
            snprintf(buf, sizeof(buf), "<builtin:%s>", v->builtin.name);
            return strdup(buf);
        case VAL_TUPLE: {
            /* build "(a, b, ...)" */
            size_t cap = 64, len = 0;
            char *s = malloc(cap);
            s[len++] = '(';
            for (int i = 0; i < v->tuple.count; i++) {
                if (i > 0) { if (len + 2 >= cap) { cap *= 2; s = realloc(s, cap); } s[len++] = ','; s[len++] = ' '; }
                if (v->tuple.names && v->tuple.names[i]) {
                    size_t nl = strlen(v->tuple.names[i]);
                    while (len + nl + 2 >= cap) { cap *= 2; s = realloc(s, cap); }
                    memcpy(s + len, v->tuple.names[i], nl); len += nl;
                    s[len++] = ':'; s[len++] = ' ';
                }
                char *es = value_to_string(v->tuple.elems[i]);
                size_t el = strlen(es);
                while (len + el + 2 >= cap) { cap *= 2; s = realloc(s, cap); }
                memcpy(s + len, es, el); len += el;
                free(es);
            }
            if (len + 2 >= cap) { cap += 4; s = realloc(s, cap); }
            s[len++] = ')'; s[len] = '\0';
            return s;
        }
        case VAL_PAT_INST: {
            size_t cap2 = 256, len2 = 0;
            char *s = malloc(cap2);
            const char *pname = v->pat_inst.def ? v->pat_inst.def->name : "?";
            size_t pnl = strlen(pname);
            while (len2 + pnl + 2 >= cap2) { cap2 *= 2; s = realloc(s, cap2); }
            memcpy(s + len2, pname, pnl); len2 += pnl;
            s[len2++] = '{';
            for (int i = 0; i < v->pat_inst.count; i++) {
                if (i > 0) {
                    while (len2 + 2 >= cap2) { cap2 *= 2; s = realloc(s, cap2); }
                    s[len2++] = ','; s[len2++] = ' ';
                }
                if (v->pat_inst.def && i < v->pat_inst.def->field_count && v->pat_inst.def->field_names[i]) {
                    const char *fn2 = v->pat_inst.def->field_names[i];
                    size_t fnl = strlen(fn2);
                    while (len2 + fnl + 2 >= cap2) { cap2 *= 2; s = realloc(s, cap2); }
                    memcpy(s + len2, fn2, fnl); len2 += fnl;
                    s[len2++] = ':'; s[len2++] = ' ';
                }
                char *fstr = value_to_string(v->pat_inst.fields[i]);
                size_t fsl = strlen(fstr);
                while (len2 + fsl + 2 >= cap2) { cap2 *= 2; s = realloc(s, cap2); }
                memcpy(s + len2, fstr, fsl); len2 += fsl;
                free(fstr);
            }
            while (len2 + 2 >= cap2) { cap2 *= 2; s = realloc(s, cap2); }
            s[len2++] = '}'; s[len2] = '\0';
            return s;
        }
        case VAL_TYPE:
            snprintf(buf, sizeof(buf), "<type:%s>", v->type_val.type_name ? v->type_val.type_name : "?");
            return strdup(buf);
        case VAL_MODULE:
            snprintf(buf, sizeof(buf), "<module:%s>", v->module.name ? v->module.name : "?");
            return strdup(buf);
        case VAL_OPTIONAL:
            if (v->optional.present) {
                char *inner = value_to_string(v->optional.val);
                size_t n = strlen(inner) + 16;
                char *r = malloc(n);
                snprintf(r, n, "some(%s)", inner);
                free(inner);
                return r;
            }
            return strdup("none");
        case VAL_SCOPE:
            return strdup("<scope>");
        case VAL_VARIANT: {
            char *inner = value_to_string(v->variant.val);
            size_t n = strlen(inner) + 32;
            char *r = malloc(n);
            snprintf(r, n, "variant(%d, %s)", v->variant.tag, inner);
            free(inner);
            return r;
        }
        default: return strdup("<unknown>");
    }
}

int value_is_truthy(Value *v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_NULL:   return 0;
        case VAL_INT:    return v->int_val != 0;
        case VAL_FLOAT:  return v->float_val != 0.0;
        case VAL_BOOL:   return v->bool_val;
        case VAL_STRING: return v->str_val && v->str_val[0] != '\0';
        case VAL_OPTIONAL: return v->optional.present;
        default:         return 1;
    }
}

int value_equals(Value *a, Value *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->type == VAL_NULL && b->type == VAL_NULL) return 1;
    if (a->type == VAL_INT && b->type == VAL_INT) return a->int_val == b->int_val;
    if (a->type == VAL_FLOAT && b->type == VAL_FLOAT) return a->float_val == b->float_val;
    if (a->type == VAL_INT && b->type == VAL_FLOAT) return (double)a->int_val == b->float_val;
    if (a->type == VAL_FLOAT && b->type == VAL_INT) return a->float_val == (double)b->int_val;
    if (a->type == VAL_BOOL && b->type == VAL_BOOL) return a->bool_val == b->bool_val;
    if (a->type == VAL_STRING && b->type == VAL_STRING) return strcmp(a->str_val, b->str_val) == 0;
    return 0;
}
