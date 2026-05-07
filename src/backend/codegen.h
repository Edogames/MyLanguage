#ifndef MYLANG_CODEGEN_H
#define MYLANG_CODEGEN_H

#include "../frontend/ast.h"
#include "../middle/symbols.h"

typedef struct {
    StrBuf out;
    SymbolTable* symbols;
    int indent;
    int in_function;
} Codegen;

char* codegen_program(AstNode* program, SymbolTable* symbols);

#endif

