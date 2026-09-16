#include "templar.h"
#include "klog.h"
#include "vesa.h"
#include "terminal.h"
#include "reboot.h"
#include "string.h"
#include "fs.h"
#include "fat32.h"
#include <stdint.h>
#include <stddef.h>

extern void usb_poll_all(void);
extern volatile int usb_kbd_dirty;
extern char g_current_path[];

#define TEMPLAR_ARENA_SIZE 65536
static uint8_t g_arena[TEMPLAR_ARENA_SIZE];
static uint32_t g_arena_used;

static void arena_reset(void) {
    g_arena_used = 0;
}

static void *arena_alloc(uint32_t size) {

    uint32_t aligned = (g_arena_used + 3u) & ~3u;
    if (aligned + size > TEMPLAR_ARENA_SIZE) {
        return NULL;
    }
    void *ptr = &g_arena[aligned];
    g_arena_used = aligned + size;
    return ptr;
}

static char *arena_strndup(const char *src, uint32_t len) {
    char *out = (char *)arena_alloc(len + 1);
    if (!out) return NULL;
    for (uint32_t i = 0; i < len; i++) out[i] = src[i];
    out[len] = '\0';
    return out;
}

static char *arena_strdup(const char *src) {
    return arena_strndup(src, (uint32_t)strlen(src));
}

static int g_had_error;
static char g_error_msg[96];

static void templar_error(const char *msg) {
    if (g_had_error) return;
    g_had_error = 1;
    uint32_t i = 0;
    while (msg[i] && i < sizeof(g_error_msg) - 1) { g_error_msg[i] = msg[i]; i++; }
    g_error_msg[i] = '\0';
}

typedef enum {
    TOK_EOF, TOK_INT_LIT, TOK_STRING_LIT, TOK_IDENT,
    TOK_TRUE, TOK_FALSE,
    TOK_KW_INT, TOK_KW_STRING, TOK_KW_BOOL,
    TOK_KW_IF, TOK_KW_ELSE, TOK_KW_WHILE, TOK_KW_FUNC, TOK_KW_RETURN,
    TOK_KW_APP, TOK_KW_ON_KEY,
    TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN, TOK_SEMI, TOK_COMMA,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN, TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    int ival;
    char *sval;
} token_t;

typedef struct {
    const char *src;
    uint32_t pos;
    token_t cur;
} lexer_t;

