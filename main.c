/*************************************************************************
 > File Name: tcc.c
 > Author: TTc
 > Mail: liutianshxkernel@gmail.com
 > Created Time: 三 10/19 22:43:55 2016
 ************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "tcc.h"

#define EXPR_LEN 100
#define MAX_ARGS 6

enum {
    AST_LITERAL,
    AST_VAR,
    AST_FUNCALL,
    AST_DECL,
    AST_ADDR,
    AST_DEREF,
};

enum {
    CTYPE_VOID,
    CTYPE_INT,
    CTYPE_CHAR,
    CTYPE_ARRAY,
    CTYPE_PTR, //pointer
};

typedef struct Ctype {
    int type;
    struct Ctype *ptr;
} Ctype;

typedef struct Ast {
    char type;
    Ctype *ctype;
    union {
        int ival;// Integer
        char c;  // Char
        struct { // String
            char *sval;
            int sid;
            struct Ast *snext;
        };
        
        struct {  // Variable
            char *vname;
            int vpos;
            struct Ast *vnext;
        };
        
        struct { // Binary operator
            struct Ast *left;
            struct Ast *right;
        };
        
        struct {  //Unary operator
            struct Ast *operand;
        };
        
        struct {  // Function call
            char *fname;
            int nargs;
            struct Ast **args;
        };
        
        struct {   //decl
            struct Ast * decl_var;
            struct Ast *decl_init;
        };
    };
} Ast;

static Ast *vars = NULL;
static Ast *strings = NULL;
static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

static Ctype *ctype_int = &(Ctype) { CTYPE_INT, NULL };
static Ctype *ctype_char = &(Ctype) { CTYPE_INT, NULL };
static Ctype *ctype_array = &(Ctype) { CTYPE_ARRAY,  &(Ctype){ CTYPE_CHAR, NULL } };

static void emit_expr(Ast *ast);
static Ast  *read_expr(int prec);
static char *ast_to_string(Ast *ast);
static char *ctype_to_string(Ctype *ctype);

static Ast * read_prim(void);

static Ast *
make_ast_uop(char type, Ctype *ctype, Ast *operand) {
    Ast *r = malloc(sizeof(Ast));
    r->type = type;
    r->ctype = ctype;
    r->operand = operand;
    return r;
}

static Ast *
make_ast_binop(char type, Ctype *ctype, Ast *left, Ast *right) {
    Ast *r = malloc(sizeof(Ast));
    r->type = type;
    r->ctype = ctype;
    r->left = left;
    r->right = right;
    return r;
}

static Ast *
make_ast_int(int val) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype_int;
    r->ival = val;
    return r;
}

static Ast *
make_ast_char(char c) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype_char;
    r->c = c;
    return r;
}

static Ast *
make_ast_var(Ctype *ctype,char *vname) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_VAR;
    r->ctype = ctype;
    r->vname = vname;
    r->vpos = vars ? vars->vpos + 1 : 1;
    r->vnext = vars;
    vars = r;
    return r;
}

static Ast *
make_ast_string(char *str) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype_array;
    r->sval = str;
    if (strings == NULL) {
        r->sid = 0;
        r->snext = NULL;
    } else {
        r->sid = strings->sid + 1;
        r->snext = strings;
    }
    strings = r;
    return r;
}

static Ast *
make_ast_funcall(char *fname, int nargs, Ast **args) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_FUNCALL;
    r->ctype = ctype_int;
    r->fname = fname;
    r->nargs = nargs;
    r->args = args;
    return r;
}

static Ast *
make_ast_decl(Ast *var, Ast *init) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_DECL;
    r->ctype = NULL;
    r->decl_var = var;
    r->decl_init = init;
    return r;
}

static Ctype *
make_ptr_type(Ctype *ctype) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_PTR;
    r->ptr = ctype;
    return r;
}

static Ast *
find_var(char *name) {
    for (Ast *p = vars; p; p = p->vnext) {
        if (!strcmp(name, p->vname))
            return p;
    }
    return NULL;
}

static bool
is_right_assoc(char op) {
    return op == '=';
}

static int
priority(char op) {
    switch (op) {
        case '=':
            return 1;
        case '+': case '-':
            return 2;
        case '*': case '/':
            return 3;
        default:
            return -1;
    }
}


#define swap(a,b) \
{typeof (a) tmp = b; b= a; a=tmp;}

static Ctype *
result_type_int(jmp_buf *jmpbuf, char op, Ctype *a, Ctype *b) {
    if (a->type == CTYPE_PTR) {
        swap(a, b);
    }
    
    if (b->type == CTYPE_PTR) {
        if (op != '+' && op != '-') {
            goto err;
        }
        if (a->type != CTYPE_PTR) {
            warn("Making a pointer from %s", ctype_to_string(a));
            return b;
        }
        Ctype *r = malloc(sizeof(Ctype));
        r->type = CTYPE_PTR;
        r->ptr = result_type_int(jmpbuf, op, a->ptr, b->ptr);
        return r;
    }
    
    switch (a->type) {
        case CTYPE_VOID:
            goto err;
        case CTYPE_INT:
        case CTYPE_CHAR:
            return ctype_int;
        case CTYPE_ARRAY:
            return result_type_int(jmpbuf, op, make_ptr_type(a->ptr), b);
        default:
            error("internal error");
    }
err:
    longjmp(*jmpbuf, 1);
}

static Ctype *
result_type(char op, Ast *a, Ast *b) {
    jmp_buf jmpbuf;
    if (setjmp(jmpbuf) == 0)
        return result_type_int(&jmpbuf, op, a->ctype, b->ctype);
    error("incompatible operands: %c: <%s> and <%s>",
          op, ast_to_string(a), ast_to_string(b));
}


static Ast *
read_func_args(char *fname) {
    Ast **args = malloc(sizeof(Ast*) * (MAX_ARGS + 1));
    int i = 0, nargs = 0;
    for (; i < MAX_ARGS; i++) {
        Token *tok = read_token();
        if (is_punct(tok, ')')) break;
        unget_token(tok);
        args[i] = read_expr(0);
        nargs++;
        tok = read_token();
        if (is_punct(tok, ')')) break;
        if (!is_punct(tok, ','))
            error("Unexpected token: '%s'", token_to_string(tok));
    }
    if (i == MAX_ARGS)
        error("Too many arguments: %s", fname);
    return make_ast_funcall(fname, nargs, args);
}

static void
ensure_lvalue(Ast *ast) {
    if (ast->type != AST_VAR)
        error("lvalue expected, but got %s", ast_to_string(ast));
}

static Ast *
read_unary_expr(void) {
    Token *tok = read_token();
    if (is_punct(tok, '&')) {
        Ast *operand = read_unary_expr();
        ensure_lvalue(operand);
        return make_ast_uop(AST_ADDR, make_ptr_type(operand->ctype), operand);
    }
    if (is_punct(tok, '*')) {
        Ast *operand = read_unary_expr();
        if (operand->ctype->type != CTYPE_PTR)
            error("pointer type expected, but got %s", ast_to_string(operand));
        return make_ast_uop(AST_DEREF, operand->ctype->ptr, operand);
    }
    unget_token(tok);
    return read_prim();
}


static Ast *
read_ident_or_func(char *name) {
    Token *tok = read_token();
    if (is_punct(tok, '('))
        return read_func_args(name);
    unget_token(tok);
    Ast *v = find_var(name);
    if (!v) error("Undefined Variable:%s",name);
    return v;
}

static Ast *
read_prim(void) {
    Token *tok = read_token();
    if (!tok) return NULL;
    switch (tok->type) {
        case TTYPE_IDENT:
            return read_ident_or_func(tok->sval);
        case TTYPE_INT:
            return make_ast_int(tok->ival);
        case TTYPE_CHAR:
            return make_ast_char(tok->c);
        case TTYPE_STRING:
            return make_ast_string(tok->sval);
        case TTYPE_PUNCT:
            error("unexpected character: '%c'", tok->punct);
        default:
            error("internal error: unknown token type: %d", tok->type);
    }
}


static Ast *
read_expr(int prec) {
    Ast *ast = read_unary_expr();
    if (!ast) return NULL;
    for(;;) {
        Token *tok = read_token();
        if (tok->type != TTYPE_PUNCT) {
            unget_token(tok);
            return ast;
        }
        int prec2 = priority(tok->punct);
        if (prec2 < 0 || prec2 < prec) {
            unget_token(tok);
            return ast;
        }
        if (is_punct(tok,'=')) {
            ensure_lvalue(ast);
        }
        Ast *rest = read_expr(prec2 +(is_right_assoc(tok->punct) ? 0 : 1));
        Ctype *ctype = result_type(tok->punct,ast,rest);
        if (ctype->type == CTYPE_PTR && ast->ctype->type != CTYPE_PTR)
            swap(ast, rest);
        ast = make_ast_binop(tok->punct,ctype,ast,rest);
    }
}

static Ctype *
get_ctype(Token *tok) {
    if (tok->type != TTYPE_IDENT){
        return NULL;
    }
    if (!strcmp(tok->sval,"int")) {
        return ctype_int;
    }
    if (!strcmp(tok->sval,"char")) {
        return ctype_char;
    }
    return NULL;
}

static bool
is_type_keyword(Token *tok) {
    return get_ctype(tok) != NULL;
}

static void
expect(char punct) {
    Token *tok = read_token();
    if (!is_punct(tok,punct)) {
        error("'%c' expected,but got %s",punct,token_to_string(tok));
    }
}

static Ast *
read_decl(void) {
    Ctype *ctype = get_ctype(read_token());
    Token *tok;
    for (;;) {
        tok = read_token();
        if (!is_punct(tok, '*')) break;
        ctype = make_ptr_type(ctype);
    }
    if (tok->type != TTYPE_IDENT)
        error("Identifier expected, but got %s", token_to_string(tok));
    Ast *var = make_ast_var(ctype,tok->sval);
    expect('=');
    Ast *init = read_expr(0);
    return make_ast_decl(var,init);
}

static Ast *
read_decl_or_stmt(void) {
    Token *tok = peek_token();
    if (!tok) return NULL;
    Ast *r = is_type_keyword(tok)? read_decl() : read_expr(0);
    tok = read_token();
    if (!is_punct(tok,';')) {
        error("Unterminated expression :%s",token_to_string(tok));
    }
    return r;
}

static void
emit_assign(Ast *var, Ast *value) {
    emit_expr(value);
    printf("mov %%rax, -%d(%%rbp)\n\t", var->vpos * 8);
}

static int
ctype_shift(Ctype *ctype) {
    switch (ctype->type) {
        case CTYPE_CHAR: return 0;
        case CTYPE_INT: return 2;
        default: return 3;
    }
}

static int
ctype_size(Ctype *ctype) {
    return 1 << ctype_shift(ctype);
}

static void
emit_pointer_arith(char op, Ast *left, Ast *right) {
    assert(left->ctype->type == CTYPE_PTR);
    emit_expr(left);
    printf("push %%rax\n\t");
    emit_expr(right);
    int shift = ctype_shift(left->ctype);
    if (shift > 0)
        printf("sal $%d, %%rax\n\t", shift);
    printf("mov %%rax, %%rbx\n\t"
           "pop %%rax\n\t"
           "add %%rbx, %%rax\n\t");
}


static void
emit_binop(Ast *ast) {
    if (ast->type == '=') {
        emit_assign(ast->left,ast->right);
        return;
    }
    
    if (ast->ctype->type == CTYPE_PTR) {
        emit_pointer_arith(ast->type, ast->left, ast->right);
        return;
    }
    char *op;
    switch (ast->type) {
        case '+': op = "add"; break;
        case '-': op = "sub"; break;
        case '*': op = "imul"; break;
        case '/': break;
        default: error("invalid operator '%c'", ast->type);
    }
    emit_expr(ast->left);
    printf("push %%rax\n\t");
    emit_expr(ast->right);
    if (ast->type == '/') {
        printf("mov %%rax, %%rbx\n\t");
        printf("pop %%rax\n\t");
        printf("mov $0, %%edx\n\t");
        printf("idiv %%rbx\n\t");
    } else {
        printf("pop %%rbx\n\t");
        printf("%s %%rbx, %%rax\n\t", op);
    }
}

static void
emit_expr(Ast *ast) {
    switch (ast->type) {
        case AST_LITERAL:
            switch (ast->ctype->type) {
                case CTYPE_INT:
                    printf("mov $%d, %%eax\n\t", ast->ival);
                    break;
                case CTYPE_CHAR:
                    printf("mov $%d, %%rax\n\t", ast->c);
                    break;
                case CTYPE_ARRAY:
                    printf("lea .s%d(%%rip), %%rax\n\t", ast->sid);
                    break;
                default:
                    error("internal error");
            }
            break;
        case AST_VAR:
            switch (ctype_size(ast->ctype)) {
                case 1:
                    printf("mov $0, %%eax\n\t");
                    printf("mov -%d(%%rbp), %%al\n\t", ast->vpos * 8);
                    break;
                case 4:
                    printf("mov -%d(%%rbp), %%eax\n\t", ast->vpos * 8);
                    break;
                case 8:
                    printf("mov -%d(%%rbp), %%rax\n\t", ast->vpos * 8);
                    break;
                default:
                    error("internal error");
            }
            break;
            
        case AST_FUNCALL:
            for (int i = 1; i < ast->nargs; i++)
                printf("push %%%s\n\t", REGS[i]);
            for (int i = 0; i < ast->nargs; i++) {
                emit_expr(ast->args[i]);
                printf("push %%rax\n\t");
            }
            for (int i = ast->nargs - 1; i >= 0; i--)
                printf("pop %%%s\n\t", REGS[i]);
            printf("mov $0, %%eax\n\t");
            printf("call %s\n\t", ast->fname);
            for (int i = ast->nargs - 1; i > 0; i--)
                printf("pop %%%s\n\t", REGS[i]);
            break;
        case AST_DECL:
            emit_assign(ast->decl_var,ast->decl_init);
            break;
        case AST_ADDR:
            assert(ast->operand->type == AST_VAR);
            printf("lea -%d(%%rbp), %%rax\n\t", ast->operand->vpos * 8);
            break;
        case AST_DEREF:
            assert(ast->operand->ctype->type == CTYPE_PTR);
            emit_expr(ast->operand);
            char *reg;
            switch (ctype_size(ast->ctype)) {
                case 1: reg = "%bl";  break;
                case 4: reg = "%ebx"; break;
                case 8: reg = "%rbx"; break;
                default: error("internal error");
            }
            printf("mov $0, %%ebx\n\t");
            printf("mov (%%rax), %s\n\t", reg);
            printf("mov %%rbx, %%rax\n\t");
            break;
        default:
            emit_binop(ast);
    }
}

static char *
quote(char *p) {
    String *s = make_string();
    while (*p) {
        if (*p == '\"' || *p == '\\')
            string_append(s,'\\');
        string_append(s, *p);
        p++;
    }
    return get_cstring(s);
}

static char *
ctype_to_string(Ctype *ctype) {
    switch(ctype->type) {
        case CTYPE_VOID: return "void";
        case CTYPE_INT: return "int";
        case CTYPE_CHAR: return "char";
        case CTYPE_PTR: {
            String *s = make_string();
            string_appendf(s, "%s",ctype_to_string(ctype->ptr));
            string_append(s, '*');
            return get_cstring(s);
        }
        case CTYPE_ARRAY: {
            String *s = make_string();
            string_appendf(s, "%s", ctype_to_string(ctype->ptr));
            string_appendf(s, "[]");
            return get_cstring(s);
        }
        default: error("unknown ctype: %d", ctype);
    }
}

static void
ast_to_string_int(Ast *ast,String *buf) {
    switch (ast->type) {
        case AST_LITERAL:
            switch (ast->ctype->type) {
                case CTYPE_INT:
                    string_appendf(buf,"%d", ast->ival);
                    break;
                case CTYPE_CHAR:
                    string_appendf(buf,"'%c'", ast->c);
                    break;
                case CTYPE_ARRAY:
                    string_appendf(buf, "\"%s\"", quote(ast->sval));
                    break;
                default:
                    error("internal error");
            }
        case AST_VAR:
            string_appendf(buf, "%s", ast->vname);
            break;
        case AST_FUNCALL:
            string_appendf(buf, "%s(", ast->fname);
            for (int i = 0; ast->args[i]; i++) {
                string_appendf(buf, "%s", ast_to_string(ast->args[i]));
                if (ast->args[i + 1]) {
                    string_appendf(buf, ",");
                }
            }
            string_appendf(buf, ")");
            break;
        case AST_DECL:
            string_appendf(buf,"(decl %s %s %s)",
                           ctype_to_string(ast->decl_var->ctype),
                           ast->decl_var->vname,ast_to_string(ast->decl_init));
            break;
        case AST_ADDR:
            string_appendf(buf, "(& %s)", ast_to_string(ast->operand));
            break;
        case AST_DEREF:
            string_appendf(buf, "(* %s)",ast_to_string(ast->operand));
            break;
        default: {
            char *left = ast_to_string(ast->left);
            char *right = ast_to_string(ast->right);
            string_appendf(buf, "(%c %s %s)", ast->type, left, right);
        }
    }
}

static char *
ast_to_string(Ast *ast) {
    String *s = make_string();
    ast_to_string_int(ast, s);
    return get_cstring(s);
}

static void
emit_data_section(void) {
    if (!strings) return;
    printf("\t.data\n");
    for (Ast *p = strings; p; p = p->snext) {
        printf(".s%d:\n\t", p->sid);
        printf(".string \"%s\"\n", quote(p->sval));
    }
    printf("\t");
}

int
main(int argc, char **argv) {
    int wantast = (argc > 1 && !strcmp(argv[1], "-a"));
    Ast *exprs[EXPR_LEN];
    int i;
    for (i = 0; i < EXPR_LEN; i++) {
        Ast *t = read_decl_or_stmt();
        if (!t) break;
        exprs[i] = t;
    }
    int nexpr = i;
    if (!wantast) {
        emit_data_section();
        printf(".text\n\t"
               ".global mymain\n"
               "mymain:\n\t"
               "push %%rbp\n\t"
               "mov %%rsp, %%rbp\n\t");
        
        if (vars) printf("sub $%d, %%rsp\n\t", vars->vpos * 8);
    }
    
    for (i = 0; i < nexpr; i++) {
        if (wantast)
            printf("%s", ast_to_string(exprs[i]));
        else
            emit_expr(exprs[i]);
    }
    if (!wantast)
        printf("leave\n\t"
               "ret\n");
    return 0;
}
