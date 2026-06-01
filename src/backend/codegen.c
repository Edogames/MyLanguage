#include "codegen.h"
#include "../common/util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_indent(Codegen* cg) {
    for (int i = 0; i < cg->indent; i++) sb_append(&cg->out, "    ");
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int inside_string_at(const char* s, int pos) {
    int in = 0;
    for (int i = 0; i < pos && s[i]; i++) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\')) in = !in;
    }
    return in;
}

static void replace_single_quotes(char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\'') s[i] = '"';
    }
}

static const char* c_type_name(const char* type) {
    if (strcmp(type, "string") == 0) return "string";
    if (strcmp(type, "json") == 0)   return "json";
    if (strcmp(type, "file") == 0)   return "file";
    return type;
}

static char* trim_copy(const char* s, int len) {
    while (len > 0 && isspace((unsigned char)*s)) { s++; len--; }
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char* out = (char*)malloc((size_t)len + 1);
    memcpy(out, s, (size_t)len);
    out[len] = '\0';
    return out;
}

static int expr_is_string_like(Codegen* cg, const char* expr) {
    char* t = trim_copy(expr, (int)strlen(expr));
    int result = 0;
    if (t[0] == '"' || strncmp(t, "mlg_format(", 11) == 0 || strncmp(t, "input(", 6) == 0 ||
        strncmp(t, "string_", 7) == 0 || strncmp(t, "to_string(", 10) == 0 ||
        strstr(t, "json_get(") || strstr(t, "file_get(")) {
        result = 1;
    } else {
        const Symbol* s = symbols_find(cg->symbols, t);
        result = s && strcmp(s->type, "string") == 0;
    }
    free(t);
    return result;
}

static char* replace_word_outside_strings(const char* expr, const char* name, const char* repl) {
    StrBuf sb;
    sb_init(&sb);
    int n = (int)strlen(name);
    for (int i = 0; expr[i]; ) {
        if (!inside_string_at(expr, i) && strncmp(expr + i, name, (size_t)n) == 0 &&
            (i == 0 || !is_ident_char(expr[i - 1])) && !is_ident_char(expr[i + n])) {
            sb_append(&sb, repl);
            i += n;
        } else {
            sb_append_char(&sb, expr[i++]);
        }
    }
    char* out = mlg_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static char* capture_call_arg(char* arg_start, char** after_out) {
    int depth = 1;
    char* p = arg_start;
    while (*p && depth > 0) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        if (depth > 0) p++;
    }
    char* arg = trim_copy(arg_start, (int)(p - arg_start));
    *after_out = *p ? p + 1 : p;
    return arg;
}

static char* transform_simple_method(char* work, const char* method, const char* fn_name) {
    for (;;) {
        char* dot = strstr(work, method);
        if (!dot || inside_string_at(work, (int)(dot - work))) break;
        char* start = dot - 1;
        while (start >= work && is_ident_char(*start)) start--;
        start++;
        char* after = NULL;
        char* arg = capture_call_arg(dot + strlen(method), &after);
        replace_single_quotes(arg);
        char var[128] = {0};
        snprintf(var, sizeof(var), "%.*s", (int)(dot - start), start);
        StrBuf sb;
        sb_init(&sb);
        if (arg[0]) sb_appendf(&sb, "%.*s%s(%s, %s)%s", (int)(start - work), work, fn_name, var, arg, after);
        else        sb_appendf(&sb, "%.*s%s(%s)%s",     (int)(start - work), work, fn_name, var, after);
        free(arg);
        free(work);
        work = mlg_strdup(sb.data);
        sb_free(&sb);
    }
    return work;
}

static char* transform_file_index_reads(Codegen* cg, char* work) {
    for (int si = 0; si < cg->symbols->symbol_count; si++) {
        Symbol* sym = &cg->symbols->symbols[si];
        if (strcmp(sym->type, "file") != 0) continue;
        for (;;) {
            char pattern[96];
            snprintf(pattern, sizeof(pattern), "%s[", sym->name);
            char* pos = strstr(work, pattern);
            if (!pos || inside_string_at(work, (int)(pos - work))) break;
            char* idx_start = pos + strlen(pattern);
            int depth = 1;
            char* p = idx_start;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                if (depth > 0) p++;
            }
            char* idx = trim_copy(idx_start, (int)(p - idx_start));
            StrBuf sb;
            sb_init(&sb);
            sb_appendf(&sb, "%.*sfile_get(%s, %s)%s",
                       (int)(pos - work), work, sym->name, idx, *p ? p + 1 : p);
            free(idx);
            free(work);
            work = mlg_strdup(sb.data);
            sb_free(&sb);
        }
    }
    return work;
}

static char* transform_methods(Codegen* cg, const char* expr) {
    char* work = mlg_strdup(expr);

    /* .len */
    for (;;) {
        char* dot = strstr(work, ".len");
        if (!dot || inside_string_at(work, (int)(dot - work))) break;
        char* start = dot - 1;
        while (start >= work && is_ident_char(*start)) start--;
        start++;
        char var[128] = {0};
        snprintf(var, sizeof(var), "%.*s", (int)(dot - start), start);
        StrBuf sb;
        sb_init(&sb);
        sb_appendf(&sb, "%.*sstrlen(%s)%s", (int)(start - work), work, var, dot + 4);
        free(work);
        work = mlg_strdup(sb.data);
        sb_free(&sb);
    }

    /* .has() */
    for (;;) {
        char* dot = strstr(work, ".has(");
        if (!dot || inside_string_at(work, (int)(dot - work))) break;
        char* start = dot - 1;
        while (start >= work && is_ident_char(*start)) start--;
        start++;
        char* arg_start = dot + 5;
        int depth = 1;
        char* p = arg_start;
        while (*p && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            if (depth > 0) p++;
        }
        char var[128] = {0}, arg[512] = {0};
        snprintf(var, sizeof(var), "%.*s", (int)(dot - start), start);
        snprintf(arg, sizeof(arg), "%.*s", (int)(p - arg_start), arg_start);
        replace_single_quotes(arg);
        StrBuf sb;
        sb_init(&sb);
        sb_appendf(&sb, "%.*sstring_has(%s, %s)%s",
                   (int)(start - work), work, var, arg, *p ? p + 1 : p);
        free(work);
        work = mlg_strdup(sb.data);
        sb_free(&sb);
    }

    work = transform_simple_method(work, ".get(",         "json_get");
    work = transform_simple_method(work, ".get_int(",     "json_get_int");
    work = transform_simple_method(work, ".get_float(",   "json_get_float");
    work = transform_simple_method(work, ".get_double(",  "json_get_double");
    work = transform_simple_method(work, ".stringify(",   "json_stringify");
    work = transform_simple_method(work, ".line(",        "file_get");
    work = transform_file_index_reads(cg, work);
    return work;
}

