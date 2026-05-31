#include "compiler.h"
#include "backend/codegen.h"
#include "common/util.h"
#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "middle/symbols.h"
#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TARGET_NATIVE,
    TARGET_WIN,
    TARGET_LIN,
    TARGET_ANDROID,
    TARGET_IOS,
    TARGET_MAC
} BuildTarget;

typedef struct {
    const char* input;
    const char* output;
    BuildTarget target;
    int backend_c;
    int run_bundle;
} CompileOptions;

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

static const char* target_name(BuildTarget target) {
    switch (target) {
        case TARGET_WIN: return "win";
        case TARGET_LIN: return "lin";
        case TARGET_ANDROID: return "android";
        case TARGET_IOS: return "ios";
        case TARGET_MAC: return "mac";
        default: return "native";
    }
}

static BuildTarget parse_target(const char* value) {
    if (!value) return TARGET_NATIVE;
    if (strcmp(value, "win") == 0 || strcmp(value, "windows") == 0) return TARGET_WIN;
    if (strcmp(value, "lin") == 0 || strcmp(value, "linux") == 0) return TARGET_LIN;
    if (strcmp(value, "android") == 0) return TARGET_ANDROID;
    if (strcmp(value, "ios") == 0) return TARGET_IOS;
    if (strcmp(value, "mac") == 0 || strcmp(value, "macos") == 0) return TARGET_MAC;
    return TARGET_NATIVE;
}

static int parse_options(int argc, char** argv, CompileOptions* options) {
    options->input = NULL;
    options->output = NULL;
    options->target = TARGET_NATIVE;
    options->backend_c = 0;
    options->run_bundle = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run") == 0) {
            options->run_bundle = 1;
        } else if (strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--backend requires a value: vm or c\n");
                return 0;
            }
            const char* backend = argv[++i];
            if (strcmp(backend, "c") == 0) options->backend_c = 1;
            else if (strcmp(backend, "vm") == 0) options->backend_c = 0;
            else {
                fprintf(stderr, "Unknown backend '%s'. Use vm or c.\n", backend);
                return 0;
            }
        } else if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--target requires a value: win, lin, android, ios, or mac\n");
                return 0;
            }
            options->target = parse_target(argv[++i]);
            if (options->target == TARGET_NATIVE && strcmp(argv[i], "native") != 0) {
                fprintf(stderr, "Unknown target '%s'. Use win, lin, android, ios, or mac.\n", argv[i]);
                return 0;
            }
        } else if (!options->input) {
            options->input = argv[i];
        } else if (!options->output) {
            options->output = argv[i];
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return 0;
        }
    }
    return options->input != NULL;
}

static BuildTarget host_target(void) {
#if defined(_WIN32) || defined(_WIN64)
    return TARGET_WIN;
#elif defined(__APPLE__)
    return TARGET_MAC;
#else
    return TARGET_LIN;
#endif
}

static BuildTarget effective_target(BuildTarget requested) {
    return requested == TARGET_NATIVE ? host_target() : requested;
}

static void final_output_name(const CompileOptions* options, char* out, size_t out_size) {
    BuildTarget target = effective_target(options->target);
    const char* ext = options->backend_c && target == TARGET_WIN ? ".exe" : ".mlgb";
    if (options->output) snprintf(out, out_size, "%s", options->output);
    else snprintf(out, out_size, "app%s", ext);
    size_t len = strlen(out);
    size_t ext_len = strlen(ext);
    if (ext_len > 0 && (len < ext_len || strcmp(out + len - ext_len, ext) != 0)) {
        strncat(out, ext, out_size - len - 1);
    }
}

static const char* compiler_for_target(BuildTarget target) {
    BuildTarget host = host_target();
    switch (target) {
        case TARGET_WIN: return host == TARGET_WIN ? "gcc" : "x86_64-w64-mingw32-gcc";
        case TARGET_LIN: return host == TARGET_LIN ? "gcc" : NULL;
        case TARGET_MAC: return host == TARGET_MAC ? "clang" : NULL;
        case TARGET_IOS: return host == TARGET_MAC ? "xcrun -sdk iphoneos clang" : NULL;
        case TARGET_ANDROID: return "clang --target=aarch64-linux-android";
        default: return "gcc";
    }
}

