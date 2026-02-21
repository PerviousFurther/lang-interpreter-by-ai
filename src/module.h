#ifndef MODULE_H
#define MODULE_H

#include "interpreter.h"

/* Module cache entry */
typedef struct ModuleCache {
    char  *path;
    Value *module;
    struct ModuleCache *next;
} ModuleCache;

typedef struct {
    ModuleCache *head;
} ModuleSystem;

void   module_system_init(ModuleSystem *ms);
void   module_system_free(ModuleSystem *ms);
Value *load_module(ModuleSystem *ms, const char *path, Interpreter *interp);
void   resolve_import(AstNode *import_node, Env *env, ModuleSystem *ms, Interpreter *interp);

#endif /* MODULE_H */