static char* transform_input_empty(const char* expr) {
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; expr[i]; i++) {
        if (!inside_string_at(expr, i) && strncmp(expr + i, "input()", 7) == 0) {
            sb_append(&sb, "input(\"\")");
            i += 6;
        } else {
            sb_append_char(&sb, expr[i]);
        }
    }
    char* out = mlg_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static char* transform_file_constructor(const char* expr) {
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; expr[i]; i++) {
        if (!inside_string_at(expr, i) && strncmp(expr + i, "file(", 5) == 0 &&
            (i == 0 || !is_ident_char(expr[i - 1]))) {
            sb_append(&sb, "mlg_file_open(");
            i += 4;
        } else {
            sb_append_char(&sb, expr[i]);
        }
    }
    char* out = mlg_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static char* transform_string_comparison(Codegen* cg, const char* expr) {
    int depth = 0;
    for (int i = 0; expr[i]; i++) {
        if (inside_string_at(expr, i)) continue;
        if (expr[i] == '(' || expr[i] == '[') depth++;
        else if ((expr[i] == ')' || expr[i] == ']') && depth > 0) depth--;
        if (depth == 0 && (strncmp(expr + i, "==", 2) == 0 || strncmp(expr + i, "!=", 2) == 0)) {
            char* left  = trim_copy(expr, i);
            char* right = trim_copy(expr + i + 2, (int)strlen(expr + i + 2));
            int is_ne   = expr[i] == '!';
            if (expr_is_string_like(cg, left) || expr_is_string_like(cg, right)) {
                StrBuf sb;
                sb_init(&sb);
                sb_appendf(&sb, "(strcmp(%s, %s) %s 0)", left, right, is_ne ? "!=" : "==");
                free(left); free(right);
                char* out = mlg_strdup(sb.data);
                sb_free(&sb);
                return out;
            }
            free(left); free(right);
            break;
        }
    }
    return mlg_strdup(expr);
}

static char* transform_ref_expr(Codegen* cg, const char* expr) {
    char* out = mlg_strdup(expr);
    if (!cg->in_function) return out;
    for (int i = 0; i < cg->symbols->symbol_count; i++) {
        Symbol* s = &cg->symbols->symbols[i];
        if (!s->is_ref) continue;
        char repl[128];
        snprintf(repl, sizeof(repl), "*%s", s->name);
        char* next = replace_word_outside_strings(out, s->name, repl);
        free(out);
        out = next;
    }
    return out;
}

static char* transform_expr(Codegen* cg, const char* expr);

static const char* format_for_type(const char* type) {
    if (!type) return "%s";
    if (strcmp(type, "int") == 0 || strcmp(type, "bool") == 0) return "%d";
    if (strcmp(type, "float") == 0 || strcmp(type, "double") == 0) return "%f";
    return "%s";
}

static const char* function_call_fmt_for(Codegen* cg, const char* raw) {
    char* open = strchr(raw, '(');
    if (!open) return NULL;
    char* name = trim_copy(raw, (int)(open - raw));
    FunctionInfo* fn = symbols_find_function(cg->symbols, name);
    free(name);
    return fn ? format_for_type(fn->return_type) : NULL;
}

static const char* interpolation_fmt_for(Codegen* cg, const char* raw) {
    char* method = transform_methods(cg, raw);
    const Symbol* s = symbols_find(cg->symbols, raw);
    const char* fmt = "%s";
    if (strstr(method, "strlen("))           fmt = "%lu";
    else if (strstr(method, "string_has("))  fmt = "%d";
    else if (strstr(raw, ".get_int("))       fmt = "%d";
    else if (strstr(raw, ".get_float(") || strstr(raw, ".get_double(")) fmt = "%f";
    else if (s) fmt = format_for_type(s->type);
    else {
        const char* call_fmt = function_call_fmt_for(cg, raw);
        if (call_fmt) fmt = call_fmt;
    }
    free(method);
    return fmt;
}

static char* transform_interpolation(Codegen* cg, const char* expr) {
    const char* start = strstr(expr, "$\"");
    if (!start) return transform_methods(cg, expr);
    StrBuf out;
    sb_init(&out);
    sb_appendf(&out, "%.*smlg_format(\"", (int)(start - expr), expr);
    const char* p = start + 2;
    StrBuf args;
    sb_init(&args);
    while (*p && *p != '"') {
        if (*p == '{') {
            const char* e = strchr(p, '}');
            if (!e) break;
            char raw[512] = {0};
            snprintf(raw, sizeof(raw), "%.*s", (int)(e - p - 1), p + 1);
            char* transformed = transform_methods(cg, raw);
            const Symbol* raw_sym = symbols_find(cg->symbols, raw);
            if (raw_sym && strcmp(raw_sym->type, "json") == 0) {
                free(transformed);
                StrBuf tmp; sb_init(&tmp);
                sb_appendf(&tmp, "json_stringify(%s)", raw);
                transformed = mlg_strdup(tmp.data);
                sb_free(&tmp);
            } else if (raw_sym && strcmp(raw_sym->type, "file") == 0) {
                free(transformed);
                transformed = mlg_strdup("\"<file>\"");
            }
            sb_append(&out, interpolation_fmt_for(cg, raw));
            if (args.len > 0) sb_append(&args, ", ");
            sb_append(&args, transformed);
            free(transformed);
            p = e + 1;
        } else {
            if (*p == '"' || *p == '\\') sb_append_char(&out, '\\');
            sb_append_char(&out, *p++);
        }
    }
    sb_append_char(&out, '"');
    if (args.len > 0) { sb_append(&out, ", "); sb_append(&out, args.data); }
    sb_append_char(&out, ')');
    if (*p == '"') p++;
    sb_append(&out, p);
    sb_free(&args);
    char* result = mlg_strdup(out.data);
    sb_free(&out);
    return result;
}

static char* transform_call_refs(Codegen* cg, const char* expr) {
    char* out = mlg_strdup(expr);
    for (int f = 0; f < cg->symbols->func_count; f++) {
        FunctionInfo* fn = &cg->symbols->funcs[f];
        char* call = strstr(out, fn->name);
        if (!call || call[strlen(fn->name)] != '(') continue;
        char* p = call + strlen(fn->name) + 1;
        for (int a = 0; a < fn->param_count; a++) {
            while (isspace((unsigned char)*p)) p++;
            if (fn->params[a].is_ref && *p != '&') {
                memmove(p + 1, p, strlen(p) + 1);
                *p = '&';
                p++;
            }
            int depth = 0;
            while (*p) {
                if (*p == '(') depth++;
                else if (*p == ')' && depth == 0) break;
                else if (*p == ')' && depth > 0) depth--;
                else if (*p == ',' && depth == 0) break;
                p++;
            }
            if (*p == ',') p++;
        }
    }
    return out;
}

static char* transform_async_await(Codegen* cg, const char* expr) {
    (void)cg;
    StrBuf sb;
    sb_init(&sb);
    int len = (int)strlen(expr);
    for (int i = 0; i < len; ) {
        if (inside_string_at(expr, i)) { sb_append_char(&sb, expr[i++]); continue; }
        if (strncmp(expr + i, "await ", 6) == 0 && (i == 0 || !is_ident_char(expr[i - 1]))) {
            sb_append(&sb, "mlg_await(");
            i += 6;
            while (i < len && isspace((unsigned char)expr[i])) i++;
            int s = i;
            while (i < len && is_ident_char(expr[i])) i++;
            sb_appendf(&sb, "%.*s", i - s, expr + s);
            sb_append(&sb, ")");
            continue;
        }
        if (strncmp(expr + i, "async ", 6) == 0 && (i == 0 || !is_ident_char(expr[i - 1]))) {
            sb_append(&sb, "mlg_async_");
            i += 6;
            continue;
        }
        sb_append_char(&sb, expr[i++]);
    }
    char* out = mlg_strdup(sb.data);
    sb_free(&sb);
    return out;
}

