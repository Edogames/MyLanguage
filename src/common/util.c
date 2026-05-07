#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* mlg_strdup(const char* s) {
    const char* src = s ? s : "";
    size_t len = strlen(src) + 1;
    char* out = (char*)malloc(len);
    if (out) memcpy(out, src, len);
    return out;
}

char* mlg_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char* data = (char*)malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(data, 1, (size_t)size, f);
    data[n] = '\0';
    fclose(f);
    return data;
}

void mlg_path_dirname(const char* path, char* out, size_t out_size) {
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* sep = slash > backslash ? slash : backslash;
    if (!sep) {
        snprintf(out, out_size, ".");
        return;
    }
    size_t len = (size_t)(sep - path);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

void mlg_path_join_module(const char* base_dir, const char* module, char* out, size_t out_size) {
    char mod[256];
    snprintf(mod, sizeof(mod), "%s", module);
    char* ext = strstr(mod, ".mlg");
    if (ext) *ext = '\0';
    if (!base_dir || strcmp(base_dir, ".") == 0) {
        snprintf(out, out_size, "%s.mlg", mod);
    } else {
        snprintf(out, out_size, "%s/%s.mlg", base_dir, mod);
    }
}

void sb_init(StrBuf* sb) {
    sb->cap = 1024;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (sb->data) sb->data[0] = '\0';
}

void sb_free(StrBuf* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_reserve(StrBuf* sb, size_t need) {
    if (need <= sb->cap) return;
    while (sb->cap < need) sb->cap *= 2;
    sb->data = (char*)realloc(sb->data, sb->cap);
}

void sb_append(StrBuf* sb, const char* text) {
    if (!text) return;
    size_t n = strlen(text);
    sb_reserve(sb, sb->len + n + 1);
    memcpy(sb->data + sb->len, text, n + 1);
    sb->len += n;
}

void sb_append_char(StrBuf* sb, char ch) {
    sb_reserve(sb, sb->len + 2);
    sb->data[sb->len++] = ch;
    sb->data[sb->len] = '\0';
}

void sb_appendf(StrBuf* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        va_end(args);
        return;
    }
    sb_reserve(sb, sb->len + (size_t)n + 1);
    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
    sb->len += (size_t)n;
    va_end(args);
}

