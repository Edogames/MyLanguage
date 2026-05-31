#include "vm.h"
#include "../common/util.h"
#include "../frontend/ast.h"
#include "../frontend/lexer.h"
#include "../frontend/parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { VAL_NULL, VAL_STRING, VAL_INT, VAL_DOUBLE, VAL_JSON } ValueKind;

typedef struct {
    ValueKind kind;
    char* s;
    int i;
    double d;
} Value;

typedef struct {
    char* name;
    Value value;
} VmVar;

typedef struct {
    VmVar vars[512];
    int count;
} Vm;

static char* vm_strdup(const char* s) {
    return mlg_strdup(s ? s : "");
}

static char* trim_owned(const char* s) {
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)*s)) {
        s++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char* out = (char*)malloc((size_t)len + 1);
    memcpy(out, s, (size_t)len);
    out[len] = '\0';
    return out;
}

static int starts_with(const char* s, const char* pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}

static Value value_string(const char* s) {
    Value v = {VAL_STRING, vm_strdup(s), 0, 0};
    return v;
}

static Value value_json(const char* s) {
    Value v = {VAL_JSON, vm_strdup(s), 0, 0};
    return v;
}

static Value value_int(int i) {
    Value v = {VAL_INT, NULL, i, (double)i};
    return v;
}

static Value value_double(double d) {
    Value v = {VAL_DOUBLE, NULL, (int)d, d};
    return v;
}

static char* value_to_string(Value v) {
    char buf[128];
    if (v.kind == VAL_STRING || v.kind == VAL_JSON) return vm_strdup(v.s);
    if (v.kind == VAL_INT) {
        snprintf(buf, sizeof(buf), "%d", v.i);
        return vm_strdup(buf);
    }
    if (v.kind == VAL_DOUBLE) {
        snprintf(buf, sizeof(buf), "%f", v.d);
        return vm_strdup(buf);
    }
    return vm_strdup("");
}

static VmVar* vm_find(Vm* vm, const char* name) {
    for (int i = vm->count - 1; i >= 0; i--) {
        if (strcmp(vm->vars[i].name, name) == 0) return &vm->vars[i];
    }
    return NULL;
}

static void vm_set(Vm* vm, const char* name, Value value) {
    VmVar* existing = vm_find(vm, name);
    if (existing) {
        existing->value = value;
        return;
    }
    if (vm->count >= 512) return;
    vm->vars[vm->count].name = vm_strdup(name);
    vm->vars[vm->count].value = value;
    vm->count++;
}

static char* unquote(const char* expr) {
    char* t = trim_owned(expr);
    int len = (int)strlen(t);
    if (len >= 2 && ((t[0] == '"' && t[len - 1] == '"') || (t[0] == '\'' && t[len - 1] == '\''))) {
        t[len - 1] = '\0';
        StrBuf sb;
        sb_init(&sb);
        for (char* p = t + 1; *p; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                if (*p == 'n') sb_append_char(&sb, '\n');
                else if (*p == 't') sb_append_char(&sb, '\t');
                else sb_append_char(&sb, *p);
            } else {
                sb_append_char(&sb, *p);
            }
        }
        char* out = vm_strdup(sb.data);
        sb_free(&sb);
        free(t);
        return out;
    }
    return t;
}

static char* json_get_raw(const char* json, const char* key) {
    char pat[256];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    char* p = NULL;
    char* scan = (char*)json;
    while ((scan = strstr(scan, pat))) {
        p = scan;
        scan += strlen(pat);
    }
    if (!p) return vm_strdup("");
    p = strchr(p + strlen(pat), ':');
    if (!p) return vm_strdup("");
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '"') {
        p++;
        char* e = p;
        while (*e && !(*e == '"' && e[-1] != '\\')) e++;
        char save = *e;
        *e = '\0';
        char* out = vm_strdup(p);
        *e = save;
        return out;
    }
    char* e = p;
    while (*e && *e != ',' && *e != '}') e++;
    char* out = (char*)malloc((size_t)(e - p) + 1);
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = '\0';
    char* trimmed = trim_owned(out);
    free(out);
    return trimmed;
}

