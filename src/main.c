#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
#include "module.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "0.1.0"

static void print_help(const char *prog) {
    printf("Usage: %s [options] [file.lang]\n", prog);
    printf("Options:\n");
    printf("  -h, --help       Show this help message\n");
    printf("  -v, --version    Show version\n");
    printf("If no file is given, starts an interactive REPL.\n");
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int run_source(Interpreter *interp, const char *src, const char *filename) {
    Lexer lex;
    lexer_init(&lex, src);
    Parser parser;
    parser_init(&parser, &lex);
    AstNode *program = parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "%s: %s\n", filename, parser.error_msg);
        ast_free(program);
        token_free(&parser.cur);
        return 1;
    }
    token_free(&parser.cur);

    interp_run(interp, program);
    ast_free(program);

    if (interp->had_error) {
        fprintf(stderr, "%s\n", interp->error_msg);
        interp->had_error = 0;
        return 1;
    }
    return 0;
}

static void repl(Interpreter *interp) {
    char line[4096];
    printf("lang-interpreter v%s  (type 'exit' to quit)\n", VERSION);
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strncmp(line, "exit", 4) == 0) break;

        Lexer lex;
        lexer_init(&lex, line);
        Parser parser;
        parser_init(&parser, &lex);
        AstNode *program = parse_program(&parser);

        if (parser.had_error) {
            fprintf(stderr, "Parse error: %s\n", parser.error_msg);
            ast_free(program);
            token_free(&parser.cur);
            continue;
        }
        token_free(&parser.cur);

        /* Evaluate and print result */
        if (program && program->child_count > 0) {
            EvalResult r = eval(program->children[program->child_count - 1], interp->global);
            if (r.sig == SIG_ERROR) {
                fprintf(stderr, "%s\n", r.error_msg);
            } else if (r.val && r.val->type != VAL_NULL) {
                char *s = value_to_string(r.val);
                printf("%s\n", s);
                free(s);
            }
            value_decref(r.val);
        }

        ast_free(program);
    }
}

int main(int argc, char **argv) {
    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("lang-interpreter %s\n", VERSION);
            return 0;
        }
    }

    Interpreter interp;
    interp_init(&interp);

    if (argc < 2) {
        repl(&interp);
    } else {
        const char *filename = argv[1];
        char *src = read_file(filename);
        if (!src) { interp_free(&interp); return 1; }
        int ret = run_source(&interp, src, filename);
        free(src);
        interp_free(&interp);
        return ret;
    }

    interp_free(&interp);
    return 0;
}