static char* transform_expr(Codegen* cg, const char* expr) {
    char* s1 = transform_input_empty(expr);
    char* s2 = transform_file_constructor(s1);
    char* s3 = transform_interpolation(cg, s2);
    char* s4 = transform_methods(cg, s3);
    char* s5 = transform_string_comparison(cg, s4);
    char* s6 = transform_call_refs(cg, s5);
    char* s7 = transform_ref_expr(cg, s6);
    char* s8 = transform_async_await(cg, s7);
    free(s1); free(s2); free(s3); free(s4); free(s5); free(s6); free(s7);
    return s8;
}

static const char* printf_fmt_for(Codegen* cg, const char* expr) {
    const Symbol* s = symbols_find(cg->symbols, expr);
    if (strstr(expr, "strlen(") || strstr(expr, ".len")) return "%lu";
    if (s) return format_for_type(s->type);
    const char* call_fmt = function_call_fmt_for(cg, expr);
    if (call_fmt) return call_fmt;
    return "%s";
}

static void emit_node(Codegen* cg, AstNode* n);

static void emit_block(Codegen* cg, AstNode* block) {
    sb_append(&cg->out, "{\n");
    cg->indent++;
    for (int i = 0; i < block->block.statements.count; i++)
        emit_node(cg, block->block.statements.items[i]);
    cg->indent--;
    emit_indent(cg);
    sb_append(&cg->out, "}\n");
}

static int is_function_node(AstNode* n) { return n && n->kind == NODE_FUNC; }

static int emit_file_index_assignment(Codegen* cg, const char* expr) {
    for (int si = 0; si < cg->symbols->symbol_count; si++) {
        Symbol* sym = &cg->symbols->symbols[si];
        if (strcmp(sym->type, "file") != 0) continue;
        char pattern[96];
        snprintf(pattern, sizeof(pattern), "%s[", sym->name);
        char* pos = strstr(expr, pattern);
        if (!pos || pos != expr) continue;
        char* idx_start = (char*)expr + strlen(pattern);
        char* close = strchr(idx_start, ']');
        char* eq    = close ? strchr(close, '=') : NULL;
        if (!close || !eq) continue;
        char* idx       = trim_copy(idx_start, (int)(close - idx_start));
        char* value_raw = trim_copy(eq + 1, (int)strlen(eq + 1));
        char* value     = transform_expr(cg, value_raw);
        emit_indent(cg);
        sb_appendf(&cg->out, "file_set(%s, %s, %s);\n", sym->name, idx, value);
        free(idx); free(value_raw); free(value);
        return 1;
    }
    return 0;
}

static int emit_builtin_method_statement(Codegen* cg, const char* expr) {
    const char* methods[] = {".replace(", ".set(", ".append(", ".save(", ".delete("};
    const char* funcs[]   = {"string_replace", "json_set", "file_append", "file_save", "file_delete"};
    for (int mi = 0; mi < 5; mi++) {
        char* dot = strstr(expr, methods[mi]);
        if (!dot || inside_string_at(expr, (int)(dot - expr))) continue;
        char* start = dot - 1;
        while (start >= expr && is_ident_char(*start)) start--;
        start++;
        char var[128] = {0};
        snprintf(var, sizeof(var), "%.*s", (int)(dot - start), start);
        char* after = NULL;
        char* args  = capture_call_arg(dot + strlen(methods[mi]), &after);
        (void)after;
        emit_indent(cg);
        if (strcmp(methods[mi], ".replace(") == 0) {
            sb_appendf(&cg->out, "%s = string_replace(%s, %s);\n", var, var, args);
        } else if (strcmp(methods[mi], ".save(") == 0) {
            sb_appendf(&cg->out, "file_save(%s);\n", var);
        } else if (strcmp(methods[mi], ".delete(") == 0) {
            sb_appendf(&cg->out, "file_delete(%s, %s);\n", var, args[0] ? args : "false");
        } else if (strcmp(methods[mi], ".set(") == 0) {
            const Symbol* s = symbols_find(cg->symbols, var);
            sb_appendf(&cg->out, "%s(%s, %s);\n",
                       s && strcmp(s->type, "file") == 0 ? "file_set" : "json_set", var, args);
        } else {
            sb_appendf(&cg->out, "%s(%s%s%s);\n",
                       funcs[mi], var, args[0] ? ", " : "", args);
        }
        free(args);
        return 1;
    }
    return 0;
}

static void emit_expr_statement(Codegen* cg, const char* expr) {
    if (emit_file_index_assignment(cg, expr)) return;
    if (emit_builtin_method_statement(cg, expr)) return;
    char* transformed = transform_expr(cg, expr);
    char* dot = strstr(transformed, ".replace(");
    if (dot) {
        char* start = dot - 1;
        while (start >= transformed && is_ident_char(*start)) start--;
        start++;
        char* arg_start = dot + 9;
        char* arg_end   = strrchr(arg_start, ')');
        char var[128] = {0}, args[512] = {0};
        snprintf(var,  sizeof(var),  "%.*s", (int)(dot - start), start);
        if (arg_end) snprintf(args, sizeof(args), "%.*s", (int)(arg_end - arg_start), arg_start);
        emit_indent(cg);
        sb_appendf(&cg->out, "%s = string_replace(%s, %s);\n", var, var, args);
    } else {
        emit_indent(cg);
        sb_appendf(&cg->out, "%s;\n", transformed);
    }
    free(transformed);
}

static void emit_print(Codegen* cg, const char* expr) {
    const char* inner = expr + 6;
    int len = (int)strlen(inner);
    if (len > 0 && inner[len - 1] == ')') len--;
    char raw[2048] = {0};
    snprintf(raw, sizeof(raw), "%.*s", len, inner);
    char* transformed = transform_expr(cg, raw);
    const Symbol* s = symbols_find(cg->symbols, raw);
    if (s && strcmp(s->type, "json") == 0) {
        free(transformed);
        StrBuf tmp; sb_init(&tmp);
        sb_appendf(&tmp, "json_stringify(%s)", raw);
        transformed = mlg_strdup(tmp.data);
        sb_free(&tmp);
    } else if (s && strcmp(s->type, "file") == 0) {
        free(transformed);
        transformed = mlg_strdup("\"<file>\"");
    }
    emit_indent(cg);
    sb_appendf(&cg->out, "printf(\"%s\\n\", %s);\n",
               strstr(raw, "$\"") ? "%s" : printf_fmt_for(cg, raw), transformed);
    free(transformed);
}