static int is_ident_start(char c) { return (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static token_type_t keyword_lookup(const char *s) {
    if (strcmp(s, "int") == 0) return TOK_KW_INT;
    if (strcmp(s, "string") == 0) return TOK_KW_STRING;
    if (strcmp(s, "bool") == 0) return TOK_KW_BOOL;
    if (strcmp(s, "if") == 0) return TOK_KW_IF;
    if (strcmp(s, "else") == 0) return TOK_KW_ELSE;
    if (strcmp(s, "while") == 0) return TOK_KW_WHILE;
    if (strcmp(s, "func") == 0) return TOK_KW_FUNC;
    if (strcmp(s, "return") == 0) return TOK_KW_RETURN;
    if (strcmp(s, "app") == 0) return TOK_KW_APP;
    if (strcmp(s, "on_key") == 0) return TOK_KW_ON_KEY;
    if (strcmp(s, "true") == 0) return TOK_TRUE;
    if (strcmp(s, "false") == 0) return TOK_FALSE;
    return TOK_IDENT;
}

static void lexer_next(lexer_t *lx) {
    const char *s = lx->src;
    uint32_t p = lx->pos;

    for (;;) {
        char c = s[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
        if (c == '/' && s[p + 1] == '/') {
            while (s[p] && s[p] != '\n') p++;
            continue;
        }
        break;
    }

    char c = s[p];
    if (c == '\0') { lx->pos = p; lx->cur.type = TOK_EOF; return; }

    if (is_digit(c)) {
        uint32_t start = p;
        while (is_digit(s[p])) p++;
        int val = 0;
        for (uint32_t i = start; i < p; i++) val = val * 10 + (s[i] - '0');
        lx->pos = p;
        lx->cur.type = TOK_INT_LIT;
        lx->cur.ival = val;
        return;
    }

    if (is_ident_start(c)) {
        uint32_t start = p;
        while (is_ident_char(s[p])) p++;
        char tmp[64];
        uint32_t len = p - start;
        if (len > 63) len = 63;
        for (uint32_t i = 0; i < len; i++) tmp[i] = s[start + i];
        tmp[len] = '\0';
        lx->pos = p;
        token_type_t kw = keyword_lookup(tmp);
        lx->cur.type = kw;
        if (kw == TOK_IDENT) lx->cur.sval = arena_strdup(tmp);
        return;
    }

    if (c == '"') {
        p++;
        char buf[256];
        uint32_t len = 0;
        while (s[p] && s[p] != '"' && len < sizeof(buf) - 1) {
            if (s[p] == '\\' && s[p + 1]) {
                p++;
                char esc = s[p];
                if (esc == 'n') buf[len++] = '\n';
                else if (esc == 't') buf[len++] = '\t';
                else if (esc == '"') buf[len++] = '"';
                else if (esc == '\\') buf[len++] = '\\';
                else buf[len++] = esc;
                p++;
            } else {
                buf[len++] = s[p++];
            }
        }
        buf[len] = '\0';
        if (s[p] == '"') p++; else { templar_error("unterminated string literal"); }
        lx->pos = p;
        lx->cur.type = TOK_STRING_LIT;
        lx->cur.sval = arena_strndup(buf, len);
        return;
    }

    if (c == '=' && s[p + 1] == '=') { lx->pos = p + 2; lx->cur.type = TOK_EQ; return; }
    if (c == '!' && s[p + 1] == '=') { lx->pos = p + 2; lx->cur.type = TOK_NEQ; return; }
    if (c == '<' && s[p + 1] == '=') { lx->pos = p + 2; lx->cur.type = TOK_LE; return; }
    if (c == '>' && s[p + 1] == '=') { lx->pos = p + 2; lx->cur.type = TOK_GE; return; }
    if (c == '&' && s[p + 1] == '&') { lx->pos = p + 2; lx->cur.type = TOK_AND; return; }
    if (c == '|' && s[p + 1] == '|') { lx->pos = p + 2; lx->cur.type = TOK_OR; return; }

    lx->pos = p + 1;
    switch (c) {
        case '{': lx->cur.type = TOK_LBRACE; return;
        case '}': lx->cur.type = TOK_RBRACE; return;
        case '(': lx->cur.type = TOK_LPAREN; return;
        case ')': lx->cur.type = TOK_RPAREN; return;
        case ';': lx->cur.type = TOK_SEMI; return;
        case ',': lx->cur.type = TOK_COMMA; return;
        case '+': lx->cur.type = TOK_PLUS; return;
        case '-': lx->cur.type = TOK_MINUS; return;
        case '*': lx->cur.type = TOK_STAR; return;
        case '/': lx->cur.type = TOK_SLASH; return;
        case '%': lx->cur.type = TOK_PERCENT; return;
        case '=': lx->cur.type = TOK_ASSIGN; return;
        case '<': lx->cur.type = TOK_LT; return;
        case '>': lx->cur.type = TOK_GT; return;
        case '!': lx->cur.type = TOK_NOT; return;
        default:
            templar_error("unexpected character in source");
            lx->cur.type = TOK_ERROR;
            return;
    }
}

static void lexer_init(lexer_t *lx, const char *src) {
    lx->src = src;
    lx->pos = 0;
    lexer_next(lx);
}

typedef enum {
    ND_PROGRAM, ND_BLOCK, ND_VARDECL, ND_ASSIGN, ND_IF, ND_WHILE,
    ND_FUNCDECL, ND_RETURN, ND_EXPRSTMT, ND_APPDECL,
    ND_LIT_INT, ND_LIT_STR, ND_LIT_BOOL, ND_IDENT, ND_BINOP, ND_UNOP, ND_CALL
} node_kind_t;

typedef enum { VT_INT, VT_STRING, VT_BOOL, VT_VOID } val_type_t;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_NEG, OP_NOT
} op_t;

typedef struct node node_t;
struct node {
    node_kind_t kind;
    node_t *a, *b, *c;
    node_t *next;
    int ival;
    char *sval;
    char *sval2;
    val_type_t vtype;
    op_t op;
};

#define MAX_NEST 32

static node_t *node_new(node_kind_t kind) {
    node_t *n = (node_t *)arena_alloc(sizeof(node_t));
    if (!n) { templar_error("script too large (out of templar memory)"); return NULL; }
    n->kind = kind;
    n->a = n->b = n->c = n->next = NULL;
    n->ival = 0;
    n->sval = NULL;
    n->sval2 = NULL;
    n->vtype = VT_VOID;
    return n;
}

typedef struct {
    lexer_t lx;
    int depth;
} parser_t;

static node_t *parse_block(parser_t *p);
static node_t *parse_expr(parser_t *p);
static node_t *parse_statement(parser_t *p);

static int check(parser_t *p, token_type_t t) { return p->lx.cur.type == t; }

static int accept(parser_t *p, token_type_t t) {
    if (check(p, t)) { lexer_next(&p->lx); return 1; }
    return 0;
}

static void expect(parser_t *p, token_type_t t, const char *what) {
    if (!accept(p, t)) templar_error(what);
}

static val_type_t type_from_kw(parser_t *p) {
    if (accept(p, TOK_KW_INT)) return VT_INT;
    if (accept(p, TOK_KW_STRING)) return VT_STRING;
    if (accept(p, TOK_KW_BOOL)) return VT_BOOL;
    templar_error("expected a type (int/string/bool)");
    return VT_INT;
}

static int is_type_kw(parser_t *p) {
    return check(p, TOK_KW_INT) || check(p, TOK_KW_STRING) || check(p, TOK_KW_BOOL);
}

static node_t *parse_primary(parser_t *p) {
    if (g_had_error) return NULL;
    if (check(p, TOK_INT_LIT)) {
        node_t *n = node_new(ND_LIT_INT);
        if (n) n->ival = p->lx.cur.ival;
        lexer_next(&p->lx);
        return n;
    }
    if (check(p, TOK_STRING_LIT)) {
        node_t *n = node_new(ND_LIT_STR);
        if (n) n->sval = p->lx.cur.sval;
        lexer_next(&p->lx);
        return n;
    }
    if (accept(p, TOK_TRUE)) { node_t *n = node_new(ND_LIT_BOOL); if (n) n->ival = 1; return n; }
    if (accept(p, TOK_FALSE)) { node_t *n = node_new(ND_LIT_BOOL); if (n) n->ival = 0; return n; }
    if (check(p, TOK_IDENT)) {
        char *name = p->lx.cur.sval;
        lexer_next(&p->lx);
        if (accept(p, TOK_LPAREN)) {
            node_t *call = node_new(ND_CALL);
            if (call) call->sval = name;
            node_t *head = NULL, *tail = NULL;
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    node_t *arg = parse_expr(p);
                    if (!head) head = tail = arg; else { tail->next = arg; tail = arg; }
                    if (!accept(p, TOK_COMMA)) break;
                }
            }
            expect(p, TOK_RPAREN, "expected ')' after call arguments");
            if (call) call->a = head;
            return call;
        }
        node_t *n = node_new(ND_IDENT);
        if (n) n->sval = name;
        return n;
    }
    if (accept(p, TOK_LPAREN)) {
        node_t *n = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        return n;
    }
    templar_error("expected an expression");
    lexer_next(&p->lx);
    return NULL;
}

