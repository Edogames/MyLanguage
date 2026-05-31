#ifndef MYLANG_AST_H
#define MYLANG_AST_H

typedef enum {
    NODE_PROGRAM,
    NODE_IMPORT,
    NODE_FUNC,
    NODE_BLOCK,
    NODE_VAR,
    NODE_RETURN,
    NODE_EXPR,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR
} NodeKind;

typedef struct AstNode AstNode;

typedef struct {
    char* type;
    char* name;
    int is_ref;
} Param;

typedef struct {
    AstNode** items;
    int count;
    int cap;
} NodeArray;

struct AstNode {
    NodeKind kind;
    int line;
    union {
        struct { NodeArray statements; } program;
        struct { char* module; char** items; int item_count; int wildcard; } import_stmt;
        struct { char* return_type; char* name; Param* params; int param_count; int is_async; AstNode* body; } func;
        struct { NodeArray statements; } block;
        struct { char* type; char* name; char* expr; } var;
        struct { char* expr; } ret;
        struct { char* expr; } expr_stmt;
        struct { char* condition; AstNode* then_branch; AstNode* else_branch; } if_stmt;
        struct { char* condition; AstNode* body; } while_stmt;
        struct { char* header; AstNode* body; } for_stmt;
    };
};

void nodes_init(NodeArray* arr);
void nodes_push(NodeArray* arr, AstNode* node);
AstNode* ast_new(NodeKind kind, int line);
void ast_free(AstNode* node);

#endif
