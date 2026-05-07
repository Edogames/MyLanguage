#include "token.h"
#include "../common/util.h"

#include <stdlib.h>

void tokens_init(TokenArray* arr) {
    arr->count = 0;
    arr->cap = 128;
    arr->items = (Token*)malloc(sizeof(Token) * arr->cap);
}

void tokens_push(TokenArray* arr, Token token) {
    if (arr->count >= arr->cap) {
        arr->cap *= 2;
        arr->items = (Token*)realloc(arr->items, sizeof(Token) * arr->cap);
    }
    arr->items[arr->count++] = token;
}

void tokens_free(TokenArray* arr) {
    for (int i = 0; i < arr->count; i++) free(arr->items[i].text);
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->cap = 0;
}