static node_t *parse_unary(parser_t *p) {
    if (accept(p, TOK_MINUS)) {
        node_t *n = node_new(ND_UNOP);
        if (n) { n->op = OP_NEG; n->a = parse_unary(p); }
        return n;
    }
    if (accept(p, TOK_NOT)) {
        node_t *n = node_new(ND_UNOP);
        if (n) { n->op = OP_NOT; n->a = parse_unary(p); }
        return n;
    }
    return parse_primary(p);
}

static node_t *parse_binop_level(parser_t *p, node_t *(*next_level)(parser_t *),
                                  const token_type_t *toks, const op_t *ops, int n_toks) {
    node_t *left = next_level(p);
    for (;;) {
        int matched = -1;
        for (int i = 0; i < n_toks; i++) {
            if (check(p, toks[i])) { matched = i; break; }
        }
        if (matched < 0) break;
        lexer_next(&p->lx);
        node_t *right = next_level(p);
        node_t *n = node_new(ND_BINOP);
        if (n) { n->op = ops[matched]; n->a = left; n->b = right; }
        left = n;
    }
    return left;
}

static node_t *parse_factor(parser_t *p) {
    static const token_type_t toks[] = { TOK_STAR, TOK_SLASH, TOK_PERCENT };
    static const op_t ops[] = { OP_MUL, OP_DIV, OP_MOD };
    return parse_binop_level(p, parse_unary, toks, ops, 3);
}
static node_t *parse_term(parser_t *p) {
    static const token_type_t toks[] = { TOK_PLUS, TOK_MINUS };
    static const op_t ops[] = { OP_ADD, OP_SUB };
    return parse_binop_level(p, parse_factor, toks, ops, 2);
}
static node_t *parse_comparison(parser_t *p) {
    static const token_type_t toks[] = { TOK_LT, TOK_GT, TOK_LE, TOK_GE };
    static const op_t ops[] = { OP_LT, OP_GT, OP_LE, OP_GE };
    return parse_binop_level(p, parse_term, toks, ops, 4);
}
static node_t *parse_equality(parser_t *p) {
    static const token_type_t toks[] = { TOK_EQ, TOK_NEQ };
    static const op_t ops[] = { OP_EQ, OP_NEQ };
    return parse_binop_level(p, parse_comparison, toks, ops, 2);
}
static node_t *parse_logic_and(parser_t *p) {
    static const token_type_t toks[] = { TOK_AND };
    static const op_t ops[] = { OP_AND };
    return parse_binop_level(p, parse_equality, toks, ops, 1);
}
static node_t *parse_logic_or(parser_t *p) {
    static const token_type_t toks[] = { TOK_OR };
    static const op_t ops[] = { OP_OR };
    return parse_binop_level(p, parse_logic_and, toks, ops, 1);
}
static node_t *parse_expr(parser_t *p) { return parse_logic_or(p); }

static node_t *parse_block(parser_t *p) {
    if (++p->depth > MAX_NEST) { templar_error("script nesting too deep"); p->depth--; return NULL; }
    expect(p, TOK_LBRACE, "expected '{'");
    node_t *blk = node_new(ND_BLOCK);
    node_t *head = NULL, *tail = NULL;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF) && !g_had_error) {
        node_t *s = parse_statement(p);
        if (!s) break;
        if (!head) head = tail = s; else { tail->next = s; tail = s; }
    }
    expect(p, TOK_RBRACE, "expected '}'");
    if (blk) blk->a = head;
    p->depth--;
    return blk;
}

