#include "symbols.h"
#include "../common/util.h"

#include <stdlib.h>
#include <string.h>

void symbols_init(SymbolTable* st) {
    st->symbol_count = 0;
    st->symbol_cap = 128;
    st->symbols = (Symbol*)calloc((size_t)st->symbol_cap, sizeof(Symbol));
    st->func_count = 0;
    st->func_cap = 64;
    st->funcs = (FunctionInfo*)calloc((size_t)st->func_cap, sizeof(FunctionInfo));
}

void symbols_free(SymbolTable* st) {
    for (int i = 0; i < st->symbol_count; i++) {
        free(st->symbols[i].name);
        free(st->symbols[i].type);
    }
    for (int i = 0; i < st->func_count; i++) {
        free(st->funcs[i].name);
        free(st->funcs[i].return_type);
    }
    free(st->symbols);
    free(st->funcs);
}

void symbols_add(SymbolTable* st, const char* name, const char* type, int is_ref) {
    if (!name || !name[0]) return;
    for (int i = st->symbol_count - 1; i >= 0; i--) {
        if (strcmp(st->symbols[i].name, name) == 0) return;
    }
    if (st->symbol_count >= st->symbol_cap) {
        st->symbol_cap *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, sizeof(Symbol) * (size_t)st->symbol_cap);
    }
    st->symbols[st->symbol_count++] = (Symbol){mlg_strdup(name), mlg_strdup(type), is_ref};
}

const Symbol* symbols_find(SymbolTable* st, const char* name) {
    for (int i = st->symbol_count - 1; i >= 0; i--) {
        if (strcmp(st->symbols[i].name, name) == 0) return &st->symbols[i];
    }
    return NULL;
}

void symbols_add_function(SymbolTable* st, AstNode* func) {
    if (!func || func->kind != NODE_FUNC) return;
    if (symbols_find_function(st, func->func.name)) return;
    if (st->func_count >= st->func_cap) {
        st->func_cap *= 2;
        st->funcs = (FunctionInfo*)realloc(st->funcs, sizeof(FunctionInfo) * (size_t)st->func_cap);
    }
    FunctionInfo* info = &st->funcs[st->func_count++];
    info->name = mlg_strdup(func->func.name);
    info->return_type = mlg_strdup(func->func.return_type);
    info->params = func->func.params;
    info->param_count = func->func.param_count;
}

FunctionInfo* symbols_find_function(SymbolTable* st, const char* name) {
    for (int i = 0; i < st->func_count; i++) {
        if (strcmp(st->funcs[i].name, name) == 0) return &st->funcs[i];
    }
    return NULL;
}

static void collect_node(SymbolTable* st, AstNode* n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_PROGRAM:
            for (int i = 0; i < n->program.statements.count; i++) collect_node(st, n->program.statements.items[i]);
            break;
        case NODE_FUNC:
            symbols_add_function(st, n);
            for (int i = 0; i < n->func.param_count; i++) {
                symbols_add(st, n->func.params[i].name, n->func.params[i].type, n->func.params[i].is_ref);
            }
            collect_node(st, n->func.body);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < n->block.statements.count; i++) collect_node(st, n->block.statements.items[i]);
            break;
        case NODE_VAR:
            symbols_add(st, n->var.name, n->var.type, 0);
            break;
        default:
            break;
    }
}

void symbols_collect_program(SymbolTable* st, AstNode* program) {
    collect_node(st, program);
}