static int build_command(BuildTarget target, const char* output, char* command, size_t command_size) {
    const char* compiler = compiler_for_target(target);
    if (!compiler) return 0;
    snprintf(command, command_size, "%s _temp_.c -o \"%s\" -Wall -Wextra%s", compiler, output,
             target == TARGET_WIN ? " -lws2_32" : "");
    return 1;
}

static void print_target_note(BuildTarget target) {
    if (target == TARGET_ANDROID) {
        printf("Target android selected. This requires an Android NDK clang in PATH, or a clang that supports --target=aarch64-linux-android.\n");
    } else if (target == TARGET_IOS) {
        printf("Target ios selected. This requires Xcode command line tools and xcrun on macOS.\n");
    } else if (target == TARGET_MAC) {
        printf("Target mac selected. This requires clang and macOS SDK headers on this machine.\n");
    } else if (target == TARGET_LIN && host_target() != TARGET_LIN) {
        printf("Target lin selected. Run this compiler on Linux/WSL, or install/use a real Linux cross compiler.\n");
    } else if (target == TARGET_WIN && host_target() != TARGET_WIN) {
        printf("Target win selected. This requires x86_64-w64-mingw32-gcc in PATH.\n");
    }
}

/*
static void old_final_output_name(int argc, char** argv, char* out, size_t out_size) {
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
*/

int mylang_main(int argc, char** argv) {
    CompileOptions options;
    if (argc < 2 || !parse_options(argc, argv, &options)) {
        printf("Usage: %s [--run] <file.mlg|file.mlgb> [output_name(optional)] [--backend vm|c] [--target win|lin|android|ios|mac]\n", argv[0]);
        return 1;
    }

    if (options.run_bundle) {
        return vm_run_bundle(options.input);
    }

    char* source = mlg_read_file(options.input);
    if (!source) {
        fprintf(stderr, "Open failed: %s\n", options.input);
        return 1;
    }

    AstNode* program = parse_file(options.input);
    if (!program) {
        free(source);
        return 1;
    }

    ImportSet loaded = {0};
    mark_loaded(&loaded, options.input);
    if (!resolve_imports(program, options.input, &loaded)) return 1;

    SymbolTable symbols;
    symbols_init(&symbols);
    symbols_collect_program(&symbols, program);

    char output[256];
    final_output_name(&options, output, sizeof(output));

    if (!options.backend_c) {
        if (!vm_write_bundle(output, source, target_name(effective_target(options.target)))) {
            fprintf(stderr, "Write failed: %s\n", output);
            free(source);
            symbols_free(&symbols);
            return 1;
        }
        printf("Built portable Mylang app: %s\n", output);
        printf("Run it with: %s --run %s\n", argv[0], output);
        free(source);
        symbols_free(&symbols);
        return 0;
    }

    char* c_source = codegen_program(program, &symbols);
    if (!write_text_file("_temp_.c", c_source)) {
        fprintf(stderr, "Write failed: _temp_.c\n");
        free(c_source);
        free(source);
        return 1;
    }

    char command[512];
    BuildTarget target = effective_target(options.target);
    printf("Compiled AST to _temp_.c\n");
    printf("Building target %s: %s\n", target_name(target), output);
    print_target_note(target);
    if (!build_command(target, output, command, sizeof(command))) {
        fprintf(stderr, "No configured compiler for target '%s' on this host yet.\n", target_name(target));
        free(c_source);
        free(source);
        symbols_free(&symbols);
        return 1;
    }
    int status = system(command);
    free(c_source);
    free(source);
    symbols_free(&symbols);
    return status == 0 ? 0 : 1;
}