static void emit_node(Codegen* cg, AstNode* n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_FUNC: {
            /* forward declaration */
            emit_indent(cg);
            sb_appendf(&cg->out, "%s %s(", c_type_name(n->func.return_type), n->func.name);
            for (int i = 0; i < n->func.param_count; i++) {
                if (i) sb_append(&cg->out, ", ");
                sb_appendf(&cg->out, "%s%s %s",
                           c_type_name(n->func.params[i].type),
                           n->func.params[i].is_ref ? "*" : "",
                           n->func.params[i].name);
            }
            sb_append(&cg->out, ");\n\n");

            /* args struct */
            emit_indent(cg);
            sb_appendf(&cg->out, "typedef struct {\n");
            for (int i = 0; i < n->func.param_count; i++)
                sb_appendf(&cg->out, "    %s %s;\n",
                           c_type_name(n->func.params[i].type), n->func.params[i].name);
            sb_appendf(&cg->out, "    task task_obj;\n");
            sb_appendf(&cg->out, "} mlg_args_%s;\n\n", n->func.name);

            /* thread wrapper */
            emit_indent(cg);
            sb_appendf(&cg->out, "void* mlg_thread_%s(void* raw_arg) {\n", n->func.name);
            sb_appendf(&cg->out, "    mlg_args_%s* args = (mlg_args_%s*)raw_arg;\n",
                       n->func.name, n->func.name);
            if (strcmp(n->func.return_type, "void") == 0) {
                sb_appendf(&cg->out, "    %s(", n->func.name);
                for (int i = 0; i < n->func.param_count; i++) {
                    if (i) sb_append(&cg->out, ", ");
                    sb_appendf(&cg->out, "args->%s", n->func.params[i].name);
                }
                sb_appendf(&cg->out, ");\n");
                sb_appendf(&cg->out, "    args->task_obj->result = NULL;\n");
            } else {
                sb_appendf(&cg->out, "    %s res = %s(",
                           c_type_name(n->func.return_type), n->func.name);
                for (int i = 0; i < n->func.param_count; i++) {
                    if (i) sb_append(&cg->out, ", ");
                    sb_appendf(&cg->out, "args->%s", n->func.params[i].name);
                }
                sb_appendf(&cg->out, ");\n");
                if (strcmp(n->func.return_type, "int") == 0 ||
                    strcmp(n->func.return_type, "bool") == 0) {
                    sb_appendf(&cg->out,
                               "    args->task_obj->result = (void*)(intptr_t)res;\n");
                } else if (strcmp(n->func.return_type, "float") == 0 ||
                           strcmp(n->func.return_type, "double") == 0) {
                    sb_appendf(&cg->out,
                               "    double* d = malloc(sizeof(double)); *d = res;\n"
                               "    args->task_obj->result = (void*)d;\n");
                } else {
                    sb_appendf(&cg->out,
                               "    args->task_obj->result = (void*)res;\n");
                }
            }
            sb_appendf(&cg->out,
                       "    args->task_obj->completed = true;\n"
                       "    free(args);\n"
                       "    return NULL;\n"
                       "}\n\n");

            /* async starter */
            emit_indent(cg);
            sb_appendf(&cg->out, "task mlg_async_%s(", n->func.name);
            for (int i = 0; i < n->func.param_count; i++) {
                if (i) sb_append(&cg->out, ", ");
                sb_appendf(&cg->out, "%s %s",
                           c_type_name(n->func.params[i].type), n->func.params[i].name);
            }
            sb_appendf(&cg->out, ") {\n");
            sb_appendf(&cg->out,
                       "    task t = mlg_task_create();\n"
                       "    mlg_args_%s* args = malloc(sizeof(mlg_args_%s));\n",
                       n->func.name, n->func.name);
            for (int i = 0; i < n->func.param_count; i++)
                sb_appendf(&cg->out, "    args->%s = %s;\n",
                           n->func.params[i].name, n->func.params[i].name);
            sb_appendf(&cg->out,
                       "    args->task_obj = t;\n"
                       "    mlg_thread_start(t, mlg_thread_%s, args);\n"
                       "    return t;\n"
                       "}\n\n", n->func.name);

            /* actual function */
            emit_indent(cg);
            sb_appendf(&cg->out, "%s %s(", c_type_name(n->func.return_type), n->func.name);
            for (int i = 0; i < n->func.param_count; i++) {
                if (i) sb_append(&cg->out, ", ");
                sb_appendf(&cg->out, "%s%s %s",
                           c_type_name(n->func.params[i].type),
                           n->func.params[i].is_ref ? "*" : "",
                           n->func.params[i].name);
            }
            sb_append(&cg->out, ") ");
            int prev = cg->in_function;
            cg->in_function = 1;
            emit_block(cg, n->func.body);
            cg->in_function = prev;
            break;
        }
        case NODE_VAR: {
            symbols_add(cg->symbols, n->var.name, n->var.type, 0);
            char* expr = transform_expr(cg, n->var.expr);
            emit_indent(cg);
            sb_appendf(&cg->out, "%s %s", c_type_name(n->var.type), n->var.name);
            if (expr[0]) {
                if (strstr(expr, "mlg_await(") &&
                    (strcmp(n->var.type, "int") == 0 || strcmp(n->var.type, "bool") == 0)) {
                    char* casted = replace_word_outside_strings(
                        expr, "mlg_await", "(int)(intptr_t)mlg_await");
                    sb_appendf(&cg->out, " = %s", casted);
                    free(casted);
                } else if (strstr(expr, "mlg_await(") &&
                           (strcmp(n->var.type, "float") == 0 ||
                            strcmp(n->var.type, "double") == 0)) {
                    char* casted = replace_word_outside_strings(
                        expr, "mlg_await", "*(double*)mlg_await");
                    sb_appendf(&cg->out, " = %s", casted);
                    free(casted);
                } else {
                    sb_appendf(&cg->out, " = %s", expr);
                }
            }
            sb_append(&cg->out, ";\n");
            free(expr);
            break;
        }
        case NODE_RETURN: {
            char* expr = transform_expr(cg, n->ret.expr);
            emit_indent(cg);
            sb_appendf(&cg->out, "return %s;\n", expr);
            free(expr);
            break;
        }
        case NODE_EXPR:
            if (strncmp(n->expr_stmt.expr, "print(", 6) == 0)
                emit_print(cg, n->expr_stmt.expr);
            else
                emit_expr_statement(cg, n->expr_stmt.expr);
            break;
        case NODE_IF: {
            char* cond = transform_expr(cg, n->if_stmt.condition);
            emit_indent(cg);
            sb_appendf(&cg->out, "if (%s) ", cond);
            free(cond);
            emit_block(cg, n->if_stmt.then_branch);
            if (n->if_stmt.else_branch) {
                emit_indent(cg);
                sb_append(&cg->out, "else ");
                if (n->if_stmt.else_branch->kind == NODE_IF)
                    emit_node(cg, n->if_stmt.else_branch);
                else
                    emit_block(cg, n->if_stmt.else_branch);
            }
            break;
        }
        case NODE_WHILE: {
            char* cond = transform_expr(cg, n->while_stmt.condition);
            emit_indent(cg);
            sb_appendf(&cg->out, "while (%s) ", cond);
            free(cond);
            emit_block(cg, n->while_stmt.body);
            break;
        }
        case NODE_FOR: {
            char* header = transform_expr(cg, n->for_stmt.header);
            char loop_type[32] = {0}, loop_name[64] = {0};
            if (sscanf(header, "%31s %63[^=; ]", loop_type, loop_name) == 2)
                symbols_add(cg->symbols, loop_name, loop_type, 0);
            emit_indent(cg);
            sb_appendf(&cg->out, "for (%s) ", header);
            free(header);
            emit_block(cg, n->for_stmt.body);
            break;
        }
        case NODE_BLOCK:
            emit_indent(cg);
            emit_block(cg, n);
            break;
        default:
            break;
    }
}

/* ─────────────────────────────────────────────────────────────────
   Runtime emission
   ───────────────────────────────────────────────────────────────── */
