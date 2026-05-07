#ifndef MYLANG_PARSER_H
#define MYLANG_PARSER_H

#include "ast.h"
#include "token.h"

typedef struct {
    TokenArray* tokens;
    int pos;
    const char* filename;
    int had_error;
} Parser;

AstNode* parse_tokens(TokenArray* tokens, const char* filename);

#endif

