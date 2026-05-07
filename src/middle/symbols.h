#ifndef MYLANG_SYMBOLS_H
#define MYLANG_SYMBOLS_H

#include "../frontend/ast.h"

typedef struct {
    char* name;
    char* type;
    int is_ref;
} Symbol;

typedef struct {
    char* name;
    char* return_type;
    Param* params;
    int param_count;
} FunctionInfo;

typedef struct {
    Symbol* symbols;
    int symbol_count;
    int symbol_cap;
    FunctionInfo* funcs;
    int func_count;
    int func_cap;
} SymbolTable;

void symbols_init(SymbolTable* st);
void symbols_free(SymbolTable* st);
void symbols_add(SymbolTable* st, const char* name, const char* type, int is_ref);
const Symbol* symbols_find(SymbolTable* st, const char* name);
void symbols_add_function(SymbolTable* st, AstNode* func);
FunctionInfo* symbols_find_function(SymbolTable* st, const char* name);
void symbols_collect_program(SymbolTable* st, AstNode* program);

#endif

