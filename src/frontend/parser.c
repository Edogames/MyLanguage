#include "parser.h"
#include "../common/util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Token* peek(Parser* p) { return &p->tokens->items[p->pos]; }
static int is_eof(Parser* p) { return peek(p)->kind == TOK_EOF; }
static int is_text(Parser* p, const char* text) { return strcmp(peek(p)->text, text) == 0; }
static int match(Parser* p, const char* text) { if (is_text(p, text)) { p->pos++; return 1; } return 0; }

static void parser_error(Parser* p, const char* msg) {
    fprintf(stderr, "%s:%d:%d: parse error: %s near '%s'\n",
            p->filename, peek(p)->line, peek(p)->col, msg, peek(p)->text);
    p->had_error = 1;
}

static int is_type_token(Token* t) {
    return strcmp(t->text, "void") == 0 || strcmp(t->text, "int") == 0 ||
           strcmp(t->text, "string") == 0 || strcmp(t->text, "float") == 0 ||
           strcmp(t->text, "double") == 0 || strcmp(t->text, "bool") == 0 ||
           strcmp(t->text, "json") == 0 || strcmp(t->text, "file") == 0 ||
           strcmp(t->text, "task") == 0;
}

static int token_needs_space(const char* a, const char* b) {
    if (!a || !b || !a[0] || !b[0]) return 0;
    char ac = a[strlen(a) - 1];
    char bc = b[0];
    if ((isalnum((unsigned char)ac) || ac == '_' || ac == '"' || ac == '\'') &&
        (isalnum((unsigned char)bc) || bc == '_' || bc == '"' || bc == '\'' || bc == '$')) return 1;
    return 0;
}