static node_t *parse_app_decl(parser_t *p) {
    expect(p, TOK_KW_APP, "expected 'app'");
    node_t *n = node_new(ND_APPDECL);
    if (!check(p, TOK_STRING_LIT)) { templar_error("expected app name string after 'app'"); return NULL; }
    if (n) n->sval = p->lx.cur.sval;
    lexer_next(&p->lx);
    expect(p, TOK_LBRACE, "expected '{' to start app body");

    node_t *setup_head = NULL, *setup_tail = NULL;
    node_t *onkey_body = NULL;
    char *onkey_param = NULL;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF) && !g_had_error) {
        if (check(p, TOK_KW_ON_KEY)) {
            lexer_next(&p->lx);
            expect(p, TOK_LPAREN, "expected '(' after on_key");
            if (!check(p, TOK_IDENT)) { templar_error("expected a parameter name in on_key(...)"); break; }
            onkey_param = p->lx.cur.sval;
            lexer_next(&p->lx);
            expect(p, TOK_RPAREN, "expected ')' after on_key parameter");
            onkey_body = parse_block(p);
            continue;
        }
        node_t *s = parse_statement(p);
        if (!s) break;
        if (!setup_head) setup_head = setup_tail = s; else { setup_tail->next = s; setup_tail = s; }
    }
    expect(p, TOK_RBRACE, "expected '}' to close app body");

    if (n) {
        node_t *setup_blk = node_new(ND_BLOCK);
        if (setup_blk) setup_blk->a = setup_head;
        n->a = setup_blk;
        n->b = onkey_body;
        n->sval2 = onkey_param;
    }
    return n;
}

static node_t *parse_statement(parser_t *p) {
    if (g_had_error) return NULL;

    if (is_type_kw(p)) {
        val_type_t t = type_from_kw(p);
        if (!check(p, TOK_IDENT)) { templar_error("expected identifier after type"); return NULL; }
        node_t *n = node_new(ND_VARDECL);
        if (n) { n->vtype = t; n->sval = p->lx.cur.sval; }
        lexer_next(&p->lx);
        if (accept(p, TOK_ASSIGN)) { if (n) n->a = parse_expr(p); }
        expect(p, TOK_SEMI, "expected ';' after variable declaration");
        return n;
    }

    if (check(p, TOK_KW_IF)) {
        lexer_next(&p->lx);
        expect(p, TOK_LPAREN, "expected '(' after if");
        node_t *n = node_new(ND_IF);
        node_t *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')' after if condition");
        node_t *then_blk = parse_block(p);
        node_t *else_stmt = NULL;
        if (accept(p, TOK_KW_ELSE)) {
            if (check(p, TOK_KW_IF)) else_stmt = parse_statement(p);
            else else_stmt = parse_block(p);
        }
        if (n) { n->a = cond; n->b = then_blk; n->c = else_stmt; }
        return n;
    }

    if (check(p, TOK_KW_WHILE)) {
        lexer_next(&p->lx);
        expect(p, TOK_LPAREN, "expected '(' after while");
        node_t *n = node_new(ND_WHILE);
        node_t *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')' after while condition");
        node_t *body = parse_block(p);
        if (n) { n->a = cond; n->b = body; }
        return n;
    }

    if (check(p, TOK_KW_FUNC)) {
        lexer_next(&p->lx);
        if (!check(p, TOK_IDENT)) { templar_error("expected function name"); return NULL; }
        node_t *n = node_new(ND_FUNCDECL);
        if (n) n->sval = p->lx.cur.sval;
        lexer_next(&p->lx);
        expect(p, TOK_LPAREN, "expected '(' after function name");
        node_t *phead = NULL, *ptail = NULL;
        if (!check(p, TOK_RPAREN)) {
            for (;;) {
                val_type_t t = type_from_kw(p);
                if (!check(p, TOK_IDENT)) { templar_error("expected parameter name"); break; }
                node_t *param = node_new(ND_VARDECL);
                if (param) { param->vtype = t; param->sval = p->lx.cur.sval; }
                lexer_next(&p->lx);
                if (!phead) phead = ptail = param; else { ptail->next = param; ptail = param; }
                if (!accept(p, TOK_COMMA)) break;
            }
        }
        expect(p, TOK_RPAREN, "expected ')' after parameters");
        node_t *body = parse_block(p);
        if (n) { n->a = phead; n->b = body; }
        return n;
    }

    if (check(p, TOK_KW_APP)) return parse_app_decl(p);

    if (check(p, TOK_KW_RETURN)) {
        lexer_next(&p->lx);
        node_t *n = node_new(ND_RETURN);
        if (!check(p, TOK_SEMI)) { if (n) n->a = parse_expr(p); }
        expect(p, TOK_SEMI, "expected ';' after return");
        return n;
    }

    if (check(p, TOK_LBRACE)) return parse_block(p);

    if (check(p, TOK_IDENT)) {
        lexer_t save = p->lx;
        char *name = p->lx.cur.sval;
        lexer_next(&p->lx);
        if (check(p, TOK_ASSIGN)) {
            lexer_next(&p->lx);
            node_t *n = node_new(ND_ASSIGN);
            if (n) { n->sval = name; n->a = parse_expr(p); }
            expect(p, TOK_SEMI, "expected ';' after assignment");
            return n;
        }
        p->lx = save;
    }

    node_t *n = node_new(ND_EXPRSTMT);
    node_t *expr = parse_expr(p);
    if (n) n->a = expr;
    expect(p, TOK_SEMI, "expected ';' after expression");
    return n;
}

