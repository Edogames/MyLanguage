#include "lexer.h"
#include "../common/util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void push_text(TokenArray* out, TokenKind kind, const char* start, int len, int line, int col) {
    char* text = (char*)malloc((size_t)len + 1);
    memcpy(text, start, (size_t)len);
    text[len] = '\0';
    tokens_push(out, (Token){kind, text, line, col});
}

int lex_source(const char* source, const char* filename, TokenArray* out) {
    int i = 0, line = 1, col = 1;
    (void)filename;
    tokens_init(out);

    while (source[i]) {
        char c = source[i];
        if (c == '\r') {
            i++;
            continue;
        }
        if (c == '\n') {
            i++;
            line++;
            col = 1;
            continue;
        }
        if (isspace((unsigned char)c)) {
            i++;
            col++;
            continue;
        }
        if (c == '/' && source[i + 1] == '/') {
            while (source[i] && source[i] != '\n') i++;
            continue;
        }
        if (c == '/' && source[i + 1] == '*') {
            i += 2;
            col += 2;
            while (source[i] && !(source[i] == '*' && source[i + 1] == '/')) {
                if (source[i] == '\n') {
                    line++;
                    col = 1;
                    i++;
                } else {
                    i++;
                    col++;
                }
            }
            if (source[i]) {
                i += 2;
                col += 2;
            }
            continue;
        }

        int start_line = line, start_col = col;
        if (isalpha((unsigned char)c) || c == '_') {
            int start = i;
            while (isalnum((unsigned char)source[i]) || source[i] == '_') {
                i++;
                col++;
            }
            push_text(out, TOK_IDENT, source + start, i - start, start_line, start_col);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            int start = i;
            while (isdigit((unsigned char)source[i]) || source[i] == '.') {
                i++;
                col++;
            }
            push_text(out, TOK_NUMBER, source + start, i - start, start_line, start_col);
            continue;
        }
        if (c == '$' && source[i + 1] == '"') {
            int start = i;
            i += 2;
            col += 2;
            while (source[i]) {
                if (source[i] == '"' && source[i - 1] != '\\') {
                    i++;
                    col++;
                    break;
                }
                if (source[i] == '\n') {
                    line++;
                    col = 1;
                    i++;
                } else {
                    i++;
                    col++;
                }
            }
            push_text(out, TOK_INTERP_STRING, source + start, i - start, start_line, start_col);
            continue;
        }
        if (c == '"' || c == '\'') {
            char quote = c;
            int start = i++;
            col++;
            while (source[i]) {
                if (source[i] == quote && source[i - 1] != '\\') {
                    i++;
                    col++;
                    break;
                }
                if (source[i] == '\n') {
                    line++;
                    col = 1;
                    i++;
                } else {
                    i++;
                    col++;
                }
            }
            push_text(out, TOK_STRING, source + start, i - start, start_line, start_col);
            continue;
        }

        if ((c == '=' || c == '!' || c == '<' || c == '>') && source[i + 1] == '=') {
            push_text(out, TOK_SYMBOL, source + i, 2, start_line, start_col);
            i += 2;
            col += 2;
            continue;
        }
        if ((c == '&' && source[i + 1] == '&') || (c == '|' && source[i + 1] == '|') ||
            (c == '+' && source[i + 1] == '+') || (c == '-' && source[i + 1] == '-')) {
            push_text(out, TOK_SYMBOL, source + i, 2, start_line, start_col);
            i += 2;
            col += 2;
            continue;
        }

        push_text(out, TOK_SYMBOL, source + i, 1, start_line, start_col);
        i++;
        col++;
    }

    tokens_push(out, (Token){TOK_EOF, mlg_strdup("<eof>"), line, col});
    return 1;
}
