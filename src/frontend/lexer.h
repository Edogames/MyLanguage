#ifndef MYLANG_LEXER_H
#define MYLANG_LEXER_H

#include "token.h"

int lex_source(const char* source, const char* filename, TokenArray* out);

#endif

