#define _POSIX_C_SOURCE 200809L
#include "module.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void module_system_init(ModuleSystem *ms) {
    ms->head = NULL;
}

void module_system_free(ModuleSystem *ms) {
    ModuleCache *c = ms->head;
    while (c) {
        ModuleCache *next = c->next;
        free(c->path);
        value_decref(c->module);
        free(c);
        c = next;
    }
    ms->head = NULL;
}

static Value *cache_lookup(ModuleSystem *ms, const char *path) {
    for (ModuleCache *c = ms->head; c; c = c->next) {
        if (strcmp(c->path, path) == 0) return c->module;
    }
    return NULL;
}

static void cache_insert(ModuleSystem *ms, const char *path, Value *mod) {
    ModuleCache *c = calloc(1, sizeof(ModuleCache));
    c->path = strdup(path);
    c->module = mod;
    value_incref(mod);
    c->next = ms->head;
    ms->head = c;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

Value *load_module(ModuleSystem *ms, const char *path, Interpreter *interp) {
    Value *cached = cache_lookup(ms, path);
    if (cached) { value_incref(cached); return cached; }

    char *src = read_file(path);
    if (!src) {
        fprintf(stderr, "Module not found: %s\n", path);
        return value_new_null();
    }

    Lexer lex;
    lexer_init(&lex, src);
    Parser parser;
    parser_init(&parser, &lex);
    AstNode *program = parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "Parse error in module %s: %s\n", path, parser.error_msg);
        ast_free(program);
        free(src);
        token_free(&parser.cur);
        return value_new_null();
    }

    /* Run in a fresh module environment */
    Env *mod_env = env_new(interp->global);
    EvalResult r = eval(program, mod_env);
    ast_free(program);
    free(src);
    token_free(&parser.cur);

    if (r.sig == SIG_ERROR) {
        fprintf(stderr, "Runtime error in module %s: %s\n", path, r.error_msg);
        value_decref(r.val);
        env_decref(mod_env);
        return value_new_null();
    }
    value_decref(r.val);

    /* extract module name from path */
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    /* strip .lang extension */
    char name_buf[256];
    strncpy(name_buf, base, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf)-1] = '\0';
    char *dot = strrchr(name_buf, '.');
    if (dot) *dot = '\0';

    Value *mod = value_new_module(name_buf, mod_env);
    env_decref(mod_env);
    cache_insert(ms, path, mod);
    return mod;
}

void resolve_import(AstNode *import_node, Env *env, ModuleSystem *ms, Interpreter *interp) {
    if (!import_node || import_node->type != AST_IMPORT_DECL) return;

    /* Build file path from module name (dots → slashes), preserving .lang extension */
    const char *mod_name = import_node->name;
    char path_buf[512];
    /* Replace dots in module name with slashes, then append .lang */
    size_t i = 0;
    for (; mod_name[i] && i < sizeof(path_buf) - 6; i++) {
        path_buf[i] = (mod_name[i] == '.') ? '/' : mod_name[i];
    }
    snprintf(path_buf + i, sizeof(path_buf) - i, ".lang");

    Value *mod = load_module(ms, path_buf, interp);
    const char *alias = import_node->op ? import_node->op : mod_name;

    if (import_node->child_count == 0) {
        /* import as alias */
        env_def(env, alias, mod);
    } else {
        /* import specific items */
        for (int i = 0; i < import_node->child_count; i++) {
            AstNode *item = import_node->children[i];
            if (!item) continue;
            const char *iname = item->name;
            const char *ialias = item->op ? item->op : iname;
            Value *v = NULL;
            if (mod->type == VAL_MODULE && mod->module.env) {
                v = env_get(mod->module.env, iname);
            }
            if (v) {
                value_incref(v);
                env_def(env, ialias, v);
                value_decref(v);
            }
        }
    }

    value_decref(mod);
}