static node_t *parse_program(parser_t *p) {
    node_t *prog = node_new(ND_PROGRAM);
    node_t *head = NULL, *tail = NULL;
    while (!check(p, TOK_EOF) && !g_had_error) {
        node_t *s = parse_statement(p);
        if (!s) break;
        if (!head) head = tail = s; else { tail->next = s; tail = s; }
    }
    if (prog) prog->a = head;
    return prog;
}

typedef struct {
    val_type_t type;
    int ival;
    char *sval;
} value_t;

static value_t val_int(int i) { value_t v; v.type = VT_INT; v.ival = i; v.sval = NULL; return v; }
static value_t val_bool(int b) { value_t v; v.type = VT_BOOL; v.ival = b ? 1 : 0; v.sval = NULL; return v; }
static value_t val_str(char *s) { value_t v; v.type = VT_STRING; v.ival = 0; v.sval = s; return v; }
static value_t val_void(void) { value_t v; v.type = VT_VOID; v.ival = 0; v.sval = NULL; return v; }

static int truthy(value_t v) {
    if (v.type == VT_STRING) return v.sval && v.sval[0] != '\0';
    return v.ival != 0;
}

static int val_as_int(value_t v) { return (v.type == VT_STRING) ? 0 : v.ival; }

#define MAX_VARS_PER_SCOPE 32
#define MAX_SCOPES 24

typedef struct { char *name; value_t val; } var_t;
typedef struct { var_t vars[MAX_VARS_PER_SCOPE]; int count; } scope_t;

static scope_t g_scopes[MAX_SCOPES];
static int g_scope_top;

static void push_scope(void) {
    if (g_scope_top + 1 >= MAX_SCOPES) { templar_error("too much nesting / recursion"); return; }
    g_scope_top++;
    g_scopes[g_scope_top].count = 0;
}
static void pop_scope(void) {
    if (g_scope_top >= 0) g_scope_top--;
}

static void env_define(const char *name, value_t v) {
    if (g_scope_top < 0) { templar_error("internal error: no active scope"); return; }
    scope_t *sc = &g_scopes[g_scope_top];
    if (sc->count >= MAX_VARS_PER_SCOPE) { templar_error("too many variables in one scope"); return; }
    sc->vars[sc->count].name = (char *)name;
    sc->vars[sc->count].val = v;
    sc->count++;
}

static var_t *env_find(const char *name) {
    for (int s = g_scope_top; s >= 0; s--) {
        scope_t *sc = &g_scopes[s];
        for (int i = 0; i < sc->count; i++) {
            if (strcmp(sc->vars[i].name, name) == 0) return &sc->vars[i];
        }
    }
    return NULL;
}

static value_t env_get(const char *name) {
    var_t *v = env_find(name);
    if (!v) { templar_error("use of undeclared variable"); return val_void(); }
    return v->val;
}

static void env_set(const char *name, value_t val) {
    var_t *v = env_find(name);
    if (!v) { templar_error("assignment to undeclared variable"); return; }
    v->val = val;
}

#define MAX_FUNCS 16
typedef struct { char *name; node_t *decl; } func_entry_t;
static func_entry_t g_funcs[MAX_FUNCS];
static int g_func_count;

static node_t *find_func(const char *name) {
    for (int i = 0; i < g_func_count; i++) {
        if (strcmp(g_funcs[i].name, name) == 0) return g_funcs[i].decl;
    }
    return NULL;
}

static void register_func(node_t *decl) {
    if (g_func_count >= MAX_FUNCS) { templar_error("too many functions declared"); return; }
    g_funcs[g_func_count].name = decl->sval;
    g_funcs[g_func_count].decl = decl;
    g_func_count++;
}

static int g_return_flag;
static value_t g_return_value;

templar_key_handler_t g_active_key_handler = NULL;

static volatile int g_app_should_close;
static node_t *g_app_onkey_body;
static char g_app_onkey_param[32];

static void exec_stmt(node_t *n);
static void exec_block_body(node_t *blk);
static value_t eval_expr(node_t *n);

