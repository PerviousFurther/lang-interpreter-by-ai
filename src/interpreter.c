#define _POSIX_C_SOURCE 200809L
#include "interpreter.h"
#include "builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ------------------------------------------------------------------ Env */

Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->parent = parent;
    e->ref_count = 1;
    if (parent) env_incref(parent);
    return e;
}

void env_incref(Env *e) { if (e) e->ref_count++; }

void env_decref(Env *e) {
    if (!e) return;
    e->ref_count--;
    if (e->ref_count > 0) return;
    EnvEntry *en = e->entries;
    while (en) {
        EnvEntry *next = en->next;
        free(en->name);
        value_decref(en->val);
        free(en);
        en = next;
    }
    env_decref(e->parent);
    free(e);
}

Value *env_get(Env *e, const char *name) {
    for (Env *cur = e; cur; cur = cur->parent) {
        for (EnvEntry *en = cur->entries; en; en = en->next) {
            if (strcmp(en->name, name) == 0) return en->val;
        }
    }
    return NULL;
}

static EnvEntry *env_find_entry(Env *e, const char *name) {
    for (Env *cur = e; cur; cur = cur->parent) {
        for (EnvEntry *en = cur->entries; en; en = en->next) {
            if (strcmp(en->name, name) == 0) return en;
        }
    }
    return NULL;
}

void env_def(Env *e, const char *name, Value *val) {
    /* Check if already in this scope */
    for (EnvEntry *en = e->entries; en; en = en->next) {
        if (strcmp(en->name, name) == 0) {
            value_decref(en->val);
            en->val = val;
            if (val) value_incref(val);
            return;
        }
    }
    EnvEntry *en = calloc(1, sizeof(EnvEntry));
    en->name = strdup(name);
    en->val  = val;
    if (val) value_incref(val);
    en->next = e->entries;
    e->entries = en;
}

void env_set(Env *e, const char *name, Value *val) {
    EnvEntry *en = env_find_entry(e, name);
    if (en) {
        value_decref(en->val);
        en->val = val;
        if (val) value_incref(val);
    } else {
        env_def(e, name, val);
    }
}

/* ------------------------------------------------------------------ EvalResult helpers */

static EvalResult ok(Value *v) {
    EvalResult r; r.sig = SIG_NONE; r.val = v; r.error_msg[0] = '\0'; return r;
}
static EvalResult sig_return(Value *v) {
    EvalResult r; r.sig = SIG_RETURN; r.val = v; r.error_msg[0] = '\0'; return r;
}
static EvalResult sig_break(void) {
    EvalResult r; r.sig = SIG_BREAK; r.val = NULL; r.error_msg[0] = '\0'; return r;
}
static EvalResult sig_yield(Value *v) {
    EvalResult r; r.sig = SIG_YIELD; r.val = v; r.error_msg[0] = '\0'; return r;
}
static EvalResult err(const char *msg, int line, int col) {
    EvalResult r;
    r.sig = SIG_ERROR;
    r.val = NULL;
    snprintf(r.error_msg, sizeof(r.error_msg), "Runtime error at line %d col %d: %s", line, col, msg);
    return r;
}

/* ------------------------------------------------------------------ forward */
static EvalResult eval_call(AstNode *node, Env *env);
static EvalResult eval_fn_call(Value *fn, Value **args, int argc, int line, int col);

/* ------------------------------------------------------------------ eval */

