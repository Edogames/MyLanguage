#include "compiler.h"
#include "backend/codegen.h"
#include "common/util.h"
#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "middle/symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char paths[128][512];
    int count;
} ImportSet;

static int already_loaded(ImportSet* set, const char* path) {
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->paths[i], path) == 0) return 1;
    }
    return 0;
}

static void mark_loaded(ImportSet* set, const char* path) {
    if (set->count < 128) snprintf(set->paths[set->count++], 512, "%s", path);
}

static int import_wants(AstNode* import_node, const char* name) {
    if (import_node->import_stmt.wildcard) return 1;
    for (int i = 0; i < import_node->import_stmt.item_count; i++) {
        if (strcmp(import_node->import_stmt.items[i], name) == 0) return 1;
    }
    return 0;
}

static AstNode* parse_file(const char* path) {
    char* source = mlg_read_file(path);
    if (!source) {
        fprintf(stderr, "Open failed: %s\n", path);
        return NULL;
    }
    TokenArray tokens;
    if (!lex_source(source, path, &tokens)) {
        free(source);
        return NULL;
    }
    AstNode* ast = parse_tokens(&tokens, path);
    tokens_free(&tokens);
    free(source);
    return ast;
}

static void prepend_imported_functions(AstNode* program, AstNode** funcs, int count) {
    if (count == 0) return;
    NodeArray old = program->program.statements;
    NodeArray next;
    nodes_init(&next);
    for (int i = 0; i < count; i++) nodes_push(&next, funcs[i]);
    for (int i = 0; i < old.count; i++) nodes_push(&next, old.items[i]);
    free(old.items);
    program->program.statements = next;
}

static int resolve_imports(AstNode* program, const char* file_path, ImportSet* loaded) {
    char base_dir[512];
    mlg_path_dirname(file_path, base_dir, sizeof(base_dir));
    AstNode* imported_funcs[512];
    int imported_count = 0;

    for (int i = 0; i < program->program.statements.count; i++) {
        AstNode* stmt = program->program.statements.items[i];
        if (stmt->kind != NODE_IMPORT) continue;
        char import_path[512];
        mlg_path_join_module(base_dir, stmt->import_stmt.module, import_path, sizeof(import_path));
        if (already_loaded(loaded, import_path)) continue;
        mark_loaded(loaded, import_path);

        AstNode* module = parse_file(import_path);
        if (!module) return 0;
        if (!resolve_imports(module, import_path, loaded)) return 0;
        for (int j = 0; j < module->program.statements.count; j++) {
            AstNode* candidate = module->program.statements.items[j];
            if (candidate->kind == NODE_FUNC && import_wants(stmt, candidate->func.name)) {
                imported_funcs[imported_count++] = candidate;
            }
        }
    }
    prepend_imported_functions(program, imported_funcs, imported_count);
    return 1;
}

static int write_text_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return 1;
}

static void final_output_name(int argc, char** argv, char* out, size_t out_size) {
#if defined(_WIN32) || defined(_WIN64)
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    if (argc > 2) snprintf(out, out_size, "%s", argv[2]);
    else snprintf(out, out_size, "app%s", ext);
    size_t len = strlen(out);
    size_t ext_len = strlen(ext);
    if (ext_len > 0 && (len < ext_len || strcmp(out + len - ext_len, ext) != 0)) {
        strncat(out, ext, out_size - len - 1);
    }
}

int mylang_main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <file.mlg> [output_name(optional)]\n", argv[0]);
        return 1;
    }

    AstNode* program = parse_file(argv[1]);
    if (!program) return 1;

    ImportSet loaded = {0};
    mark_loaded(&loaded, argv[1]);
    if (!resolve_imports(program, argv[1], &loaded)) return 1;

    SymbolTable symbols;
    symbols_init(&symbols);
    symbols_collect_program(&symbols, program);

    char* c_source = codegen_program(program, &symbols);
    if (!write_text_file("_temp_.c", c_source)) {
        fprintf(stderr, "Write failed: _temp_.c\n");
        free(c_source);
        return 1;
    }

    char output[256];
    final_output_name(argc, argv, output, sizeof(output));
    char command[512];
    snprintf(command, sizeof(command), "gcc _temp_.c -o \"%s\" -Wall -Wextra", output);
    printf("Compiled AST to _temp_.c\n");
    printf("Building executable: %s\n", output);
    int status = system(command);
    free(c_source);
    symbols_free(&symbols);
    return status == 0 ? 0 : 1;
}