static void emit_runtime(StrBuf* out) {
    /* ── system headers & type aliases ── */
    sb_append(out,
        "#include <stdio.h>\n#include <stdlib.h>\n#include <stdbool.h>\n"
        "#include <stdarg.h>\n#include <stdint.h>\n#include <float.h>\n"
        "#include <string.h>\n#include <locale.h>\n"
        "#if defined(_WIN32)||defined(_WIN64)\n"
        "#ifndef _WIN32_WINNT\n#define _WIN32_WINNT 0x0600\n#endif\n"
        "#include <winsock2.h>\n#include <ws2tcpip.h>\n#include <windows.h>\n"
        "#else\n"
        "#include <sys/types.h>\n#include <sys/socket.h>\n#include <netdb.h>\n"
        "#include <netinet/in.h>\n#include <unistd.h>\n#include <errno.h>\n"
        "#include <pthread.h>\n#endif\n"
        "\ntypedef char* string;\n"
        "typedef struct MlgJson  { string text; } MlgJson;  typedef MlgJson*  json;\n"
        "typedef struct MlgFile  { string path; string mode; string* lines; int line_count; int line_cap; } MlgFile; typedef MlgFile* file;\n"
        "typedef struct MlgTask  {\n"
        "    #if defined(_WIN32)||defined(_WIN64)\n    HANDLE thread;\n    #else\n    pthread_t thread;\n    #endif\n"
        "    void* result;\n    volatile bool completed;\n} MlgTask;\n"
        "typedef MlgTask* task;\n\n"
        "#define COUNT_ARGS(...) COUNT_ARGS_IMPL(__VA_ARGS__, 10,9,8,7,6,5,4,3,2,1)\n"
        "#define COUNT_ARGS_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,N,...) N\n"
        "#define m_max(...) m_max_impl(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)\n"
        "#define m_min(...) m_min_impl(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)\n\n");

    /* ── string / math helpers ── */
    sb_append(out,
        "string string_dup(const char* s){const char* src=s?s:\"\";size_t len=strlen(src)+1;string r=malloc(len);if(r)memcpy(r,src,len);return r;}\n"
        "string input(const char* prompt){printf(\"%s\",prompt);string s=malloc(1024);fgets(s,1024,stdin);s[strcspn(s,\"\\n\")]=0;return s;}\n"
        "string string_replace(string s,const char* old,const char* new_str){if(!s||!old||!*old)return string_dup(s);char* pos=strstr(s,old);if(!pos)return string_dup(s);size_t ol=strlen(old),nl=new_str?strlen(new_str):0;string r=malloc(strlen(s)-ol+nl+1);strncpy(r,s,pos-s);strcpy(r+(pos-s),new_str?new_str:\"\");strcpy(r+(pos-s)+nl,pos+ol);return r;}\n"
        "string string_cut(string s,const char* marker){if(!s||!marker)return string_dup(s);char* pos=strstr(s,marker);if(!pos)return string_dup(s);size_t len=pos-s;string r=malloc(len+1);strncpy(r,s,len);r[len]=0;return r;}\n"
        "string string_from(string s,const char* marker){if(!s||!marker)return string_dup(s);char* pos=strstr(s,marker);return string_dup(pos?pos:s);}\n"
        "string string_replace_all(string s,const char* old,const char* new_str){if(!s||!old||!*old)return string_dup(s);size_t ol=strlen(old),nl=new_str?strlen(new_str):0,count=0;for(char* p=s;(p=strstr(p,old));p+=ol)count++;string r=malloc(strlen(s)+count*(nl-ol)+1);char* d=r;char* src=s;char* p;while((p=strstr(src,old))){strncpy(d,src,p-src);d+=p-src;if(new_str)strcpy(d,new_str);d+=nl;src=p+ol;}strcpy(d,src);return r;}\n"
        "int string_has(string s,const char* sub){return s&&sub&&strstr(s,sub)!=NULL;}\n"
        "float m_clamp(float value,float min,float max){if(value<min)return min;if(value>max)return max;return value;}\n"
        "float m_max_impl(int count,...){va_list a;va_start(a,count);float m=-FLT_MAX;for(int i=0;i<count;i++){double v=va_arg(a,double);if(v>m)m=(float)v;}va_end(a);return m;}\n"
        "float m_min_impl(int count,...){va_list a;va_start(a,count);float m=FLT_MAX;for(int i=0;i<count;i++){double v=va_arg(a,double);if(v<m)m=(float)v;}va_end(a);return m;}\n"
        "char* to_string(double number){char* s=malloc(50);snprintf(s,50,\"%f\",number);return s;}\n"
        "string mlg_format(const char* fmt,...){char* out=malloc(4096);va_list a;va_start(a,fmt);vsnprintf(out,4096,fmt,a);va_end(a);return out;}\n"
        "int parse_int(string s){return s?atoi(s):0;}\n"
        "float parse_float(string s){return s?(float)atof(s):0.0f;}\n"
        "double parse_double(string s){return s?atof(s):0.0;}\n");

    /* ── JSON ── */
    sb_append(out,
        "static string json_trim_copy(const char* a,int n){while(n>0&&(*a==' '||*a=='\\n'||*a=='\\t'||*a=='\\r')){a++;n--;}while(n>0&&(a[n-1]==' '||a[n-1]=='\\n'||a[n-1]=='\\t'||a[n-1]=='\\r'))n--;string r=malloc(n+1);memcpy(r,a,n);r[n]=0;return r;}\n"
        "json json_parse(string s){json j=malloc(sizeof(MlgJson));j->text=string_dup(s?s:\"{}\");return j;}\n"
        "string json_stringify(json j){return j?j->text:\"{}\";}\n"
        "string json_get(json j,const char* key){if(!j||!key)return string_dup(\"\");char pat[256];snprintf(pat,sizeof(pat),\"\\\"%s\\\"\",key);char* p=strstr(j->text,pat);if(!p)return string_dup(\"\");p=strchr(p+strlen(pat),':');if(!p)return string_dup(\"\");p++;while(*p==' '||*p=='\\n'||*p=='\\t'||*p=='\\r')p++;if(*p=='\\\"'){p++;char* e=p;while(*e&&!(*e=='\\\"'&&e[-1]!='\\\\'))e++;return json_trim_copy(p,(int)(e-p));}char* e=p;while(*e&&*e!=','&&*e!='}')e++;return json_trim_copy(p,(int)(e-p));}\n"
        "int json_get_int(json j,const char* key){string v=json_get(j,key);int r=parse_int(v);free(v);return r;}\n"
        "float json_get_float(json j,const char* key){string v=json_get(j,key);float r=parse_float(v);free(v);return r;}\n"
        "double json_get_double(json j,const char* key){string v=json_get(j,key);double r=parse_double(v);free(v);return r;}\n"
        "void json_set(json j,const char* key,const char* value){if(!j||!key)return;char pat[256];snprintf(pat,sizeof(pat),\"\\\"%s\\\"\",key);char* k=strstr(j->text,pat);char* end=strrchr(j->text,'}');const char* val=value?value:\"\";if(k){char* colon=strchr(k+strlen(pat),':');if(!colon)return;char* vs=colon+1;while(*vs==' '||*vs=='\\n'||*vs=='\\t'||*vs=='\\r')vs++;char* ve=vs;if(*vs=='\\\"'){ve=vs+1;while(*ve&&!(*ve=='\\\"'&&ve[-1]!='\\\\'))ve++;if(*ve)ve++;}else{while(*ve&&*ve!=','&&*ve!='}')ve++;}size_t pre=vs-j->text;size_t post=strlen(ve);string r=malloc(pre+strlen(val)+post+4);snprintf(r,pre+1,\"%s\",j->text);strcat(r,\"\\\"\");strcat(r,val);strcat(r,\"\\\"\");strcat(r,ve);free(j->text);j->text=r;return;}size_t base=end?(size_t)(end-j->text):strlen(j->text);string r=malloc(base+strlen(key)+strlen(val)+16);snprintf(r,base+1,\"%s\",j->text);if(base<=1)strcpy(r,\"{\");else strcat(r,\",\");strcat(r,\"\\\"\");strcat(r,key);strcat(r,\"\\\":\\\"\");strcat(r,val);strcat(r,\"\\\"}\");free(j->text);j->text=r;}\n");

    /* ── File I/O ── */
    sb_append(out,
        "static void file_reserve(file f,int need){if(need<=f->line_cap)return;while(f->line_cap<need)f->line_cap=f->line_cap?f->line_cap*2:8;f->lines=realloc(f->lines,sizeof(string)*f->line_cap);}\n"
        "file mlg_file_open(string path,string mode){file f=calloc(1,sizeof(MlgFile));f->path=string_dup(path);f->mode=string_dup(mode?mode:\"read\");if(mode&&strcmp(mode,\"write\")==0)return f;FILE* fp=fopen(path,\"rb\");if(!fp)return f;char buf[4096];while(fgets(buf,sizeof(buf),fp)){buf[strcspn(buf,\"\\r\\n\")]=0;file_reserve(f,f->line_count+1);f->lines[f->line_count++]=string_dup(buf);}fclose(fp);return f;}\n"
        "string file_get(file f,int index){if(!f||index<0||index>=f->line_count)return string_dup(\"\");return f->lines[index];}\n"
        "void file_set(file f,int index,string value){if(!f||index<0)return;file_reserve(f,index+1);while(f->line_count<=index)f->lines[f->line_count++]=string_dup(\"\");free(f->lines[index]);f->lines[index]=string_dup(value);}\n"
        "void file_append(file f,string value){if(!f)return;file_reserve(f,f->line_count+1);f->lines[f->line_count++]=string_dup(value);}\n"
        "void file_save(file f){if(!f)return;FILE* fp=fopen(f->path,\"wb\");if(!fp)return;for(int i=0;i<f->line_count;i++){fputs(f->lines[i],fp);fputc('\\n',fp);}fclose(fp);}\n"
        "void file_delete(file f,bool permanent){if(!f)return;if(permanent){remove(f->path);return;}for(int i=0;i<f->line_count;i++)free(f->lines[i]);f->line_count=0;file_save(f);}\n");

    /* ── Network helpers (shared by HTTP and WS) ── */
    sb_append(out,
        "static void net_init(){static int done=0;if(done)return;done=1;\n"
        "#if defined(_WIN32)||defined(_WIN64)\nWSADATA w;WSAStartup(MAKEWORD(2,2),&w);\n#endif\n}\n"
        "static void net_close_socket(int s){\n"
        "#if defined(_WIN32)||defined(_WIN64)\nclosesocket(s);\n#else\nclose(s);\n#endif\n}\n"
        "static unsigned long mlg_hash_core(const char* s){unsigned long h=1469598103u;while(s&&*s){h^=(unsigned char)*s++;h*=16777619u;}return h;}\n");

    /* ── password_hash / password_verify ── */
    sb_append(out,
        "string password_hash(string password){"
        "char* out=malloc(64);"
        "unsigned long h=mlg_hash_core(password);"
        "snprintf(out,64,\"mlg$fnv1a$%08lx\",h);"
        "return out;}\n"
        "bool password_verify(string password,string hash){"
        "string h=password_hash(password);"
        "bool ok=hash&&strcmp(h,hash)==0;"
        "free(h);return ok;}\n");

    /* ── HTTP client: GET ── */
    sb_append(out,
        "string http_get(string host,string path,int port){\n"
        "    net_init();\n"
        "    struct hostent* he=gethostbyname(host);\n"
        "    if(!he)return string_dup(\"\");\n"
        "    int sock=(int)socket(AF_INET,SOCK_STREAM,0);\n"
        "    if(sock<0)return string_dup(\"\");\n"
        "    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));\n"
        "    addr.sin_family=AF_INET;\n"
        "    addr.sin_port=htons((unsigned short)port);\n"
        "    memcpy(&addr.sin_addr,he->h_addr_list[0],(size_t)he->h_length);\n"
        "    if(connect(sock,(struct sockaddr*)&addr,sizeof(addr))!=0){net_close_socket(sock);return string_dup(\"\");}\n"
        "    char req[1024];\n"
        "    snprintf(req,sizeof(req),\"GET %s HTTP/1.0\\r\\nHost: %s\\r\\nConnection: close\\r\\n\\r\\n\",path?path:\"/\",host);\n"
        "    send(sock,req,(int)strlen(req),0);\n"
        "    char* out=malloc(65536);int used=0,n;\n"
        "    while((n=(int)recv(sock,out+used,65535-used,0))>0){used+=n;if(used>=65535)break;}\n"
        "    out[used]=0;net_close_socket(sock);return out;}\n");

    /* ── HTTP client: POST  (NEW) ── */
    sb_append(out,
        "string http_post(string host,string path,int port,string body){\n"
        "    net_init();\n"
        "    struct hostent* he=gethostbyname(host);\n"
        "    if(!he)return string_dup(\"\");\n"
        "    int sock=(int)socket(AF_INET,SOCK_STREAM,0);\n"
        "    if(sock<0)return string_dup(\"\");\n"
        "    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));\n"
        "    addr.sin_family=AF_INET;\n"
        "    addr.sin_port=htons((unsigned short)port);\n"
        "    memcpy(&addr.sin_addr,he->h_addr_list[0],(size_t)he->h_length);\n"
        "    if(connect(sock,(struct sockaddr*)&addr,sizeof(addr))!=0){net_close_socket(sock);return string_dup(\"\");}\n"
        "    const char* b=body?body:\"\";\n"
        "    char req[4096];\n"
        "    snprintf(req,sizeof(req),\n"
        "        \"POST %s HTTP/1.0\\r\\nHost: %s\\r\\n\"\n"
        "        \"Content-Type: application/json\\r\\n\"\n"
        "        \"Content-Length: %lu\\r\\nConnection: close\\r\\n\\r\\n%s\",\n"
        "        path?path:\"/\",host,(unsigned long)strlen(b),b);\n"
        "    send(sock,req,(int)strlen(req),0);\n"
        "    char* out=malloc(65536);int used=0,n;\n"
        "    while((n=(int)recv(sock,out+used,65535-used,0))>0){used+=n;if(used>=65535)break;}\n"
        "    out[used]=0;net_close_socket(sock);return out;}\n");

    /* ── HTTP request parsers (NEW) ── */
    sb_append(out,
        "/* Returns the HTTP method: GET, POST, PUT, DELETE, etc. */\n"
        "string http_parse_method(string request){\n"
        "    if(!request||!request[0])return string_dup(\"\");\n"
        "    char* space=strchr(request,' ');\n"
        "    if(!space)return string_dup(\"\");\n"
        "    return json_trim_copy(request,(int)(space-request));}\n"

        "/* Returns the URL path, e.g. \"/api/users\" */\n"
        "string http_parse_path(string request){\n"
        "    if(!request||!request[0])return string_dup(\"/\");\n"
        "    char* first=strchr(request,' ');\n"
        "    if(!first)return string_dup(\"/\");\n"
        "    char* start=first+1;\n"
        "    char* end=strchr(start,' ');\n"
        "    if(!end)end=start+strlen(start);\n"
        "    /* strip query string for the plain path */\n"
        "    char* q=start;while(q<end&&*q!='?')q++;\n"
        "    return json_trim_copy(start,(int)(q-start));}\n"

        "/* Returns the query string after '?', or empty string */\n"
        "string http_parse_query(string request){\n"
        "    if(!request||!request[0])return string_dup(\"\");\n"
        "    char* first=strchr(request,' ');\n"
        "    if(!first)return string_dup(\"\");\n"
        "    char* start=first+1;\n"
        "    char* end=strchr(start,' ');\n"
        "    if(!end)end=start+strlen(start);\n"
        "    char* q=start;while(q<end&&*q!='?')q++;\n"
        "    if(q>=end)return string_dup(\"\");\n"
        "    return json_trim_copy(q+1,(int)(end-q-1));}\n"

        "/* Returns the request body (everything after \\r\\n\\r\\n) */\n"
        "string http_parse_body(string request){\n"
        "    if(!request)return string_dup(\"\");\n"
        "    char* sep=strstr(request,\"\\r\\n\\r\\n\");\n"
        "    return string_dup(sep?sep+4:\"\");}\n"

        "/* Returns the value of a specific header, e.g. \"Content-Type\" */\n"
        "string http_parse_header(string request,const char* header_name){\n"
        "    if(!request||!header_name)return string_dup(\"\");\n"
        "    char needle[256];\n"
        "    snprintf(needle,sizeof(needle),\"%s:\",header_name);\n"
        "    char* pos=strstr(request,needle);\n"
        "    if(!pos)return string_dup(\"\");\n"
        "    char* start=pos+strlen(needle);\n"
        "    while(*start==' '||*start=='\\t')start++;\n"
        "    char* end=strstr(start,\"\\r\\n\");\n"
        "    if(!end)end=start+strlen(start);\n"
        "    return json_trim_copy(start,(int)(end-start));}\n");

    /* ── HTTP server helpers ── */
    sb_append(out,
        "int http_server_once(int port,string response){net_init();int srv=(int)socket(AF_INET,SOCK_STREAM,0);if(srv<0)return 0;struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=0;addr.sin_port=htons((unsigned short)port);int yes=1;setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,(char*)&yes,sizeof(yes));if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))!=0){net_close_socket(srv);return 0;}listen(srv,1);int c=(int)accept(srv,NULL,NULL);if(c<0){net_close_socket(srv);return 0;}char buf[2048];recv(c,buf,sizeof(buf)-1,0);const char* body=response?response:\"\";char header[512];snprintf(header,sizeof(header),\"HTTP/1.1 200 OK\\r\\nContent-Type: text/plain; charset=utf-8\\r\\nContent-Length: %lu\\r\\nConnection: close\\r\\n\\r\\n\",(unsigned long)strlen(body));send(c,header,(int)strlen(header),0);send(c,body,(int)strlen(body),0);net_close_socket(c);net_close_socket(srv);return 1;}\n"
        "int http_server_start(int port){net_init();int srv=(int)socket(AF_INET,SOCK_STREAM,0);if(srv<0)return -1;struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=0;addr.sin_port=htons((unsigned short)port);int yes=1;setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,(char*)&yes,sizeof(yes));if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))!=0){net_close_socket(srv);return -1;}if(listen(srv,10)!=0){net_close_socket(srv);return -1;}return srv;}\n"
        "int http_accept(int server_socket){if(server_socket<0)return -1;return(int)accept(server_socket,NULL,NULL);}\n"
        "string http_read_request(int client_socket){if(client_socket<0)return string_dup(\"\");char* buf=malloc(4096);int n=recv(client_socket,buf,4095,0);if(n<=0){free(buf);return string_dup(\"\");}buf[n]='\\0';return buf;}\n"
        "void http_send_response(int client_socket,string content_type,string response_body){if(client_socket<0)return;const char* ct=content_type?content_type:\"text/html; charset=utf-8\";const char* body=response_body?response_body:\"\";char header[512];snprintf(header,sizeof(header),\"HTTP/1.1 200 OK\\r\\nContent-Type: %s\\r\\nContent-Length: %lu\\r\\nConnection: close\\r\\n\\r\\n\",ct,(unsigned long)strlen(body));send(client_socket,header,(int)strlen(header),0);send(client_socket,body,(int)strlen(body),0);}\n"
        "void http_close(int client_socket){if(client_socket>=0)net_close_socket(client_socket);}\n\n");

    /* ── WebSocket ── */
    sb_append(out,
        "int websocket_server_once(int port){net_init();int srv=(int)socket(AF_INET,SOCK_STREAM,0);if(srv<0)return -1;struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=0;addr.sin_port=htons((unsigned short)port);int yes=1;setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,(char*)&yes,sizeof(yes));if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))!=0){net_close_socket(srv);return -1;}listen(srv,1);int c=(int)accept(srv,NULL,NULL);net_close_socket(srv);return c;}\n"
        "void websocket_send_text(int socket_id,string text){if(socket_id<0||!text)return;unsigned char header[10];size_t len=strlen(text);header[0]=0x81;if(len<126){header[1]=(unsigned char)len;send(socket_id,(char*)header,2,0);}else{header[1]=126;header[2]=(unsigned char)((len>>8)&255);header[3]=(unsigned char)(len&255);send(socket_id,(char*)header,4,0);}send(socket_id,text,(int)len,0);}\n"
        "void websocket_close(int socket_id){if(socket_id>=0)net_close_socket(socket_id);}\n");

    /* ── console helpers ── */
    sb_append(out,
        "void mlg_init_console(){\n"
        "setlocale(LC_ALL,\".UTF8\");\n"
        "#if defined(_WIN32)||defined(_WIN64)\n"
        "extern int __stdcall SetConsoleOutputCP(unsigned int);\n"
        "extern int __stdcall SetConsoleCP(unsigned int);\n"
        "SetConsoleOutputCP(65001);SetConsoleCP(65001);\n"
        "system(\"chcp 65001 > nul\");\n"
        "#endif\n}\n"
        "void clear_console(){\n"
        "#if defined(_WIN32)||defined(_WIN64)\nsystem(\"cls\");\n"
        "#else\nsystem(\"clear\");\n#endif\n}\n\n");

    /* ── async / task ── */
    sb_append(out,
        "task mlg_task_create(){task t=calloc(1,sizeof(MlgTask));return t;}\n"
        "void mlg_thread_start(task t,void*(*fn)(void*),void* arg){\n"
        "#if defined(_WIN32)||defined(_WIN64)\n"
        "t->thread=CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)fn,arg,0,NULL);\n"
        "#else\npthread_create(&t->thread,NULL,fn,arg);\n#endif\n}\n"
        "void* mlg_await(task t){if(!t)return NULL;\n"
        "#if defined(_WIN32)||defined(_WIN64)\n"
        "WaitForSingleObject(t->thread,INFINITE);CloseHandle(t->thread);\n"
        "#else\npthread_join(t->thread,NULL);\n#endif\n"
        "void* res=t->result;free(t);return res;}\n\n");

    /* ── SHA-1 + Base64 (used for WebSocket handshake) ── */
    sb_append(out,
        "#define SHA1_ROT(x,y) ((x<<y)|(x>>(32-y)))\n"
        "static void mlg_sha1(const unsigned char* msg,size_t len,unsigned char* digest){\n"
        "    uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;\n"
        "    size_t new_len=(((len+8)/64)+1)*64;\n"
        "    unsigned char* pad=calloc(1,new_len);\n"
        "    memcpy(pad,msg,len);pad[len]=0x80;\n"
        "    uint64_t bits=(uint64_t)len*8;\n"
        "    for(int i=0;i<8;i++)pad[new_len-8+i]=(unsigned char)(bits>>(56-i*8));\n"
        "    for(size_t chunk=0;chunk<new_len;chunk+=64){\n"
        "        uint32_t w[80];\n"
        "        for(int i=0;i<16;i++)w[i]=((uint32_t)pad[chunk+i*4]<<24)|((uint32_t)pad[chunk+i*4+1]<<16)|((uint32_t)pad[chunk+i*4+2]<<8)|pad[chunk+i*4+3];\n"
        "        for(int i=16;i<80;i++)w[i]=SHA1_ROT((w[i-3]^w[i-8]^w[i-14]^w[i-16]),1);\n"
        "        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;\n"
        "        for(int i=0;i<80;i++){\n"
        "            uint32_t f,k;\n"
        "            if(i<20){f=(b&c)|(~b&d);k=0x5A827999;}\n"
        "            else if(i<40){f=b^c^d;k=0x6ED9EBA1;}\n"
        "            else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}\n"
        "            else{f=b^c^d;k=0xCA62C1D6;}\n"
        "            uint32_t temp=SHA1_ROT(a,5)+f+e+k+w[i];\n"
        "            e=d;d=c;c=SHA1_ROT(b,30);b=a;a=temp;}\n"
        "        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;}\n"
        "    free(pad);\n"
        "    digest[0]=(unsigned char)(h0>>24);digest[1]=(unsigned char)(h0>>16);digest[2]=(unsigned char)(h0>>8);digest[3]=(unsigned char)h0;\n"
        "    digest[4]=(unsigned char)(h1>>24);digest[5]=(unsigned char)(h1>>16);digest[6]=(unsigned char)(h1>>8);digest[7]=(unsigned char)h1;\n"
        "    digest[8]=(unsigned char)(h2>>24);digest[9]=(unsigned char)(h2>>16);digest[10]=(unsigned char)(h2>>8);digest[11]=(unsigned char)h2;\n"
        "    digest[12]=(unsigned char)(h3>>24);digest[13]=(unsigned char)(h3>>16);digest[14]=(unsigned char)(h3>>8);digest[15]=(unsigned char)h3;\n"
        "    digest[16]=(unsigned char)(h4>>24);digest[17]=(unsigned char)(h4>>16);digest[18]=(unsigned char)(h4>>8);digest[19]=(unsigned char)h4;}\n"
        "static const char mlg_b64chars[]=\"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/\";\n"
        "static void mlg_base64_encode(const unsigned char* in,size_t in_len,char* out){\n"
        "    size_t i=0,j=0;uint32_t v;\n"
        "    for(;i<in_len-(in_len%3);i+=3){\n"
        "        v=((uint32_t)in[i]<<16)|((uint32_t)in[i+1]<<8)|in[i+2];\n"
        "        out[j++]=mlg_b64chars[(v>>18)&0x3F];out[j++]=mlg_b64chars[(v>>12)&0x3F];\n"
        "        out[j++]=mlg_b64chars[(v>>6)&0x3F];out[j++]=mlg_b64chars[v&0x3F];}\n"
        "    if(in_len%3==1){v=in[i];out[j++]=mlg_b64chars[(v>>2)&0x3F];out[j++]=mlg_b64chars[(v<<4)&0x3F];out[j++]='=';out[j++]='=';}\n"
        "    else if(in_len%3==2){v=((uint32_t)in[i]<<8)|in[i+1];out[j++]=mlg_b64chars[(v>>10)&0x3F];out[j++]=mlg_b64chars[(v>>4)&0x3F];out[j++]=mlg_b64chars[(v<<2)&0x3F];out[j++]='=';}\n"
        "    out[j]='\\0';}\n\n");

    /* ── WebSocket upgrade handshake ── */
    sb_append(out,
        "int websocket_server_start(int port){return http_server_start(port);}\n"
        "int websocket_accept(int server_socket){\n"
        "    int c=http_accept(server_socket);\n"
        "    if(c<0)return -1;\n"
        "    char req[4096];int n=recv(c,req,sizeof(req)-1,0);\n"
        "    if(n<=0){net_close_socket(c);return -1;}\n"
        "    req[n]='\\0';\n"
        "    char* key_header=strstr(req,\"Sec-WebSocket-Key:\");\n"
        "    if(!key_header){net_close_socket(c);return -1;}\n"
        "    key_header+=strlen(\"Sec-WebSocket-Key:\");\n"
        "    while(*key_header==' '||*key_header=='\\t')key_header++;\n"
        "    char* key_end=strstr(key_header,\"\\r\\n\");\n"
        "    if(!key_end){net_close_socket(c);return -1;}\n"
        "    char key[128]={0};\n"
        "    snprintf(key,sizeof(key),\"%.*s\",(int)(key_end-key_header),key_header);\n"
        "    char concat[256];\n"
        "    snprintf(concat,sizeof(concat),\"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11\",key);\n"
        "    unsigned char hash[20];\n"
        "    mlg_sha1((unsigned char*)concat,strlen(concat),hash);\n"
        "    char accept_key[64];\n"
        "    mlg_base64_encode(hash,20,accept_key);\n"
        "    char response[512];\n"
        "    snprintf(response,sizeof(response),\n"
        "        \"HTTP/1.1 101 Switching Protocols\\r\\n\"\n"
        "        \"Upgrade: websocket\\r\\nConnection: Upgrade\\r\\n\"\n"
        "        \"Sec-WebSocket-Accept: %s\\r\\n\\r\\n\",accept_key);\n"
        "    send(c,response,(int)strlen(response),0);\n"
        "    return c;}\n\n");
}

/* ─────────────────────────────────────────────────────────────────
   Entry point
   ───────────────────────────────────────────────────────────────── */
char* codegen_program(AstNode* program, SymbolTable* symbols) {
    Codegen cg;
    sb_init(&cg.out);
    cg.symbols    = symbols;
    cg.indent     = 0;
    cg.in_function = 0;

    emit_runtime(&cg.out);

    /* emit top-level functions first */
    for (int i = 0; i < program->program.statements.count; i++) {
        AstNode* n = program->program.statements.items[i];
        if (is_function_node(n)) {
            emit_node(&cg, n);
            sb_append_char(&cg.out, '\n');
        }
    }

    /* emit main() body */
    sb_append(&cg.out, "int main(){\n    mlg_init_console();\n");
    cg.indent = 1;
    for (int i = 0; i < program->program.statements.count; i++) {
        AstNode* n = program->program.statements.items[i];
        if (!is_function_node(n) && n->kind != NODE_IMPORT)
            emit_node(&cg, n);
    }
    sb_append(&cg.out, "    return 0;\n}\n");

    char* out = mlg_strdup(cg.out.data);
    sb_free(&cg.out);
    return out;
}