EvalResult eval(AstNode *node, Env *env) {
    if (!node) return ok(value_new_null());

    switch (node->type) {

    /* ---- literals ---- */
    case AST_NULL_LIT:  return ok(value_new_null());
    case AST_INT_LIT:   return ok(value_new_int(node->data.int_val));
    case AST_FLOAT_LIT: return ok(value_new_float(node->data.float_val));
    case AST_STR_LIT:   return ok(value_new_string(node->data.str_val));

    /* ---- identifier ---- */
    case AST_IDENT: {
        Value *v = env_get(env, node->name);
        if (!v) {
            char buf[128];
            snprintf(buf, sizeof(buf), "undefined variable '%s'", node->name);
            return err(buf, node->line, node->col);
        }
        value_incref(v);
        return ok(v);
    }

    /* ---- assignment ---- */
    case AST_ASSIGN: {
        EvalResult rhs = eval(node->body, env);
        if (rhs.sig != SIG_NONE) return rhs;
        /* lhs */
        AstNode *lhs = node->init;
        if (!lhs) return err("invalid assignment target", node->line, node->col);
        if (lhs->type == AST_IDENT) {
            env_set(env, lhs->name, rhs.val);
            Value *ret = rhs.val; value_incref(ret);
            value_decref(rhs.val);
            return ok(ret);
        } else if (lhs->type == AST_MEMBER) {
            EvalResult obj_r = eval(lhs->init, env);
            if (obj_r.sig != SIG_NONE) { value_decref(rhs.val); return obj_r; }
            Value *obj = obj_r.val;
            if (obj->type == VAL_PAT_INST && obj->pat_inst.def) {
                PatDef *def = obj->pat_inst.def;
                for (int i = 0; i < def->field_count; i++) {
                    if (def->field_names[i] && strcmp(def->field_names[i], lhs->name) == 0) {
                        value_decref(obj->pat_inst.fields[i]);
                        obj->pat_inst.fields[i] = rhs.val;
                        value_incref(rhs.val);
                        Value *ret = rhs.val; value_incref(ret);
                        value_decref(rhs.val);
                        value_decref(obj);
                        return ok(ret);
                    }
                }
            } else if (obj->type == VAL_SCOPE && obj->scope.env) {
                env_set(obj->scope.env, lhs->name, rhs.val);
                Value *ret = rhs.val; value_incref(ret);
                value_decref(rhs.val);
                value_decref(obj);
                return ok(ret);
            }
            value_decref(obj);
            value_decref(rhs.val);
            return err("cannot assign to member", lhs->line, lhs->col);
        } else if (lhs->type == AST_INDEX) {
            /* TODO: index assignment */
            value_decref(rhs.val);
            return err("index assignment not yet implemented", lhs->line, lhs->col);
        }
        value_decref(rhs.val);
        return err("invalid assignment target", node->line, node->col);
    }

    /* ---- unary ---- */
    case AST_UNOP: {
        EvalResult r = eval(node->init, env);
        if (r.sig != SIG_NONE) return r;
        Value *v = r.val;
        const char *op = node->op;
        if (strcmp(op, "-") == 0) {
            if (v->type == VAL_INT)   { Value *res = value_new_int(-v->int_val); value_decref(v); return ok(res); }
            if (v->type == VAL_FLOAT) { Value *res = value_new_float(-v->float_val); value_decref(v); return ok(res); }
        }
        if (strcmp(op, "!") == 0) {
            int t = value_is_truthy(v); value_decref(v);
            return ok(value_new_bool(!t));
        }
        if (strcmp(op, "~") == 0) {
            if (v->type == VAL_INT) { Value *res = value_new_int(~v->int_val); value_decref(v); return ok(res); }
        }
        value_decref(v);
        return err("unsupported unary op", node->line, node->col);
    }

    /* ---- binary ---- */
    case AST_BINOP: {
        if (node->child_count < 2) return err("binop needs 2 children", node->line, node->col);
        EvalResult lr = eval(node->children[0], env);
        if (lr.sig != SIG_NONE) return lr;
        EvalResult rr = eval(node->children[1], env);
        if (rr.sig != SIG_NONE) { value_decref(lr.val); return rr; }
        Value *l = lr.val, *r = rr.val;
        const char *op = node->op;

#define ARITH(sym, intop, floatop) \
    if (strcmp(op, sym) == 0) { \
        if (l->type == VAL_INT && r->type == VAL_INT) { \
            Value *res = value_new_int(l->int_val intop r->int_val); \
            value_decref(l); value_decref(r); return ok(res); \
        } \
        double lf = (l->type == VAL_FLOAT) ? l->float_val : (double)l->int_val; \
        double rf = (r->type == VAL_FLOAT) ? r->float_val : (double)r->int_val; \
        Value *res = value_new_float(lf floatop rf); \
        value_decref(l); value_decref(r); return ok(res); \
    }
#define CMP(sym, cop) \
    if (strcmp(op, sym) == 0) { \
        int res; \
        if (l->type == VAL_INT && r->type == VAL_INT) res = l->int_val cop r->int_val; \
        else { \
            double lf = (l->type == VAL_FLOAT) ? l->float_val : (double)l->int_val; \
            double rf = (r->type == VAL_FLOAT) ? r->float_val : (double)r->int_val; \
            res = lf cop rf; \
        } \
        value_decref(l); value_decref(r); return ok(value_new_bool(res)); \
    }

        ARITH("+", +, +)
        ARITH("-", -, -)
        ARITH("*", *, *)
        if (strcmp(op, "/") == 0) {
            if (l->type == VAL_INT && r->type == VAL_INT) {
                if (r->int_val == 0) { value_decref(l); value_decref(r); return err("division by zero", node->line, node->col); }
                Value *res = value_new_int(l->int_val / r->int_val);
                value_decref(l); value_decref(r); return ok(res);
            }
            double lf = (l->type == VAL_FLOAT) ? l->float_val : (double)l->int_val;
            double rf = (r->type == VAL_FLOAT) ? r->float_val : (double)r->int_val;
            Value *res = value_new_float(lf / rf);
            value_decref(l); value_decref(r); return ok(res);
        }
        if (strcmp(op, "%") == 0) {
            if (l->type == VAL_INT && r->type == VAL_INT) {
                if (r->int_val == 0) { value_decref(l); value_decref(r); return err("modulo by zero", node->line, node->col); }
                Value *res = value_new_int(l->int_val % r->int_val);
                value_decref(l); value_decref(r); return ok(res);
            }
        }
        CMP("<", <) CMP(">", >) CMP("<=", <=) CMP(">=", >=)
        if (strcmp(op, "==") == 0) { int eq = value_equals(l, r); value_decref(l); value_decref(r); return ok(value_new_bool(eq)); }
        if (strcmp(op, "!=") == 0) { int eq = value_equals(l, r); value_decref(l); value_decref(r); return ok(value_new_bool(!eq)); }
        if (strcmp(op, "&&") == 0) { int tv = value_is_truthy(l) && value_is_truthy(r); value_decref(l); value_decref(r); return ok(value_new_bool(tv)); }
        if (strcmp(op, "||") == 0) { int tv = value_is_truthy(l) || value_is_truthy(r); value_decref(l); value_decref(r); return ok(value_new_bool(tv)); }
        /* Bitwise */
        if (strcmp(op, "&") == 0 && l->type == VAL_INT && r->type == VAL_INT) { Value *res = value_new_int(l->int_val & r->int_val); value_decref(l); value_decref(r); return ok(res); }
        if (strcmp(op, "|") == 0 && l->type == VAL_INT && r->type == VAL_INT) { Value *res = value_new_int(l->int_val | r->int_val); value_decref(l); value_decref(r); return ok(res); }
        if (strcmp(op, "^") == 0 && l->type == VAL_INT && r->type == VAL_INT) { Value *res = value_new_int(l->int_val ^ r->int_val); value_decref(l); value_decref(r); return ok(res); }
        if (strcmp(op, "<<") == 0 && l->type == VAL_INT && r->type == VAL_INT) { Value *res = value_new_int(l->int_val << r->int_val); value_decref(l); value_decref(r); return ok(res); }
        if (strcmp(op, ">>") == 0 && l->type == VAL_INT && r->type == VAL_INT) { Value *res = value_new_int(l->int_val >> r->int_val); value_decref(l); value_decref(r); return ok(res); }
        /* String concatenation */
        if (strcmp(op, "+") == 0 && l->type == VAL_STRING && r->type == VAL_STRING) {
            size_t n = strlen(l->str_val) + strlen(r->str_val) + 1;
            char *s = malloc(n);
            strcpy(s, l->str_val); strcat(s, r->str_val);
            Value *res = value_new_string(s); free(s);
            value_decref(l); value_decref(r); return ok(res);
        }
        value_decref(l); value_decref(r);
        return err("unsupported binary operation", node->line, node->col);
    }

    /* ---- optional ?: ---- */
    case AST_OPTIONAL: {
        EvalResult cr = eval(node->cond, env);
        if (cr.sig != SIG_NONE) return cr;
        if (value_is_truthy(cr.val)) {
            value_decref(cr.val);
            return eval(node->init, env);
        }
        value_decref(cr.val);
        if (node->alt) return eval(node->alt, env);
        return ok(value_new_null());
    }

    /* ---- copy/move ---- */
    case AST_COPY: {
        EvalResult r = eval(node->init, env);
        if (r.sig != SIG_NONE) return r;
        Value *copied = value_copy(r.val);
        value_decref(r.val);
        return ok(copied);
    }
    case AST_MOVE: {
        /* treat same as eval for now */
        return eval(node->init, env);
    }

    /* ---- member access ---- */
    case AST_MEMBER: {
        EvalResult obj_r = eval(node->init, env);
        if (obj_r.sig != SIG_NONE) return obj_r;
        Value *obj = obj_r.val;
        const char *field = node->name;
        if (obj->type == VAL_PAT_INST && obj->pat_inst.def) {
            PatDef *def = obj->pat_inst.def;
            for (int i = 0; i < def->field_count; i++) {
                if (def->field_names[i] && strcmp(def->field_names[i], field) == 0) {
                    Value *fv = obj->pat_inst.fields[i];
                    value_incref(fv);
                    value_decref(obj);
                    return ok(fv);
                }
            }
        } else if (obj->type == VAL_SCOPE && obj->scope.env) {
            Value *v = env_get(obj->scope.env, field);
            if (v) { value_incref(v); value_decref(obj); return ok(v); }
        } else if (obj->type == VAL_MODULE && obj->module.env) {
            Value *v = env_get(obj->module.env, field);
            if (v) { value_incref(v); value_decref(obj); return ok(v); }
        } else if (obj->type == VAL_TUPLE) {
            /* access by name */
            if (obj->tuple.names) {
                for (int i = 0; i < obj->tuple.count; i++) {
                    if (obj->tuple.names[i] && strcmp(obj->tuple.names[i], field) == 0) {
                        Value *fv = obj->tuple.elems[i];
                        value_incref(fv);
                        value_decref(obj);
                        return ok(fv);
                    }
                }
            }
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "no member '%s'", field);
        value_decref(obj);
        return err(buf, node->line, node->col);
    }

    /* ---- index ---- */
    case AST_INDEX: {
        EvalResult obj_r = eval(node->init, env);
        if (obj_r.sig != SIG_NONE) return obj_r;
        if (node->child_count < 1) { value_decref(obj_r.val); return err("index missing", node->line, node->col); }
        EvalResult idx_r = eval(node->children[0], env);
        if (idx_r.sig != SIG_NONE) { value_decref(obj_r.val); return idx_r; }
        Value *obj = obj_r.val, *idx = idx_r.val;
        if (obj->type == VAL_TUPLE && idx->type == VAL_INT) {
            long long i = idx->int_val;
            if (i < 0) i += obj->tuple.count;
            if (i >= 0 && i < obj->tuple.count) {
                Value *fv = obj->tuple.elems[i];
                value_incref(fv);
                value_decref(obj); value_decref(idx);
                return ok(fv);
            }
            value_decref(obj); value_decref(idx);
            return err("tuple index out of range", node->line, node->col);
        }
        value_decref(obj); value_decref(idx);
        return err("index not supported for this type", node->line, node->col);
    }

    /* ---- call ---- */
    case AST_CALL: return eval_call(node, env);

    /* ---- tuple literal ---- */
    case AST_TUPLE: {
        Value *t = value_new_tuple(node->child_count);
        /* check for named elements (AST_ASSIGN children) */
        for (int i = 0; i < node->child_count; i++) {
            AstNode *child = node->children[i];
            if (child && child->type == AST_ASSIGN && child->init && child->init->type == AST_IDENT) {
                /* named: name = expr */
                if (!t->tuple.names) {
                    t->tuple.names = calloc((size_t)node->child_count, sizeof(char *));
                }
                t->tuple.names[i] = strdup(child->init->name);
                EvalResult r = eval(child->body, env);
                if (r.sig != SIG_NONE) { value_decref(t); return r; }
                t->tuple.elems[i] = r.val;
            } else if (child && child->type == AST_TYPE_ANN && child->name) {
                /* named: name:type = expr — for return tuples */
                if (!t->tuple.names) {
                    t->tuple.names = calloc((size_t)node->child_count, sizeof(char *));
                }
                t->tuple.names[i] = strdup(child->name);
                EvalResult r = eval(child->init ? child->init : child, env);
                if (r.sig != SIG_NONE) { value_decref(t); return r; }
                t->tuple.elems[i] = r.val;
            } else {
                EvalResult r = eval(child, env);
                if (r.sig != SIG_NONE) { value_decref(t); return r; }
                t->tuple.elems[i] = r.val;
            }
        }
        return ok(t);
    }

    /* ---- scope ---- */
    case AST_SCOPE: {
        Env *child_env = env_new(env);
        EvalResult r = eval_block(node, child_env);
        env_decref(child_env);
        return r;
    }

    /* ---- block (same as scope but in context) ---- */
    case AST_BLOCK: return eval_block(node, env);

    /* ---- program ---- */
    case AST_PROGRAM: {
        EvalResult r = ok(value_new_null());
        for (int i = 0; i < node->child_count; i++) {
            value_decref(r.val);
            r = eval(node->children[i], env);
            if (r.sig == SIG_ERROR) return r;
            if (r.sig == SIG_RETURN || r.sig == SIG_BREAK || r.sig == SIG_YIELD) {
                /* propagate signals from top level */
                if (r.sig != SIG_RETURN) return r;
                /* return at top level just gives the value */
                r.sig = SIG_NONE;
            }
        }
        return r;
    }

    /* ---- function declaration ---- */
    case AST_FN_DECL: {
        Value *fn = value_new_function(node, env, node->name);
        env_def(env, node->name, fn);
        value_decref(fn);
        return ok(value_new_null());
    }

    /* ---- variable declaration ---- */
    case AST_VAR_DECL: {
        Value *v = value_new_null();
        if (node->init) {
            EvalResult r = eval(node->init, env);
            if (r.sig != SIG_NONE) return r;
            value_decref(v);
            v = r.val;
        }
        env_def(env, node->name, v);
        value_decref(v);
        return ok(value_new_null());
    }

    /* ---- pattern declaration ---- */
    case AST_PAT_DECL: {
        /* Count pub fields from the body */
        int field_count = 0;
        if (node->body && node->body->type == AST_SCOPE) {
            for (int i = 0; i < node->body->child_count; i++) {
                AstNode *ch = node->body->children[i];
                if (ch && ch->type == AST_VAR_DECL) field_count++;
            }
        }
        PatDef *def = patdef_new(node->name, field_count);
        int fi = 0;
        if (node->body && node->body->type == AST_SCOPE) {
            for (int i = 0; i < node->body->child_count; i++) {
                AstNode *ch = node->body->children[i];
                if (ch && ch->type == AST_VAR_DECL) {
                    def->field_names[fi++] = strdup(ch->name);
                }
            }
        }

        /* Build module env with pattern name; patdef stored directly in module value */
        Env *pat_env = env_new(NULL);
        {
            Value *nv = value_new_string(node->name);
            env_def(pat_env, "__name__", nv);
            value_decref(nv);
        }

        /* Evaluate methods into the pat_env directly (no parent reference) */
        if (node->body && node->body->type == AST_SCOPE) {
            for (int i = 0; i < node->body->child_count; i++) {
                AstNode *ch = node->body->children[i];
                if (ch && ch->type == AST_FN_DECL) {
                    /* Define method in pat_env; closure will reference pat_env */
                    Value *fnv = value_new_function(ch, pat_env, ch->name);
                    env_def(pat_env, ch->name, fnv);
                    value_decref(fnv);
                }
            }
        }

        Value *mod = value_new_module(node->name, pat_env);
        env_decref(pat_env);
        mod->module.patdef = def;  /* module owns def (ref=1 from patdef_new) */
        env_def(env, node->name, mod);
        value_decref(mod);
        return ok(value_new_null());
    }

    /* ---- import ---- */
    case AST_IMPORT_DECL: {
        /* TODO: real module loading via module.c */
        return ok(value_new_null());
    }

    /* ---- for loop ---- */
    case AST_FOR: {
        /* evaluate range */
        EvalResult range_r = eval(node->cond, env);
        if (range_r.sig != SIG_NONE) return range_r;
        Value *range = range_r.val;

        /* iterate over tuple elements or integer range */
        const char *var_name = node->init ? node->init->name : "_";
        Value *result = value_new_null();

        if (range->type == VAL_TUPLE) {
            for (int i = 0; i < range->tuple.count; i++) {
                Env *loop_env = env_new(env);
                value_incref(range->tuple.elems[i]);
                env_def(loop_env, var_name, range->tuple.elems[i]);
                value_decref(range->tuple.elems[i]);
                EvalResult r = eval_block(node->body, loop_env);
                env_decref(loop_env);
                if (r.sig == SIG_BREAK) { value_decref(r.val); break; }
                if (r.sig == SIG_YIELD) { value_decref(result); result = r.val; continue; }
                if (r.sig == SIG_RETURN || r.sig == SIG_ERROR) { value_decref(range); value_decref(result); return r; }
                value_decref(r.val);
            }
        } else if (range->type == VAL_INT) {
            /* for i : N  →  0..N-1 */
            for (long long i = 0; i < range->int_val; i++) {
                Env *loop_env = env_new(env);
                env_def(loop_env, var_name, value_new_int(i));
                EvalResult r = eval_block(node->body, loop_env);
                env_decref(loop_env);
                if (r.sig == SIG_BREAK) { value_decref(r.val); break; }
                if (r.sig == SIG_YIELD) { value_decref(result); result = r.val; continue; }
                if (r.sig == SIG_RETURN || r.sig == SIG_ERROR) { value_decref(range); value_decref(result); return r; }
                value_decref(r.val);
            }
        }
        value_decref(range);
        return ok(result);
    }

    /* ---- while loop ---- */
    case AST_WHILE: {
        Value *result = value_new_null();
        for (;;) {
            if (node->cond) {
                EvalResult cr = eval(node->cond, env);
                if (cr.sig != SIG_NONE) { value_decref(result); return cr; }
                int t = value_is_truthy(cr.val);
                value_decref(cr.val);
                if (!t) break;
            }
            Env *loop_env = env_new(env);
            EvalResult r = eval_block(node->body, loop_env);
            env_decref(loop_env);
            if (r.sig == SIG_BREAK) { value_decref(r.val); break; }
            if (r.sig == SIG_YIELD) { value_decref(result); result = r.val; continue; }
            if (r.sig == SIG_RETURN || r.sig == SIG_ERROR) { value_decref(result); return r; }
            value_decref(r.val);
            /* trailing condition */
            if (node->alt) {
                EvalResult cr = eval(node->alt, env);
                if (cr.sig != SIG_NONE) { value_decref(result); return cr; }
                int t = value_is_truthy(cr.val);
                value_decref(cr.val);
                if (!t) break;
            }
        }
        return ok(result);
    }

    /* ---- switch ---- */
    case AST_SWITCH: {
        EvalResult tag_r = eval(node->cond, env);
        if (tag_r.sig != SIG_NONE) return tag_r;
        Value *tag = tag_r.val;
        Value *result = value_new_null();
        int matched = 0;
        for (int i = 0; i < node->child_count; i++) {
            AstNode *cas = node->children[i];
            if (!cas) continue;
            if (!cas->cond) { /* default */
                matched = 1;
            } else {
                EvalResult cv = eval(cas->cond, env);
                if (cv.sig != SIG_NONE) { value_decref(tag); value_decref(result); return cv; }
                matched = value_equals(tag, cv.val);
                value_decref(cv.val);
            }
            if (matched) {
                Env *case_env = env_new(env);
                EvalResult r = eval_block(cas, case_env);
                env_decref(case_env);
                value_decref(result);
                result = r.val ? r.val : value_new_null();
                if (r.sig == SIG_BREAK) { r.sig = SIG_NONE; }
                else if (r.sig != SIG_NONE) { value_decref(tag); return r; }
                break;
            }
        }
        value_decref(tag);
        return ok(result);
    }

    /* ---- break / yield / return ---- */
    case AST_BREAK:  return sig_break();
    case AST_YIELD: {
        if (node->init) {
            EvalResult r = eval(node->init, env);
            if (r.sig != SIG_NONE) return r;
            return sig_yield(r.val);
        }
        return sig_yield(value_new_null());
    }
    case AST_RETURN: {
        if (node->init) {
            EvalResult r = eval(node->init, env);
            if (r.sig != SIG_NONE) return r;
            return sig_return(r.val);
        }
        return sig_return(value_new_null());
    }

    /* ---- template instantiation used as type prefix: <Pat>(...) ---- */
    case AST_TEMPLATE_INST: {
        /* If used as a call base (postfix handles it), here it is just a type node */
        /* Return a type value */
        if (node->child_count > 0 && node->children[0] && node->children[0]->type == AST_TYPE_ANN) {
            const char *tname = node->children[0]->data.str_val;
            if (tname) return ok(value_new_type(tname));
        }
        return ok(value_new_null());
    }

    /* ---- type annotation (used as expr) ---- */
    case AST_TYPE_ANN: {
        if (node->data.str_val) {
            Value *v = env_get(env, node->data.str_val);
            if (v) { value_incref(v); return ok(v); }
            return ok(value_new_type(node->data.str_val));
        }
        return ok(value_new_null());
    }

    default:
        return err("unhandled AST node type", node->line, node->col);
    }
}