static char* capture_until(Parser* p, const char* end) {
    StrBuf sb;
    sb_init(&sb);
    const char* last = NULL;
    int paren = 0, bracket = 0, brace = 0;
    while (!is_eof(p)) {
        const char* t = peek(p)->text;
        if (paren == 0 && bracket == 0 && brace == 0 && strcmp(t, end) == 0) break;
        if (strcmp(t, "(") == 0) paren++;
        else if (strcmp(t, ")") == 0 && paren > 0) paren--;
        else if (strcmp(t, "[") == 0) bracket++;
        else if (strcmp(t, "]") == 0 && bracket > 0) bracket--;
        else if (strcmp(t, "{") == 0) brace++;
        else if (strcmp(t, "}") == 0 && brace > 0) brace--;
        if (token_needs_space(last, t)) sb_append_char(&sb, ' ');
        sb_append(&sb, t);
        last = t;
        p->pos++;
    }
    char* out = mlg_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static char* capture_parens(Parser* p) {
    if (!match(p, "(")) {
        parser_error(p, "expected '('");
        return mlg_strdup("");
    }
    StrBuf sb;
    sb_init(&sb);
    const char* last = NULL;
    int depth = 1;
    while (!is_eof(p) && depth > 0) {
        const char* t = peek(p)->text;
        if (strcmp(t, "(") == 0) depth++;
        if (strcmp(t, ")") == 0) {
            depth--;
            if (depth == 0) {
                p->pos++;
                break;
            }
        }
        if (token_needs_space(last, t)) sb_append_char(&sb, ' ');
        sb_append(&sb, t);
        last = t;
        p->pos++;
    }
    char* out = mlg_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static AstNode* parse_statement(Parser* p);

static AstNode* parse_block(Parser* p) {
    AstNode* block = ast_new(NODE_BLOCK, peek(p)->line);
    if (!match(p, "{")) {
        parser_error(p, "expected block");
        return block;
    }
    while (!is_eof(p) && !match(p, "}")) {
        nodes_push(&block->block.statements, parse_statement(p));
    }
    return block;
}

static AstNode* parse_import(Parser* p) {
    AstNode* n = ast_new(NODE_IMPORT, peek(p)->line);
    match(p, "from");
    n->import_stmt.module = mlg_strdup(peek(p)->text);
    p->pos++;
    if (!match(p, "import")) parser_error(p, "expected import");
    if (match(p, "*")) {
        n->import_stmt.wildcard = 1;
    } else {
        n->import_stmt.items = NULL;
        n->import_stmt.item_count = 0;
        while (!is_eof(p) && !is_text(p, ";") && !is_text(p, "{") && !is_type_token(peek(p))) {
            if (strcmp(peek(p)->text, ",") != 0) {
                n->import_stmt.items = (char**)realloc(n->import_stmt.items, sizeof(char*) * (n->import_stmt.item_count + 1));
                n->import_stmt.items[n->import_stmt.item_count++] = mlg_strdup(peek(p)->text);
            }
            p->pos++;
            if (is_text(p, "\n")) break;
        }
    }
    match(p, ";");
    return n;
}

static AstNode* parse_function(Parser* p, int is_async) {
    AstNode* n = ast_new(NODE_FUNC, peek(p)->line);
    n->func.is_async = is_async;
    n->func.return_type = mlg_strdup(peek(p)->text);
    p->pos++;
    n->func.name = mlg_strdup(peek(p)->text);
    p->pos++;
    match(p, "(");
    while (!is_eof(p) && !match(p, ")")) {
        Param param = {0};
        if (match(p, "ref")) param.is_ref = 1;
        param.type = mlg_strdup(peek(p)->text);
        p->pos++;
        param.name = mlg_strdup(peek(p)->text);
        p->pos++;
        n->func.params = (Param*)realloc(n->func.params, sizeof(Param) * (n->func.param_count + 1));
        n->func.params[n->func.param_count++] = param;
        match(p, ",");
    }
    n->func.body = parse_block(p);
    return n;
}

static AstNode* parse_var(Parser* p) {
    AstNode* n = ast_new(NODE_VAR, peek(p)->line);
    n->var.type = mlg_strdup(peek(p)->text);
    p->pos++;
    n->var.name = mlg_strdup(peek(p)->text);
    p->pos++;
    if (match(p, "=")) n->var.expr = capture_until(p, ";");
    else n->var.expr = mlg_strdup("");
    if (!match(p, ";")) parser_error(p, "expected ';'");
    return n;
}

static AstNode* parse_statement(Parser* p) {
    if (is_text(p, "async")) {
        p->pos++;
        if (!is_type_token(peek(p))) {
            p->pos--; // backtrack "async"
            AstNode* n = ast_new(NODE_EXPR, peek(p)->line);
            n->expr_stmt.expr = capture_until(p, ";");
            if (!match(p, ";")) parser_error(p, "expected ';'");
            return n;
        }
        return parse_function(p, 1);
    }
    if (is_text(p, "from")) return parse_import(p);
    if (is_text(p, "{")) return parse_block(p);
    if (is_text(p, "if")) {
        AstNode* n = ast_new(NODE_IF, peek(p)->line);
        p->pos++;
        n->if_stmt.condition = capture_parens(p);
        n->if_stmt.then_branch = parse_block(p);
        if (match(p, "else")) {
            if (is_text(p, "if")) n->if_stmt.else_branch = parse_statement(p);
            else n->if_stmt.else_branch = parse_block(p);
        }
        return n;
    }
    if (is_text(p, "while")) {
        AstNode* n = ast_new(NODE_WHILE, peek(p)->line);
        p->pos++;
        n->while_stmt.condition = capture_parens(p);
        n->while_stmt.body = parse_block(p);
        return n;
    }
    if (is_text(p, "for")) {
        AstNode* n = ast_new(NODE_FOR, peek(p)->line);
        p->pos++;
        n->for_stmt.header = capture_parens(p);
        n->for_stmt.body = parse_block(p);
        return n;
    }
    if (is_text(p, "return")) {
        AstNode* n = ast_new(NODE_RETURN, peek(p)->line);
        p->pos++;
        n->ret.expr = capture_until(p, ";");
        if (!match(p, ";")) parser_error(p, "expected ';'");
        return n;
    }
    if (is_type_token(peek(p))) {
        Token* t0 = peek(p);
        Token* t2 = &p->tokens->items[p->pos + 2];
        
        if (t0->kind == TOK_IDENT) {
            if (strcmp(t2->text, "(") == 0) return parse_function(p, 0);
            return parse_var(p);
        }
        if (strcmp(t2->text, "(") == 0) return parse_function(p, 0);
        (void)t0;
        return parse_var(p);
    }
    AstNode* n = ast_new(NODE_EXPR, peek(p)->line);
    n->expr_stmt.expr = capture_until(p, ";");
    if (!match(p, ";")) parser_error(p, "expected ';'");
    return n;
}

AstNode* parse_tokens(TokenArray* tokens, const char* filename) {
    Parser p = {tokens, 0, filename, 0};
    AstNode* program = ast_new(NODE_PROGRAM, 1);
    while (!is_eof(&p)) {
        nodes_push(&program->program.statements, parse_statement(&p));
    }
    if (p.had_error) {
        ast_free(program);
        return NULL;
    }
    return program;
}