static void templar_app_key_dispatch(char c) {
    if (c == 27) { g_app_should_close = 1; return; }
    if (!g_app_onkey_body || g_had_error) return;
    push_scope();
    env_define(g_app_onkey_param[0] ? g_app_onkey_param : "k", val_int((int)(unsigned char)c));
    exec_block_body(g_app_onkey_body);
    pop_scope();
}

static void exec_app_decl(node_t *n) {
    if (g_had_error) return;

    vesa_clear(0x000000);
    vesa_swap();

    g_app_onkey_body = n->b;
    if (n->sval2) strncpy(g_app_onkey_param, n->sval2, sizeof(g_app_onkey_param) - 1);
    else g_app_onkey_param[0] = '\0';
    g_app_onkey_param[sizeof(g_app_onkey_param) - 1] = '\0';

    g_app_should_close = 0;
    templar_key_handler_t prev_handler = g_active_key_handler;
    g_active_key_handler = templar_app_key_dispatch;

    if (n->a) exec_stmt(n->a);

    while (!g_app_should_close && !g_had_error) {
        usb_poll_all();
        if (usb_kbd_dirty) usb_kbd_dirty = 0;
        asm volatile("pause");
    }

    g_active_key_handler = prev_handler;
    g_app_onkey_body = NULL;

    vesa_clear(0x000000);
    vesa_swap();
    klog_color("CRUSADER", 0xFFFF00);
    klog_color(g_current_path, 0xFFA500);
    klog_color(">> ", 0xFFFF00);
}

#define MAX_BUILTIN_ARGS 6

static void draw_text(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += 9; s++; continue; }
        vesa_draw_char(*s, cx, y, fg, bg);
        cx += 8;
        s++;
    }
}

static value_t bi_log(value_t *a, int n) {
    if (n >= 1 && a[0].type == VT_STRING) kklog(a[0].sval);
    return val_void();
}
static value_t bi_clear(value_t *a, int n) {
    vesa_clear(n >= 1 ? (uint32_t)val_as_int(a[0]) : 0x000000);
    return val_void();
}
static value_t bi_rect(value_t *a, int n) {
    if (n < 5) { templar_error("rect(x,y,w,h,color) needs 5 arguments"); return val_void(); }
    vesa_draw_rec(val_as_int(a[0]), val_as_int(a[1]), val_as_int(a[2]), val_as_int(a[3]), (uint32_t)val_as_int(a[4]));
    return val_void();
}
static value_t bi_line_h(value_t *a, int n) {
    if (n < 4) { templar_error("line_h(x,y,len,color) needs 4 arguments"); return val_void(); }
    vesa_draw_hor(val_as_int(a[0]), val_as_int(a[1]), val_as_int(a[2]), (uint32_t)val_as_int(a[3]));
    return val_void();
}
static value_t bi_line_v(value_t *a, int n) {
    if (n < 4) { templar_error("line_v(x,y,len,color) needs 4 arguments"); return val_void(); }
    vesa_draw_ver(val_as_int(a[0]), val_as_int(a[1]), val_as_int(a[2]), (uint32_t)val_as_int(a[3]));
    return val_void();
}
static value_t bi_text(value_t *a, int n) {
    if (n < 5 || a[2].type != VT_STRING) { templar_error("text(x,y,str,fg,bg) needs 5 arguments"); return val_void(); }
    draw_text(val_as_int(a[0]), val_as_int(a[1]), a[2].sval, (uint32_t)val_as_int(a[3]), (uint32_t)val_as_int(a[4]));
    return val_void();
}
static value_t bi_swap(value_t *a, int n) { (void)a; (void)n; vesa_swap(); return val_void(); }

static value_t bi_exit_app(value_t *a, int n) { (void)a; (void)n; g_app_should_close = 1; return val_void(); }

static value_t bi_reboot(value_t *a, int n) { (void)a; (void)n; reboot_triple_fault(); return val_void(); }

static value_t bi_sleep(value_t *a, int n) {
    int ms = (n >= 1) ? val_as_int(a[0]) : 0;
    if (ms < 0) ms = 0;
    volatile unsigned int iter = 8000u * (unsigned int)ms;
    for (volatile unsigned int i = 0; i < iter; i++) asm volatile("nop");
    return val_void();
}

#define TEMPLAR_FILE_BUF_SIZE 16384
static uint8_t g_file_buf[TEMPLAR_FILE_BUF_SIZE];

static int templar_read_file_raw(const char *filename, uint32_t *out_len) {
    if (g_fat32.mounted) {
        fat32_dirent_t entries[64];
        int n = fat32_list_dir(g_fat32.root_cluster, entries, 64);
        for (int i = 0; i < n; i++) {
            if (strcasecmp(entries[i].name, filename) == 0 && !(entries[i].attr & FAT32_ATTR_DIR)) {
                uint32_t to_read = entries[i].size;
                if (to_read > TEMPLAR_FILE_BUF_SIZE - 1) to_read = TEMPLAR_FILE_BUF_SIZE - 1;
                uint32_t got = fat32_read(&entries[i], 0, to_read, g_file_buf);
                *out_len = got;
                return 1;
            }
        }
        return 0;
    }
    inode_t dir;
    read_inode(g_current_dir, &dir);
    int inode_num = dir_lookup(&dir, filename);
    if (inode_num < 0) return 0;
    inode_t file_node;
    read_inode(inode_num, &file_node);
    uint32_t to_read = file_node.size;
    if (to_read > TEMPLAR_FILE_BUF_SIZE - 1) to_read = TEMPLAR_FILE_BUF_SIZE - 1;
    int got = fs_read((uint32_t)inode_num, &file_node, 0, to_read, g_file_buf);
    if (got < 0) return 0;
    *out_len = (uint32_t)got;
    return 1;
}