/* ------------------------------------------------------------------ eval_block (defined after eval) */

EvalResult eval_block(AstNode *block, Env *env) {
    if (!block) return ok(value_new_null());
    EvalResult r = ok(value_new_null());
    for (int i = 0; i < block->child_count; i++) {
        value_decref(r.val);
        r.val = NULL;
        r = eval(block->children[i], env);
        if (r.sig != SIG_NONE) return r;
    }
    return r;
}

/* ------------------------------------------------------------------ function call */

static EvalResult eval_call(AstNode *node, Env *env) {
    /* evaluate callee */
    EvalResult fn_r = eval(node->init, env);
    if (fn_r.sig != SIG_NONE) return fn_r;
    Value *fn = fn_r.val;

    /* evaluate arguments */
    int argc = node->child_count;
    Value **args = calloc((size_t)(argc ? argc : 1), sizeof(Value *));
    for (int i = 0; i < argc; i++) {
        EvalResult ar = eval(node->children[i], env);
        if (ar.sig != SIG_NONE) {
            for (int j = 0; j < i; j++) value_decref(args[j]);
            free(args);
            value_decref(fn);
            return ar;
        }
        args[i] = ar.val;
    }

    EvalResult result = eval_fn_call(fn, args, argc, node->line, node->col);
    for (int i = 0; i < argc; i++) value_decref(args[i]);
    free(args);
    value_decref(fn);
    return result;
}