static void json_set_raw(Value* json, const char* key, const char* value) {
    if (!json || json->kind != VAL_JSON) return;
    char* old = json->s ? json->s : "{}";
    char* end = strrchr(old, '}');
    size_t base = end ? (size_t)(end - old) : strlen(old);
    size_t need = base + strlen(key) + strlen(value) + 16;
    char* next = (char*)malloc(need);
    snprintf(next, base + 1, "%s", old);
    if (base <= 1) strcpy(next, "{");
    else strcat(next, ",");
    strcat(next, "\"");
    strcat(next, key);
    strcat(next, "\":\"");
    strcat(next, value);
    strcat(next, "\"}");
    json->s = next;
}

static Value eval_expr(Vm* vm, const char* expr);

static char* eval_interpolation(Vm* vm, const char* expr) {
    const char* p = strstr(expr, "$\"");
    if (!p) return NULL;
    p += 2;
    StrBuf sb;
    sb_init(&sb);
    while (*p && *p != '"') {
        if (*p == '{') {
            const char* e = strchr(p, '}');
            if (!e) break;
            char raw[512];
            snprintf(raw, sizeof(raw), "%.*s", (int)(e - p - 1), p + 1);
            Value v = eval_expr(vm, raw);
            char* text = value_to_string(v);
            sb_append(&sb, text);
            free(text);
            p = e + 1;
        } else {
            sb_append_char(&sb, *p++);
        }
    }
    char* out = vm_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static Value eval_json_method(Vm* vm, const char* expr) {
    char* dot = strstr(expr, ".get");
    if (!dot) dot = strstr(expr, ".stringify");
    if (!dot) return (Value){VAL_NULL, NULL, 0, 0};
    char obj[128];
    snprintf(obj, sizeof(obj), "%.*s", (int)(dot - expr), expr);
    VmVar* var = vm_find(vm, trim_owned(obj));
    if (!var || var->value.kind != VAL_JSON) return value_string("");
    if (strstr(dot, ".stringify(")) return value_string(var->value.s);
    char* open = strchr(dot, '(');
    char* close = strrchr(dot, ')');
    if (!open || !close) return value_string("");
    char arg[256];
    snprintf(arg, sizeof(arg), "%.*s", (int)(close - open - 1), open + 1);
    char* key = unquote(arg);
    char* raw = json_get_raw(var->value.s, key);
    free(key);
    if (starts_with(dot, ".get_int(")) {
        int i = atoi(raw);
        free(raw);
        return value_int(i);
    }
    if (starts_with(dot, ".get_float(") || starts_with(dot, ".get_double(")) {
        double d = atof(raw);
        free(raw);
        return value_double(d);
    }
    Value out = value_string(raw);
    free(raw);
    return out;
}

static Value eval_expr(Vm* vm, const char* expr) {
    char* t = trim_owned(expr);
    char* interp = eval_interpolation(vm, t);
    if (interp) {
        free(t);
        Value out = value_string(interp);
        free(interp);
        return out;
    }
    if (starts_with(t, "json_parse(")) {
        char* inner = strchr(t, '(');
        char* close = strrchr(t, ')');
        if (inner && close) {
            char raw[4096];
            snprintf(raw, sizeof(raw), "%.*s", (int)(close - inner - 1), inner + 1);
            char* s = unquote(raw);
            free(t);
            Value out = value_json(s);
            free(s);
            return out;
        }
    }
    if (strstr(t, ".get(") || strstr(t, ".get_int(") || strstr(t, ".get_float(") ||
        strstr(t, ".get_double(") || strstr(t, ".stringify(")) {
        Value out = eval_json_method(vm, t);
        free(t);
        return out;
    }
    if (starts_with(t, "parse_int(")) {
        char* inner = strchr(t, '(');
        char* close = strrchr(t, ')');
        char raw[512];
        snprintf(raw, sizeof(raw), "%.*s", (int)(close - inner - 1), inner + 1);
        char* s = unquote(raw);
        int i = atoi(s);
        free(s);
        free(t);
        return value_int(i);
    }
    if (t[0] == '"' || t[0] == '\'') {
        char* s = unquote(t);
        free(t);
        Value out = value_string(s);
        free(s);
        return out;
    }
    VmVar* var = vm_find(vm, t);
    if (var) {
        Value out = var->value;
        free(t);
        return out;
    }
    if (strchr(t, '.')) {
        double d = atof(t);
        free(t);
        return value_double(d);
    }
    int i = atoi(t);
    free(t);
    return value_int(i);
}

static int eval_condition(Vm* vm, const char* expr) {
    char* eq = strstr(expr, "==");
    char* ne = strstr(expr, "!=");
    char* op = eq ? eq : ne;
    if (!op) {
        Value v = eval_expr(vm, expr);
        return v.kind == VAL_INT ? v.i != 0 : v.kind == VAL_DOUBLE ? v.d != 0 : v.s && v.s[0];
    }
    char* left = (char*)malloc((size_t)(op - expr) + 1);
    memcpy(left, expr, (size_t)(op - expr));
    left[op - expr] = '\0';
    char* right = trim_owned(op + 2);
    Value a = eval_expr(vm, left);
    Value b = eval_expr(vm, right);
    char* as = value_to_string(a);
    char* bs = value_to_string(b);
    int same = strcmp(as, bs) == 0;
    free(left);
    free(right);
    free(as);
    free(bs);
    return eq ? same : !same;
}

static void exec_node(Vm* vm, AstNode* node);

static void exec_block(Vm* vm, AstNode* block) {
    for (int i = 0; i < block->block.statements.count; i++) exec_node(vm, block->block.statements.items[i]);
}

static void exec_expr_stmt(Vm* vm, const char* expr) {
    char* dot = strstr(expr, ".set(");
    if (dot) {
        char obj[128];
        snprintf(obj, sizeof(obj), "%.*s", (int)(dot - expr), expr);
        char* name = trim_owned(obj);
        VmVar* var = vm_find(vm, name);
        free(name);
        if (var && var->value.kind == VAL_JSON) {
            char* open = strchr(dot, '(');
            char* comma = strchr(open ? open + 1 : dot, ',');
            char* close = strrchr(dot, ')');
            if (open && comma && close) {
                char key_raw[256], val_raw[512];
                snprintf(key_raw, sizeof(key_raw), "%.*s", (int)(comma - open - 1), open + 1);
                snprintf(val_raw, sizeof(val_raw), "%.*s", (int)(close - comma - 1), comma + 1);
                char* key = unquote(key_raw);
                Value vv = eval_expr(vm, val_raw);
                char* val = value_to_string(vv);
                json_set_raw(&var->value, key, val);
                free(key);
                free(val);
            }
        }
    }
}

static void exec_node(Vm* vm, AstNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.statements.count; i++) exec_node(vm, node->program.statements.items[i]);
            break;
        case NODE_VAR:
            vm_set(vm, node->var.name, eval_expr(vm, node->var.expr));
            break;
        case NODE_EXPR:
            if (starts_with(node->expr_stmt.expr, "print(")) {
                const char* inner = node->expr_stmt.expr + 6;
                int len = (int)strlen(inner);
                while (len > 0 && inner[len - 1] == ')') len--;
                char raw[4096];
                snprintf(raw, sizeof(raw), "%.*s", len, inner);
                Value v = eval_expr(vm, raw);
                char* text = value_to_string(v);
                printf("%s\n", text);
                free(text);
            } else {
                exec_expr_stmt(vm, node->expr_stmt.expr);
            }
            break;
        case NODE_IF:
            if (eval_condition(vm, node->if_stmt.condition)) exec_node(vm, node->if_stmt.then_branch);
            else if (node->if_stmt.else_branch) exec_node(vm, node->if_stmt.else_branch);
            break;
        case NODE_BLOCK:
            exec_block(vm, node);
            break;
        default:
            break;
    }
}

int vm_run_source(const char* source, const char* filename) {
    TokenArray tokens;
    if (!lex_source(source, filename, &tokens)) return 1;
    AstNode* ast = parse_tokens(&tokens, filename);
    tokens_free(&tokens);
    if (!ast) return 1;
    Vm vm = {0};
    exec_node(&vm, ast);
    return 0;
}

int vm_write_bundle(const char* path, const char* source, const char* target) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "MLGB1\nTARGET:%s\n---SOURCE---\n%s", target ? target : "portable", source);
    fclose(f);
    return 1;
}

int vm_run_bundle(const char* path) {
    char* data = mlg_read_file(path);
    if (!data) {
        fprintf(stderr, "Open failed: %s\n", path);
        return 1;
    }
    char* marker = strstr(data, "---SOURCE---\n");
    if (!marker) {
        fprintf(stderr, "%s is not a Mylang portable bundle.\n", path);
        free(data);
        return 1;
    }
    int status = vm_run_source(marker + strlen("---SOURCE---\n"), path);
    free(data);
    return status;
}
