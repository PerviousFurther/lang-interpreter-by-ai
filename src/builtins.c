#include "builtins.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ helper */

static Value *check_argc(Value **args, int argc, int expected, const char *name) {
    if (argc < expected) {
        fprintf(stderr, "builtin %s: expected %d args, got %d\n", name, expected, argc);
        return NULL;
    }
    (void)args;
    return (Value *)1; /* non-null = ok */
}

/* ------------------------------------------------------------------ I/O */

static Value *builtin_print(Value **args, int argc) {
    for (int i = 0; i < argc; i++) {
        char *s = value_to_string(args[i]);
        printf("%s", s);
        free(s);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return value_new_null();
}

static Value *builtin_println(Value **args, int argc) {
    return builtin_print(args, argc);
}

static Value *builtin_input(Value **args, int argc) {
    if (argc > 0) {
        char *prompt = value_to_string(args[0]);
        printf("%s", prompt);
        free(prompt);
    }
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) return value_new_string("");
    /* strip trailing newline */
    size_t n = strlen(buf);
    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
    return value_new_string(buf);
}

/* ------------------------------------------------------------------ type conversions */

static Value *builtin_int(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "int")) return value_new_null();
    Value *a = args[0];
    if (a->type == VAL_INT)   return value_new_int(a->int_val);
    if (a->type == VAL_FLOAT) return value_new_int((long long)a->float_val);
    if (a->type == VAL_BOOL)  return value_new_int(a->bool_val);
    if (a->type == VAL_STRING) return value_new_int(strtoll(a->str_val, NULL, 10));
    return value_new_null();
}

static Value *builtin_float(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "float")) return value_new_null();
    Value *a = args[0];
    if (a->type == VAL_FLOAT) return value_new_float(a->float_val);
    if (a->type == VAL_INT)   return value_new_float((double)a->int_val);
    if (a->type == VAL_BOOL)  return value_new_float(a->bool_val ? 1.0 : 0.0);
    if (a->type == VAL_STRING) return value_new_float(strtod(a->str_val, NULL));
    return value_new_null();
}

static Value *builtin_string(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "string")) return value_new_null();
    char *s = value_to_string(args[0]);
    Value *r = value_new_string(s);
    free(s);
    return r;
}

static Value *builtin_bool(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "bool")) return value_new_null();
    return value_new_bool(value_is_truthy(args[0]));
}

/* ------------------------------------------------------------------ type checks */

static Value *builtin_is_null(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "is_null")) return value_new_null();
    return value_new_bool(args[0]->type == VAL_NULL);
}

static Value *builtin_is_int(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "is_int")) return value_new_null();
    return value_new_bool(args[0]->type == VAL_INT);
}

static Value *builtin_is_float(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "is_float")) return value_new_null();
    return value_new_bool(args[0]->type == VAL_FLOAT);
}

static Value *builtin_is_string(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "is_string")) return value_new_null();
    return value_new_bool(args[0]->type == VAL_STRING);
}

static Value *builtin_type_of(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "type_of")) return value_new_null();
    static const char *names[] = {
        "null","int","float","string","bool","tuple","variant",
        "function","pat_inst","scope","builtin_fn","optional","type","module"
    };
    if ((int)args[0]->type < (int)(sizeof(names)/sizeof(names[0])))
        return value_new_string(names[args[0]->type]);
    return value_new_string("unknown");
}

/* ------------------------------------------------------------------ math */

static Value *builtin_abs(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "abs")) return value_new_null();
    if (args[0]->type == VAL_INT)   return value_new_int(llabs(args[0]->int_val));
    if (args[0]->type == VAL_FLOAT) return value_new_float(fabs(args[0]->float_val));
    return value_new_null();
}

static Value *builtin_sqrt(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "sqrt")) return value_new_null();
    double v = (args[0]->type == VAL_INT) ? (double)args[0]->int_val : args[0]->float_val;
    return value_new_float(sqrt(v));
}

static Value *builtin_pow(Value **args, int argc) {
    if (!check_argc(args, argc, 2, "pow")) return value_new_null();
    double b = (args[0]->type == VAL_INT) ? (double)args[0]->int_val : args[0]->float_val;
    double e = (args[1]->type == VAL_INT) ? (double)args[1]->int_val : args[1]->float_val;
    return value_new_float(pow(b, e));
}

static Value *builtin_floor(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "floor")) return value_new_null();
    double v = (args[0]->type == VAL_INT) ? (double)args[0]->int_val : args[0]->float_val;
    return value_new_int((long long)floor(v));
}