static value_t bi_read(value_t *a, int n) {
    if (n < 1 || a[0].type != VT_STRING) { templar_error("read(filename) needs a string argument"); return val_str(""); }
    uint32_t len = 0;
    if (!templar_read_file_raw(a[0].sval, &len)) return val_str("");
    char *out = arena_strndup((char *)g_file_buf, len);
    return val_str(out ? out : (char *)"");
}

static value_t bi_write(value_t *a, int n) {
    if (n < 2 || a[0].type != VT_STRING || a[1].type != VT_STRING) {
        templar_error("write(filename, data) needs two string arguments");
        return val_bool(0);
    }
    if (g_fat32.mounted) {
        int rc = fat32_write_file(g_fat32.root_cluster, a[0].sval, (const uint8_t *)a[1].sval, (uint32_t)strlen(a[1].sval));
        return val_bool(rc == 0);
    }
    uint32_t inode_idx = fs_create_file(a[0].sval, "tpl");
    if (inode_idx == 0) return val_bool(0);
    int rc = fs_write(inode_idx, 0, (const uint8_t *)a[1].sval, strlen(a[1].sval));
    return val_bool(rc >= 0);
}

typedef struct { const char *name; value_t (*fn)(value_t *args, int argc); } builtin_entry_t;
static const builtin_entry_t g_builtins[] = {
    { "log", bi_log },
    { "clear", bi_clear },
    { "rect", bi_rect },
    { "line_h", bi_line_h },
    { "line_v", bi_line_v },
    { "text", bi_text },
    { "swap", bi_swap },
    { "exit_app", bi_exit_app },
    { "reboot", bi_reboot },
    { "sleep", bi_sleep },
    { "read", bi_read },
    { "write", bi_write },
};
static const int g_builtin_count = sizeof(g_builtins) / sizeof(g_builtins[0]);

static value_t call_user_func(node_t *decl, value_t *args, int argc) {
    push_scope();
    int i = 0;
    for (node_t *param = decl->a; param; param = param->next, i++) {
        value_t v = (i < argc) ? args[i] : val_void();
        env_define(param->sval, v);
    }
    g_return_flag = 0;
    g_return_value = val_void();
    exec_stmt(decl->b);
    g_return_flag = 0;
    pop_scope();
    return g_return_value;
}

static value_t eval_expr(node_t *n) {
    if (!n || g_had_error) return val_void();
    switch (n->kind) {
        case ND_LIT_INT: return val_int(n->ival);
        case ND_LIT_STR: return val_str(n->sval);
        case ND_LIT_BOOL: return val_bool(n->ival);
        case ND_IDENT: return env_get(n->sval);

        case ND_UNOP: {
            value_t a = eval_expr(n->a);
            if (n->op == OP_NEG) return val_int(-val_as_int(a));
            if (n->op == OP_NOT) return val_bool(!truthy(a));
            return val_void();
        }

        case ND_BINOP: {
            if (n->op == OP_AND) {
                value_t a = eval_expr(n->a);
                if (!truthy(a)) return val_bool(0);
                return val_bool(truthy(eval_expr(n->b)));
            }
            if (n->op == OP_OR) {
                value_t a = eval_expr(n->a);
                if (truthy(a)) return val_bool(1);
                return val_bool(truthy(eval_expr(n->b)));
            }
            value_t a = eval_expr(n->a);
            value_t b = eval_expr(n->b);
            if (n->op == OP_ADD && (a.type == VT_STRING || b.type == VT_STRING)) {
                char abuf[16], bbuf[16];
                const char *as = (a.type == VT_STRING) ? a.sval : itoa(a.ival, abuf, 10);
                const char *bs = (b.type == VT_STRING) ? b.sval : itoa(b.ival, bbuf, 10);
                uint32_t total = (uint32_t)strlen(as) + (uint32_t)strlen(bs);
                char *out = (char *)arena_alloc(total + 1);
                if (!out) { templar_error("out of templar memory during string concat"); return val_str((char*)""); }
                uint32_t i = 0;
                for (const char *p = as; *p; p++) out[i++] = *p;
                for (const char *p = bs; *p; p++) out[i++] = *p;
                out[i] = '\0';
                return val_str(out);
            }
            int ai = val_as_int(a), bi = val_as_int(b);
            switch (n->op) {
                case OP_ADD: return val_int(ai + bi);
                case OP_SUB: return val_int(ai - bi);
                case OP_MUL: return val_int(ai * bi);
                case OP_DIV: return val_int(bi != 0 ? ai / bi : 0);
                case OP_MOD: return val_int(bi != 0 ? ai % bi : 0);
                case OP_EQ:  return val_bool((a.type == VT_STRING && b.type == VT_STRING) ? (strcmp(a.sval, b.sval) == 0) : (ai == bi));
                case OP_NEQ: return val_bool((a.type == VT_STRING && b.type == VT_STRING) ? (strcmp(a.sval, b.sval) != 0) : (ai != bi));
                case OP_LT:  return val_bool(ai < bi);
                case OP_GT:  return val_bool(ai > bi);
                case OP_LE:  return val_bool(ai <= bi);
                case OP_GE:  return val_bool(ai >= bi);
                default: return val_void();
            }
        }

        case ND_CALL: {
            value_t args[MAX_BUILTIN_ARGS];
            int argc = 0;
            for (node_t *arg = n->a; arg; arg = arg->next) {
                if (argc < MAX_BUILTIN_ARGS) args[argc++] = eval_expr(arg);
                else eval_expr(arg);
            }
            for (int i = 0; i < g_builtin_count; i++) {
                if (strcmp(g_builtins[i].name, n->sval) == 0) return g_builtins[i].fn(args, argc);
            }
            node_t *decl = find_func(n->sval);
            if (decl) return call_user_func(decl, args, argc);
            templar_error("call to unknown function");
            return val_void();
        }

        default:
            templar_error("internal error: not an expression node");
            return val_void();
    }
}

