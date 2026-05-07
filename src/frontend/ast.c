#include "ast.h"

#include <stdlib.h>

void nodes_init(NodeArray* arr) {
    arr->count = 0;
    arr->cap = 32;
    arr->items = (AstNode**)malloc(sizeof(AstNode*) * arr->cap);
}

void nodes_push(NodeArray* arr, AstNode* node) {
    if (arr->count >= arr->cap) {
        arr->cap *= 2;
        arr->items = (AstNode**)realloc(arr->items, sizeof(AstNode*) * arr->cap);
    }
    arr->items[arr->count++] = node;
}

AstNode* ast_new(NodeKind kind, int line) {
    AstNode* node = (AstNode*)calloc(1, sizeof(AstNode));
    node->kind = kind;
    node->line = line;
    if (kind == NODE_PROGRAM) nodes_init(&node->program.statements);
    if (kind == NODE_BLOCK) nodes_init(&node->block.statements);
    return node;
}

void ast_free(AstNode* node) {
    if (!node) return;
    if (node->kind == NODE_PROGRAM) {
        for (int i = 0; i < node->program.statements.count; i++) ast_free(node->program.statements.items[i]);
        free(node->program.statements.items);
    } else if (node->kind == NODE_BLOCK) {
        for (int i = 0; i < node->block.statements.count; i++) ast_free(node->block.statements.items[i]);
        free(node->block.statements.items);
    }
    free(node);
}

