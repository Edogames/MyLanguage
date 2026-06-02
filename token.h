#ifndef MYLANG_UTIL_H
#define MYLANG_UTIL_H

#include <stddef.h>

char* mlg_strdup(const char* s);
char* mlg_read_file(const char* path);
void mlg_path_dirname(const char* path, char* out, size_t out_size);
void mlg_path_join_module(const char* base_dir, const char* module, char* out, size_t out_size);

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf* sb);
void sb_free(StrBuf* sb);
void sb_append(StrBuf* sb, const char* text);
void sb_append_char(StrBuf* sb, char ch);
void sb_appendf(StrBuf* sb, const char* fmt, ...);

#endif