static void exec_block_body(node_t *blk) {
    push_scope();
    for (node_t *s = blk->a; s && !g_had_error; s = s->next) {
        exec_stmt(s);
        if (g_return_flag) break;
    }
    pop_scope();
}

static void exec_stmt(node_t *n) {
    if (!n || g_had_error || g_return_flag) return;
    switch (n->kind) {
        case ND_BLOCK:
            exec_block_body(n);
            return;
        case ND_VARDECL: {
            value_t v;
            if (n->a) v = eval_expr(n->a);
            else v = (n->vtype == VT_STRING) ? val_str((char *)"") : (n->vtype == VT_BOOL) ? val_bool(0) : val_int(0);
            env_define(n->sval, v);
            return;
        }
        case ND_ASSIGN:
            env_set(n->sval, eval_expr(n->a));
            return;
        case ND_IF:
            if (truthy(eval_expr(n->a))) exec_stmt(n->b);
            else if (n->c) exec_stmt(n->c);
            return;
        case ND_WHILE: {
            int guard = 0;
            while (!g_had_error && truthy(eval_expr(n->a))) {
                exec_stmt(n->b);
                if (g_return_flag) break;
                if (++guard > 200000) { templar_error("while loop exceeded iteration limit"); break; }
            }
            return;
        }
        case ND_FUNCDECL:
            return;
        case ND_RETURN:
            g_return_value = n->a ? eval_expr(n->a) : val_void();
            g_return_flag = 1;
            return;
        case ND_EXPRSTMT:
            eval_expr(n->a);
            return;
        case ND_APPDECL:
            exec_app_decl(n);
            return;
        default:
            templar_error("internal error: not a statement node");
            return;
    }
}

void templar_init(void) {
    g_active_key_handler = terminal_key;
}

void templar_run_source(const char *src) {
    arena_reset();
    g_had_error = 0;
    g_error_msg[0] = '\0';
    g_scope_top = -1;
    g_func_count = 0;
    g_return_flag = 0;
    g_app_should_close = 0;
    g_app_onkey_body = NULL;

    parser_t p;
    lexer_init(&p.lx, src);
    p.depth = 0;
    node_t *program = parse_program(&p);

    if (g_had_error || !program) {
        klog_status(g_had_error ? "TEMPLAR PARSE ERROR" : "TEMPLAR OUT OF MEMORY", 0xFF0000);
        if (g_had_error) kklog(g_error_msg);
        return;
    }

    push_scope();
    for (node_t *s = program->a; s; s = s->next) {
        if (s->kind == ND_FUNCDECL) register_func(s);
    }
    for (node_t *s = program->a; s && !g_had_error; s = s->next) {
        if (s->kind == ND_FUNCDECL) continue;
        exec_stmt(s);
        if (g_return_flag) { g_return_flag = 0; break; }
    }
    pop_scope();

    if (g_had_error) {
        klog_status("TEMPLAR RUNTIME ERROR", 0xFF0000);
        kklog(g_error_msg);
    }
}

void templar_run_file(const char *filename) {
    uint32_t len = 0;
    if (!templar_read_file_raw(filename, &len)) {
        klog_status("ERROR FILE NOT FOUND", 0xFF0000);
        return;
    }
    g_file_buf[len < TEMPLAR_FILE_BUF_SIZE ? len : TEMPLAR_FILE_BUF_SIZE - 1] = '\0';
    templar_run_source((const char *)g_file_buf);
}