static Value *builtin_ceil(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "ceil")) return value_new_null();
    double v = (args[0]->type == VAL_INT) ? (double)args[0]->int_val : args[0]->float_val;
    return value_new_int((long long)ceil(v));
}

static Value *builtin_min(Value **args, int argc) {
    if (!check_argc(args, argc, 2, "min")) return value_new_null();
    if (args[0]->type == VAL_INT && args[1]->type == VAL_INT)
        return value_new_int(args[0]->int_val < args[1]->int_val ? args[0]->int_val : args[1]->int_val);
    double a = (args[0]->type == VAL_INT) ? (double)args[0]->int_val : args[0]->float_val;
    double b = (args[1]->type == VAL_INT) ? (double)args[1]->int_val : args[1]->float_val;
    return value_new_float(a < b ? a : b);
}

static Value *builtin_max(Value **args, int argc) {
    if (!check_argc(args, argc, 2, "max")) return value_new_null();
    if (args[0]->type == VAL_INT && args[1]->type == VAL_INT)
        return value_new_int(args[0]->int_val > args[1]->int_val ? args[0]->int_val : args[1]->int_val);
    double a = (args[0]->type == VAL_INT) ? (double)args[0]->int_val : args[0]->float_val;
    double b = (args[1]->type == VAL_INT) ? (double)args[1]->int_val : args[1]->float_val;
    return value_new_float(a > b ? a : b);
}

/* ------------------------------------------------------------------ string ops */

static Value *builtin_len(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "len")) return value_new_null();
    if (args[0]->type == VAL_STRING) return value_new_int((long long)strlen(args[0]->str_val));
    if (args[0]->type == VAL_TUPLE)  return value_new_int(args[0]->tuple.count);
    return value_new_null();
}

static Value *builtin_substr(Value **args, int argc) {
    if (!check_argc(args, argc, 3, "substr")) return value_new_null();
    if (args[0]->type != VAL_STRING) return value_new_null();
    const char *s = args[0]->str_val;
    long long start = args[1]->int_val;
    long long length = args[2]->int_val;
    long long slen = (long long)strlen(s);
    if (start < 0) start = 0;
    if (start > slen) start = slen;
    if (length < 0) length = 0;
    if (start + length > slen) length = slen - start;
    char *buf = malloc((size_t)length + 1);
    memcpy(buf, s + start, (size_t)length);
    buf[length] = '\0';
    Value *r = value_new_string(buf);
    free(buf);
    return r;
}

static Value *builtin_concat(Value **args, int argc) {
    size_t total = 0;
    for (int i = 0; i < argc; i++) {
        if (args[i]->type == VAL_STRING) total += strlen(args[i]->str_val);
    }
    char *buf = malloc(total + 1);
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (args[i]->type == VAL_STRING) strcat(buf, args[i]->str_val);
    }
    Value *r = value_new_string(buf);
    free(buf);
    return r;
}

/* ------------------------------------------------------------------ type reflection */

static Value *builtin_type(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "type")) return value_new_null();
    return value_type_of(args[0]);
}

/* ------------------------------------------------------------------ assert */

static Value *builtin_assert(Value **args, int argc) {
    if (!check_argc(args, argc, 1, "assert")) return value_new_null();
    if (!value_is_truthy(args[0])) {
        if (argc >= 2 && args[1]->type == VAL_STRING) {
            fprintf(stderr, "Assertion failed: %s\n", args[1]->str_val);
        } else {
            fprintf(stderr, "Assertion failed\n");
        }
        exit(1);
    }
    return value_new_null();
}

/* ------------------------------------------------------------------ register */

void builtins_register(Env *env) {
#define REG(name, fn) do { Value *_v = value_new_builtin(fn, name); env_def(env, name, _v); value_decref(_v); } while(0)
    REG("print",    builtin_print);
    REG("println",  builtin_println);
    REG("input",    builtin_input);
    REG("int",      builtin_int);
    REG("float",    builtin_float);
    REG("string",   builtin_string);
    REG("bool",     builtin_bool);
    REG("is_null",  builtin_is_null);
    REG("is_int",   builtin_is_int);
    REG("is_float", builtin_is_float);
    REG("is_string",builtin_is_string);
    REG("type_of",  builtin_type_of);
    REG("type",     builtin_type);
    REG("abs",      builtin_abs);
    REG("sqrt",     builtin_sqrt);
    REG("pow",      builtin_pow);
    REG("floor",    builtin_floor);
    REG("ceil",     builtin_ceil);
    REG("min",      builtin_min);
    REG("max",      builtin_max);
    REG("len",      builtin_len);
    REG("substr",   builtin_substr);
    REG("concat",   builtin_concat);
    REG("assert",   builtin_assert);
#undef REG
}
