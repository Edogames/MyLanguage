#ifndef MYLANG_TOKEN_H
#define MYLANG_TOKEN_H

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_INTERP_STRING,
    TOK_SYMBOL
} TokenKind;

typedef struct {
    TokenKind kind;
    char* text;
    int line;
    int col;
} Token;

typedef struct {
    Token* items;
    int count;
    int cap;
} TokenArray;

void tokens_init(TokenArray* arr);
void tokens_push(TokenArray* arr, Token token);
void tokens_free(TokenArray* arr);

#endif

