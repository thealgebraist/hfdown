#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

typedef enum {
    TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COLON, TOK_COMMA, TOK_STRING, TOK_NUMBER,
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_EOF, TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    const char* start;
    size_t length;
} token_t;

typedef enum {
    STATE_START,
    STATE_STRING,
    STATE_NUMBER,
    STATE_IDENT,
    STATE_DONE
} lex_state_t;

token_t json_next_token(const char** input) {
    const char* p = *input;
    while (*p && isspace(*p)) p++;

    token_t tok = {TOK_ERROR, p, 0};
    if (!*p) {
        tok.type = TOK_EOF;
        return tok;
    }

    char c = *p;
    switch (c) {
        case '{': tok.type = TOK_LBRACE; p++; break;
        case '}': tok.type = TOK_RBRACE; p++; break;
        case '[': tok.type = TOK_LBRACKET; p++; break;
        case ']': tok.type = TOK_RBRACKET; p++; break;
        case ':': tok.type = TOK_COLON; p++; break;
        case ',': tok.type = TOK_COMMA; p++; break;
        case '"': {
            tok.type = TOK_STRING;
            p++;
            tok.start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++; 
                p++;
            }
            tok.length = p - tok.start;
            if (*p == '"') p++;
            break;
        }
        default:
            if (isdigit(c) || c == '-') {
                tok.type = TOK_NUMBER;
                tok.start = p;
                while (*p && (isdigit(*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) p++;
                tok.length = p - tok.start;
            } else if (isalpha(c)) {
                tok.start = p;
                while (*p && isalpha(*p)) p++;
                tok.length = p - tok.start;
                if (strncmp(tok.start, "true", 4) == 0) tok.type = TOK_TRUE;
                else if (strncmp(tok.start, "false", 5) == 0) tok.type = TOK_FALSE;
                else if (strncmp(tok.start, "null", 4) == 0) tok.type = TOK_NULL;
            }
            break;
    }

    *input = p;
    return tok;
}