static EvalResult eval_fn_call(Value *fn, Value **args, int argc, int line, int col) {
    if (!fn) return err("called null value", line, col);

    if (fn->type == VAL_BUILTIN_FN) {
        Value *r = fn->builtin.fn(args, argc);
        return ok(r ? r : value_new_null());
    }

    if (fn->type == VAL_FUNCTION) {
        AstNode *decl = fn->fn.ast;
        Env *call_env = env_new(fn->fn.closure);

        /* bind parameters */
        int param_idx = 0;
        for (int i = 0; i < decl->child_count; i++) {
            AstNode *param = decl->children[i];
            if (!param || param->type != AST_PARAM) continue;
            Value *arg = (param_idx < argc) ? args[param_idx] : value_new_null();
            env_def(call_env, param->name ? param->name : "_", arg);
            if (param_idx >= argc) value_decref(arg); /* drop our ref; env holds its own */
            param_idx++;
        }

        /* execute body */
        EvalResult r;
        if (decl->body) {
            r = eval_block(decl->body, call_env);
        } else {
            r = ok(value_new_null());
        }

        env_decref(call_env);

        if (r.sig == SIG_RETURN) { r.sig = SIG_NONE; return r; }
        if (r.sig == SIG_ERROR)  return r;
        return r;
    }

    /* Pattern instantiation: PatName(field_vals...) */
    if (fn->type == VAL_MODULE && fn->module.patdef) {
        PatDef *def = fn->module.patdef;
        Value *inst = value_new_pat_inst(def, def->field_count);
        for (int i = 0; i < def->field_count && i < argc; i++) {
            inst->pat_inst.fields[i] = args[i];
            value_incref(args[i]);
        }
        /* fill remaining with null */
        for (int i = argc; i < def->field_count; i++) {
            inst->pat_inst.fields[i] = value_new_null();
        }
        return ok(inst);
    }

    if (fn->type == VAL_TYPE) {
        /* type conversion / construction */
        const char *tname = fn->type_val.type_name;
        if (argc == 1) {
            Value *arg = args[0];
            /* Numeric conversions */
            if (strncmp(tname, "i", 1) == 0 || strncmp(tname, "u", 1) == 0) {
                if (arg->type == VAL_INT)   return ok(value_new_int(arg->int_val));
                if (arg->type == VAL_FLOAT) return ok(value_new_int((long long)arg->float_val));
                if (arg->type == VAL_STRING) return ok(value_new_int(strtoll(arg->str_val, NULL, 10)));
            }
            if (strncmp(tname, "f", 1) == 0) {
                if (arg->type == VAL_FLOAT) return ok(value_new_float(arg->float_val));
                if (arg->type == VAL_INT)   return ok(value_new_float((double)arg->int_val));
                if (arg->type == VAL_STRING) return ok(value_new_float(strtod(arg->str_val, NULL)));
            }
            if (strcmp(tname, "string") == 0) {
                char *s = value_to_string(arg);
                Value *r = value_new_string(s); free(s);
                return ok(r);
            }
        }
        return ok(value_new_null());
    }

    return err("not a callable value", line, col);
}

/* ------------------------------------------------------------------ Interpreter */

void interp_init(Interpreter *interp) {
    interp->global = env_new(NULL);
    interp->had_error = 0;
    interp->error_msg[0] = '\0';
    builtins_register(interp->global);
}

void interp_run(Interpreter *interp, AstNode *program) {
    EvalResult r = eval(program, interp->global);
    if (r.sig == SIG_ERROR) {
        interp->had_error = 1;
        strncpy(interp->error_msg, r.error_msg, sizeof(interp->error_msg) - 1);
    }
    value_decref(r.val);
}

void interp_free(Interpreter *interp) {
    env_decref(interp->global);
    interp->global = NULL;
}
