//
//  main.c
//  Tc
//
//  Created by TTc on 16/12/4.
//  Copyright © 2016年 TTc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int debug;     /* print the executed instructions */
int assembly;  /* print out the assembly and source */

/* tokens and classes (operators last and precedence order) */
enum {
    Num = 128, Fun, Sys, Glo, Loc, Id,
    Char, Else, Enum, If, Int, Return, Sizeof, While,
    Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le,
    Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Break
};

int *current_id;   /* current parse ID */
int *symbols;      /* symbol table */
int token;         /* current token */
int token_val;     /* token value */
int line;          /* line number */

char *src, *old_src = NULL;  /* pointer to source code string */
int poolsize;      /* default size of text/data/stack */

int *text,*stack;  /*text segment & stack */
int *old_text;     /* for dump text sgment */
int *idmain;
char *data;        /* data segment */

int basetype;      /* the type of a declaration, make it global for convenience */
int expr_type;     /* the type of an expression */
int index_of_bp;   /* index of bp pointer on stack */

int *pc, *bp, *sp, ax, cycle; /* virtual machine registers */

/* instructions */
enum {
    LEA,IMM,JMP,CALL,JZ,JNZ,ENT,ADJ,LEV,LI,LC,SI ,SC ,PUSH,
    OR,XOR,AND,EQ,NE,LT,GT,LE,GE ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
    OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT
};

/* field of identifier */
enum {
    Token, Hash, Name, Type, Class, Value,
    BType, BClass, BValue, IdSize
};

/* type of variable / function */
enum {
    CHAR,INT,PTR
};

int expr();



/* 用于词法分析，获取下一个标记，它将自动忽略空白字符。*/
void
next() {
    
    char *last_pos;
    int hash;

    while (token == *src) {
        ++src;
        
        if (token == '\n') {
            if (assembly) { /* print compile infomation*/
                old_src = src;
                
                while (old_text < text) {
                    printf("%8.4s", & "LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
                           "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                           "OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT"[*++old_text * 5]);
                    
                    if (*old_text <= ADJ)
                    printf(" %d\n", *++old_text);
                    else
                    printf("\n");
                }
            }
            
            ++line;
        }
         /*不支持宏定义,skip */
        else if (token == '#') {
            while (*src != 0 && *src != '\n') {
                src++;
            }
        }
        /*标识符与符号表*/
        /*
        Symbol table:
        ----+-----+----+----+----+-----+-----+-----+------+------+----
         .. |token|hash|name|type|class|value|btype|bclass|bvalue| ..
        ----+-----+----+----+----+-----+-----+-----+------+------+----
        |<---      one single identifier                --->|
         */
        else if ((token >= 'a' && token <= 'z') ||
                 (token >= 'A' && token <= 'Z') ||
                 token == '_') {
            /* parse identifier*/
            last_pos = src - 1;
            hash = token;
            
            while ((*src >= 'a' && *src <= 'z') ||
                   (*src >= 'A' && *src <= 'Z') ||
                   (*src >= '0' && *src <= '9') ||
                   (*src == '_')) {
                
                hash = hash * 147 + *src;
                src++;
            }
            /* look for existing identifier, liner search */
            current_id = symbols;
            
            while (current_id[Token]) {
                if (current_id[Hash] == hash &&
                    !memcmp((char *)current_id[Name],
                            last_pos, src - last_pos)) {
                        token = current_id[Token];
                        return; //found one
                }
                current_id = current_id + IdSize;
            }
            
            /* store new ID */
            current_id[Name] = (int)last_pos;
            current_id[Hash] = hash;
            token = current_id[Token] = Id;
            return;
        }
        /* 数字, 需要支持 十进制 、十六进制、八进制 three kinds : dec(123) hex(0x123) oct(017)*/
        else if (token >= '0' && token <= '9') {
            /* 在ASCII码中，字符a对应的十六进制值是 61, A是41，故通过 (token & 15) 可以得到个位数的值 */
            token_val = token - '0';
            if (token_val >0) {
                /* dec, starts with [1-9]*/
                while (*src >= '0' && *src <= '9') {
                    token_val = token_val * 10 + *src -'0';
                }
            } else {
                /* starts with number 0; Hex*/
                if (*src == 'x' || *src == 'X') {
                    token = *++src;
                    while ((token >= '0' && token <= '9') ||
                           (token >= 'a' && token <= 'f') ||
                            (token >= 'A' && token <= 'F') ) {
                        token_val = token_val *16 + (token & 15)
                                +(token >= 'A' ? 9 : 0);
                    }
                } else {
                    /* OCT */
                    while (*src >= '0' && *src <= '7') {
                        token_val = token_val * 8 + *src++ - '0';
                    }
                }
            }
            token = Num;
            return;
        }
        /* 只支持 // 类型的注释;  */
        else if (token == '/') {
            if (*src == '/') {
                /* skip comments */
                while (*src != 0 && *src != '\n') {
                    ++src;
                }
            } else {
                /* lookahead ; divide operator */
                token = Div;
                return;
            }
        }
        /* 向前看一个字符来确定真正的标记 */
        /* parse string literal ,currently,the only suppored escape
          character is '\n', store the string literal into data */
        else if (token == '"' || token == '\'') {
            last_pos = data;
            while (*src != 0 && *src != token) {
                token_val = *src++;
                if (token_val == '\\') {
                    /* 转义字符 */
                    token_val = *src++;
                    if (token_val == 'n')
                        token_val = '\n';
                    
                }
                
                if (token_val == '"')
                    *data++ = token_val;
            }
            
            src++;
            /* if it is a single character ,return Num token */
            if (token == '"')
                token_val = (int)last_pos;
            else
                token = Num;
            return;
        }
        /* parse  '=='  and  '='*/
        else if (token == '=') {
            if (*src == '=') {
                src++;
                token = Eq;
            } else {
                token = Assign;
            }
            return;
        }
        /* parse '+' and '++' */
        else if (token == '+') {
            if (*src == '+') {
                src++;
                token = Inc;
            } else {
                token = Add;
            }
            return;
        }
        /* parse '-' and '--' */
        else if (token == '-') {
            if (*src == '-') {
                src++;
                token = Dec;
            } else {
                token = Sub;
            }
            return;
        }
        /* parse '!' and '!=' */
        else if (token == '!') {
            if (*src == '=') {
                src++;
                token = Ne;
            }
            return;
        }
        /* parse '<=', '<<' or '<' */
        else if (token == '<') {
            if (*src == '=') {
                src ++;
                token = Le;
            } else if (*src == '<') {
                src ++;
                token = Shl;
            } else {
                token = Lt;
            }
            return;
        }
        /* parse '>=', '>>' or '>' */
        else if (token == '>') {
            if (*src == '=') {
                src ++;
                token = Ge;
            } else if (*src == '>') {
                src ++;
                token = Shr;
            } else {
                token = Gt;
            }
            return;
        }
        /* parse '|' or '||' */
        else if (token == '|') {
            if (*src == '|') {
                src ++;
                token = Lor;
            } else {
                token = Or;
            }
            return;
        }
        /* parse '&' and '&&' */
        else if (token == '&') {
            if (*src == '&') {
                src ++;
                token = Lan;
            } else {
                token = And;
            }
            return;
        }
        else if (token == '^') {
            token = Xor;
            return;
        }
        else if (token == '%') {
            token = Mod;
            return;
        }
        else if (token == '*') {
            token = Mul;
            return;
        }
        else if (token == '[') {
            token = Break;
            return;
        }
        else if (token == '?') {
            token = Cond;
            return;
        }
        /* directly return the character as token;*/
        else if (token == '~' || token == ';'
                 || token == '{' || token == '}'
                 || token == '(' || token == ')'
                 || token == ']' || token == ','
                 || token == ':') {
            return;
        }


    }
}

void
match(int tk) {

    if (token != tk) {
        printf("%d: expected token: %d\n", line, tk);
        exit(-1);
    }
    next();
}


int
factor() {

    int value = 0;
    if (token == '(') {
        match('(');
        value = expr();
        match(')');
    } else {
        value = token_val;
        match(Num);
    }
    return value;
}

int
term_tail(int lvalue) {
    
    if (token == '*') {
        match('*');
        int value = lvalue * factor();
        return term_tail(value);
    } else if (token == '/') {
        match('/');
        int value = lvalue / factor();
        return term_tail(value);
    } else {
        return lvalue;
    }
}

int
term() {

    int lvalue = factor();
    return term_tail(lvalue);
}

int
expr_tail(int lvalue) {

    if (token == '+') {
        match('+');
        int value = lvalue + term();
        return expr_tail(value);
    } else if (token == '-') {
        match('-');
        int value = lvalue - term();
        return expr_tail(value);
    } else {
        return lvalue;
    }
}

int
expr() {

    int lvalue = term();
    return expr_tail(lvalue);
}



/**
 parse enum[id] { a=10,b=20,...}
 用于解析枚举类型的定义。主要的逻辑用于解析用逗号（,）分隔的变量，值得注意的是在编译器中如何保存枚举变量的信息。
 */
void
enum_declaration() {
    int i = 0;
    while (token != '}') {
        if (token != Id) {
            printf("%d: bad enum identifier %d\n", line, token);
            exit(-1);
        }
        next();
        /* like {a = 10}*/
        if (token == Assign) {
            next();
            if (token != Num) {
                printf("%d: bad enum initializer\n", line);
                exit(-1);
            }
            i = token_val;
            next();
        }
        
        current_id[Class] = Num;
        current_id[Type] = INT;
        current_id[Value] = i++;
        
        if (token == ',')
            next();
    }
}


/**
  expressions have various format.
  but majorly can be divided into two parts: unit and operator
  for example `(char) *a[10] = (int *) func(b > 0 ? 10 : 20);
  `a[10]` is an unit while `*` is an operator.
  `func(...)` in total is an unit.
  so we should first parse those unit and unary operators
  and then the binary ones
 
  also the expression can be in the following types:
 
  1. unit_unary ::= unit | unit unary_op | unary_op unit
  2. expr ::= unit_unary (bin_op unit_unary ...)
 */
void
expression(int level) {
    
    int *id;
    int tmp;
    int *addr;
    {
        if (!token) {
            printf("%d: unexpected token EOF of expression\n", line);
            exit(-1);
        }
        if (token == Num) {
            match(Num);
            /* emit code */
            *++text = IMM;
            *++text = token_val;
            expr_type = INT;
        }
        /* continous string "abc" 字符串常量, C 语言的字符串常量支持如下风格
         char *p;
         p = "first line"
         "second line"; 
         即跨行的字符串拼接，它相当于：p = "first linesecond line";
         */
        else if (token == '"') {
            *++text = IMM;
            *++text = token_val;
            match('"');
            /* store the rest strings */
            while (token == '"') {
                match('"');
            }
            
            /* append the end of string character '\0', all the data are default
             to 0, so just move data one position forward. */
            data = (char *)(((int)data + sizeof(int)) & (-sizeof(int)));
            expr_type = PTR;
        }
        /* sizeof 是一个一元运算符，我们需要知道后面参数的类型 ;
         只支持 sizeof(int)，sizeof(char) 及 sizeof(pointer type...)。并且它的结果是 int 型 */
        else if (token == Sizeof) {
            match(Sizeof);
            match('(');
            expr_type = INT;
            
            if (token == Int)
                match(Int);
            else if (token == Char) {
                match(Char);
                expr_type = CHAR;
            }
            
            while (token == Mul) {
                match(Mul);
                expr_type = expr_type + PTR;
            }
            
            match(')');
            /* emit code */
            *++text = IMM;
            *++text = (expr_type == CHAR) ? sizeof(char) : sizeof(int);
            
            expr_type = INT;
        }
        /* 变量与函数调用 ;由于取变量的值与函数的调用都是以 Id 标记开头的，因此将它们放在一起处理
        there are several type when occurs to Id
        but this is unit, so it can only be
        1. function call
        2. Enum variable
        3. global/local variable
        */
        else if (token == Id) {
            match(Id);

            id = current_id;
            /* function call */
            if (token == '(') {
                
                match('(');
                
                /* 1 pass in arguments 顺序将参数入栈  */
                tmp = 0; /* number of arguments */
                while (token != ')') {
                    expression(Assign);
                    *++text = PUSH;
                    tmp ++;
                    
                    if (token == ',') {
                        match(',');
                    }
                    
                }
                match(')');
                
                /* 2 emit code; system functions  判断函数的类型,内置函数有对应的汇编指令,而普通的函数则编译成 CALL <addr> 的形式*/
                if (id[Class] == Sys) {
                    *++text = id[Value];
                }
                /* function call */
                else if (id[Class] == Fun) {
                    *++text = CALL;
                    *++text = id[Value];
                }
                else {
                    printf("%d: bad function call\n", line);
                    exit(-1);
                }
                
                /* 3 clean the stack for arguments 用于清除入栈的参数。因为我们不在乎出栈的值，所以直接修改栈指针的大小即可 */
                if (tmp > 0) {
                    *++text = ADJ;
                    *++text = tmp;
                }
                expr_type = id[Type];
            }
            /* 4 enum variable 当该标识符是全局定义的枚举类型时,直接将对应的值用 IMM 指令存入 AX 即可 */
            else if (id[Class] == Num) {
                *++text = IMM;
                *++text = id[Value];
                expr_type = INT;
            }
            /* 5 variable  是用于加载变量的值，如果是局部变量则采用与 bp 指针相对位置的形式 */
            else {
                
                if (id[Class] == Loc) {
                    *++text = LEA;
                    *++text = index_of_bp - id[Value];
                }
                else if (id[Class] == Glo) {
                    *++text = IMM;
                    *++text = id[Value];
                }
                else {
                    printf("%d: undefined variable\n", line);
                    exit(-1);
                }
                
                /* 6 emit code, default behaviour is to load the value of the
                 address which is stored in `ax`;无论是全局还是局部变量，最终都根据它们的类型用 LC 或 LI 指令加载对应的值 */
                expr_type = id[Type];
                *++text = (expr_type == Char) ? LC : LI;
            }
        }
        /* cast or parenthesis */
        else if (token == '(') {
            match('(');
            if (token == Int || token == Char) {
                tmp = (token == Char) ? CHAR : INT; /* cast type */
                match(token);
                while (token == Mul) {
                    match(Mul);
                    tmp = tmp + PTR;
                }
                
                match(')');
                
                expression(Inc); /* cast has precedence as Inc(++) */
                
                expr_type  = tmp;
            }
            /* normal parenthesis */
            else {
                
                expression(Assign);
                match(')');
            }
        }
        /*  dereference *<addr> */
        else if (token == Mul) {
            match(Mul);
            expression(Inc); /* dereference has the same precedence as Inc(++) */
            
            if (expr_type >= PTR) {
                expr_type = expr_type - PTR;
            } else {
                printf("%d: bad dereference\n", line);
                exit(-1);
            }
            
            *++text = (expr_type == CHAR) ? LC : LI;
        }
        /* get the address of */
        else if (token == And) {
            match(And);
            expression(Inc); /* get the address of */
            if (*text == LC || *text == LI) {
                text --;
            } else {
                printf("%d: bad address of\n", line);
                exit(-1);
            }
            
            expr_type = expr_type + PTR;
        }
        /*  not  */
        else if (token == '!') {
            
            match('!');
            expression(Inc);
            
            /* emit code, use <expr> == 0 */
            *++text = PUSH;
            *++text = IMM;
            *++text = 0;
            *++text = EQ;
            
            expr_type = INT;
        }
        /* bitwise not */
        else if (token == '~') {
            match('~');
            expression(Inc);
            
            /* emit code, use <expr> XOR -1 */
            *++text = PUSH;
            *++text = IMM;
            *++text = -1;
            *++text = XOR;
            
            expr_type = INT;
        }
        /* +var, do nothing */
        else if (token == Add) {
            match(Add);
            expression(Inc);
            
            expr_type = INT;
        }
        /* -var */
        else if (token == Sub) {
            match(Sub);
            
            if (token == Num) {
                *++text = IMM;
                *++text = -token_val;
                match(Num);
            } else {
                
                *++text = IMM;
                *++text = -1;
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
            }
            
            expr_type = INT;
        }
        else if (token == Inc || token == Dec) {
            tmp = token;
            match(token);
            expression(Inc);
            if (*text == LC) {
                /* to duplicate the address */
                *text = PUSH;
                *++text = LC;
            } else if (*text == LI) {
                *text = PUSH;
                *++text = LI;
            } else {
                printf("%d: bad lvalue of pre-increment\n", line);
                exit(-1);
            }
            *++text = PUSH;
            *++text = IMM;
            *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
            *++text = (tmp == Inc) ? ADD : SUB;
            *++text = (expr_type == CHAR) ? SC : SI;
        }
        else {
            printf("%d: bad expression\n", line);
            exit(-1);
        }
    }
    /* binary operator and postfix operators. */
    {
        /* handle according to current operator's precedence */
        while (token >= level) {
            tmp = expr_type;
            /* var = expr; */
            if (token == Assign) {
                match(Assign);
                if (*text == LC || *text == LI) {
                    *text = PUSH; // save the lvalue's pointer
                } else {
                    printf("%d: bad lvalue in assignment\n", line);
                    exit(-1);
                }
                expression(Assign);
                
                expr_type = tmp;
                *++text = (expr_type == CHAR) ? SC : SI;
            }
            /* expr ? a : b; */
            else if (token == Cond) {
                match(Cond);
                *++text = JZ;
                addr = ++text;
                expression(Assign);
                if (token == ':') {
                    match(':');
                } else {
                    printf("%d: missing colon in conditional\n", line);
                    exit(-1);
                }
                *addr = (int)(text + 3);
                *++text = JMP;
                addr = ++text;
                expression(Cond);
                *addr = (int)(text + 1);
            }
            /* logic or */
            else if (token == Lor) {
                match(Lor);
                *++text = JNZ;
                addr = ++text;
                expression(Lan);
                *addr = (int)(text + 1);
                expr_type = INT;
            }
            /* logic and */
            else if (token == Lan) {
                match(Lan);
                *++text = JZ;
                addr = ++text;
                expression(Or);
                *addr = (int)(text + 1);
                expr_type = INT;
            }
            /* bitwise or */
            else if (token == Or) {
                match(Or);
                *++text = PUSH;
                expression(Xor);
                *++text = OR;
                expr_type = INT;
            }
            /* bitwise xor */
            else if (token == Xor) {
                match(Xor);
                *++text = PUSH;
                expression(And);
                *++text = XOR;
                expr_type = INT;
            }
            /* bitwise and */
            else if (token == And) {
                match(And);
                *++text = PUSH;
                expression(Eq);
                *++text = AND;
                expr_type = INT;
            }
            /* equal == */
            else if (token == Eq) {
                match(Eq);
                *++text = PUSH;
                expression(Ne);
                *++text = EQ;
                expr_type = INT;
            }
            /* not equal != */
            else if (token == Ne) {
                match(Ne);
                *++text = PUSH;
                expression(Lt);
                *++text = NE;
                expr_type = INT;
            }
            /* less than */
            else if (token == Lt) {
                match(Lt);
                *++text = PUSH;
                expression(Shl);
                *++text = LT;
                expr_type = INT;
            }
            /* greater than */
            else if (token == Gt) {
                match(Gt);
                *++text = PUSH;
                expression(Shl);
                *++text = GT;
                expr_type = INT;
            }
            /* less than or equal to */
            else if (token == Le) {
                match(Le);
                *++text = PUSH;
                expression(Shl);
                *++text = LE;
                expr_type = INT;
            }
            /* greater than or equal to */
            else if (token == Ge) {
                match(Ge);
                *++text = PUSH;
                expression(Shl);
                *++text = GE;
                expr_type = INT;
            }
            /* shift left */
            else if (token == Shl) {
                match(Shl);
                *++text = PUSH;
                expression(Add);
                *++text = SHL;
                expr_type = INT;
            }
            /* shift right */
            else if (token == Shr) {
                match(Shr);
                *++text = PUSH;
                expression(Add);
                *++text = SHR;
                expr_type = INT;
            }
            /* add */
            else if (token == Add) {
                match(Add);
                *++text = PUSH;
                expression(Mul);
                
                expr_type = tmp;
                if (expr_type > PTR) {
                    /* pointer type, and not `char *` */
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                }
                *++text = ADD;
            }
            /* sub */
            else if (token == Sub) {

                match(Sub);
                *++text = PUSH;
                expression(Mul);
                /* pointer subtraction */
                if (tmp > PTR && tmp == expr_type) {
                    *++text = SUB;
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = DIV;
                    expr_type = INT;
                }
                /* pointer movement */
                else if (tmp > PTR) {
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                    *++text = SUB;
                    expr_type = tmp;
                }
                /* numeral subtraction */
                else {
                    *++text = SUB;
                    expr_type = tmp;
                }
            }
            /* multiply */
            else if (token == Mul) {
                match(Mul);
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
                expr_type = tmp;
            }
            /* divide */
            else if (token == Div) {
                match(Div);
                *++text = PUSH;
                expression(Inc);
                *++text = DIV;
                expr_type = tmp;
            }
            /* Modulo */
            else if (token == Mod) {
                match(Mod);
                *++text = PUSH;
                expression(Inc);
                *++text = MOD;
                expr_type = tmp;
            }
            else if (token == Inc || token == Dec) {
                /* postfix inc(++) and dec(--)
                 we will increase the value to the variable and decrease it
                 on `ax` to get its original value. */
                if (*text == LI) {
                    *text = PUSH;
                    *++text = LI;
                }
                else if (*text == LC) {
                    *text = PUSH;
                    *++text = LC;
                }
                else {
                    printf("%d: bad value in increment\n", line);
                    exit(-1);
                }
                
                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? ADD : SUB;
                *++text = (expr_type == CHAR) ? SC : SI;
                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? SUB : ADD;
                match(token);
            }
            /* array access var[xx] */
            else if (token == Break) {
                match(Break);
                *++text = PUSH;
                expression(Assign);
                match(']');
                /* pointer, `not char *` */
                if (tmp > PTR) {
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                }
                else if (tmp < PTR) {
                    printf("%d: pointer type expected\n", line);
                    exit(-1);
                }
                expr_type = tmp - PTR;
                *++text = ADD;
                *++text = (expr_type == CHAR) ? LC : LI;
            }
            else {
                printf("%d: compiler error, token = %d\n", line, token);
                exit(-1);
            }
        }
    }
}
/**
 there are 8 kinds of statements here:
 1. if (...) <statement> [else <statement>]
 2. while (...) <statement>
 3. { <statement> }
 4. return xxx;
 5. <empty statement>;
 6. expression; (expression end with semicolon)
 */
void
statement() {
    
    int *a, *b; /* bess for brach control */
    
    /*  if
     if (...) <statement> [else <statement>]
     
     if (<cond>)                  <cond>
                                    JZ a
        <true_statement>  ===>    <true_statement>
                                    JMP b
     else:
     
     a:                          a:
        <false_statement>          <false_statement>
     b:                          b:
     1. 执行条件表达式 <cond>
     2. 如果条件失败,则跳转到 a的位置,执行 else 语句,这里 else 语句是可以省略的,此时 a 和 b 都指向 IF 语句后方的代码
     3. 因为汇编code 是顺序排列的,所以如果执行了 true_statement,为了防止因为顺序排列而执行了 false_statement，所以需要无条件跳转 JMP b。
     */
    if (token == If) {
        match(If);
        match('(');
        /* parse condition */
        expression(Assign);
        match(')');
        
        /* emit code for if */
        *++text = JZ;
        b = ++text;
        /* parse statement */
        statement();
        /* parse else */
        if (token == Else) {
            match(Else);
            /* emit code for JMP B*/
            *b = (int)(text + 3);
            *++text = JMP;
            b = ++text;
            
            statement();
        }
        *b = (int)(text+1);
    }
    
    /**   While
      a:                     a:
         while (<cond>)        <cond>
                               JZ b
          <statement>          <statement>
                               JMP a
      b:                     b:
     */
    else if (token == While) {
        match(While);
        a = text +1;
        match('(');
        expression(Assign);
        match(')');
        *++text = JZ;
        b = ++text;
        
        statement();
        *++text = JMP;
        *++text = (int)a;
        *b = (int)(text +1);
    }
    /*return [expression]; 一旦遇到了 Return 语句,函数要退出,需要生成汇编代码 LEV 来表示退出*/
    else if (token == Return) {
        
        match(Return);
        
        if (token != ';')
            expression(Assign);
        
        match(';');
        
        /* emit code for return */
        *++text = LEV;
    }
    /*  { <statement> ... } */
    else if (token == '{') {
       
        match('{');
        
        while (token != '}')
            statement();
        
        match('}');
    }
    /* empty statement */
    else if (token == ';') {
        match(';');
    }
     /* a = b; or function_call(); */
    else {
        expression(Assign);
        match(';');
    }
}

/**
 parameter_decl ::= type {'*'} id {',' type {'*'} id}
 解析函数的参数就是解析以逗号分隔的一个个标识符，同时记录它们的位置与类型。
 */
void
function_parameter() {
    
    int type ;
    int params = 0;
    
    while (token != ')') {
        /* 1 与全局变量定义的解析一样，用于解析该参数的类型*/
        type = INT;
        if (token == Int) {
            match(Int);
        } else if (token == Char) {
            type = CHAR;
            match(Char);
        }
        
        /* pointer type */
        while (token == Mul) {
            match(Mul);
            type = type + PTR;
        }
        
        /* parameter name */
        if (token != Id) {
            printf("%d: bad parameter declaration\n", line);
            exit(-1);
        }
        if (current_id[Class] == Loc) {
            printf("%d: duplicate parameter declaration\n", line);
            exit(-1);
        }
        match(Id);
        
        /* 2 store the local variable */
        
        /* “局部变量覆盖全局变量”,先将全局变量的信息保存（无论是否真的在全局中用到了这个变量）在 BXXX 中,
            再赋上局部变量相关的信息,如 Value 中存放的是参数的位置（是第几个参数）.  */
        current_id[BClass] = current_id[Class];
        current_id[Class]  = Loc;
        current_id[BType]  = current_id[Type];
        current_id[type]   = type;
        current_id[BValue] = current_id[Value];
        current_id[Value]  = params++; /* index of current parameter */
        
        if (token == ',')
            match(',');
    }
    /* 3 与汇编代码的生成有关,index_of_bp 就是前文提到的 new_bp 的位置 */
    index_of_bp = params+1;
}

/**
    函数体的解析
    type func_name (...) {...}
                      -->|   |<--
    ... {
    1. local declarations
    2. statements
    }

 */
void
function_body() {
    
    int pos_local; /* position of local variables on the stack */
    int type = 0;
    pos_local = index_of_bp;
    /* 1 解析函数体内的局部变量的定义,代码与全局的变量定义几乎一样 */
    while (token == Int || token == Char) {
        /* local variable declaration ,just like global ones */
        basetype = (token == Int) ? Int : CHAR;
        match(token);
        
        while (token != ';') {
            type = basetype;
            while (token != ';') {
                type = basetype;
                match(Mul);
                type = type + PTR;
            }
            /* Invalid declaration */
            if (token != Id) {
                printf("%d: bad local declaration\n", line);
                exit(-1);
            }
            /* identifier exists */
            if (current_id[Class] == Loc) {
                printf("%d: duplicate local declaration\n", line);
                exit(-1);
            }
            match(Id);
            
            /* store the local variable */
            current_id[BClass] = current_id[Class];
            current_id[Class] = Loc;
            
            current_id[BType] = current_id[Type];
            current_id[Type] = type;
            
            current_id[BValue] = current_id[Value];
            current_id[Value] = ++pos_local; /* index of current parameter */
            
            if (token == ',')
                match(',');
        }
        match(';');
    }
    /* 2  save the stack size for local variables 则用于生成汇编代码,需要在栈上为局部变量预留空间 */
    *++text = ENT;
    *++text = pos_local - index_of_bp;
    
    /* statements */
    while (token != '}') {
        
    }
    /* emit code for leaving the sub function */
    *++text = LEV;
}
/**
 type func_name (...) {...}
               | this part
 函数定义的解析
 
 */
void
function_declaration() {
    
    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();
    /*
     没有消耗最后的}字符。这么做的原因是：variable_decl 与 function_decl 是放在一起解析的，
     而 variable_decl 是以字符 ; 结束的。而 function_decl 是以字符 } 结束的，若在此通过 
     match 消耗了 ‘;’ 字符，那么外层的 while 循环就没法准确地知道函数定义已经结束。
     所以我们将结束符的解析放在了外层的 while 循环中。
     */
   //match('}');
    current_id = symbols;
    while (current_id[Token]) {
        if (current_id[Class] == Loc) {
            current_id[Class] = current_id[BClass];
            current_id[Type]  = current_id[BType];
            current_id[Value] = current_id[BValue];
        }
        current_id = current_id + IdSize;
    }
}

/* 全局的定义语句，包括变量定义，类型定义（只支持枚举）及函数定义 */
/* 所以一个类型首先有基本类型, 如 CHAR 或 INT,当它是一个指向基本类型的指针时,如 int *data,
   我们就将它的类型加上 PTR 即代码中的: type = type + PTR;
   同理，如果是指针的指针,则再加上 PTR.
 */
void
global_declaration() {
    /*  int [*]id [; | (...) {...}] */
    int type; /* tmp, actual type for variable */
    int i;
    
    basetype  = INT;
    /* parse enum, this should be treated alone */
    if (token == Enum) {
        /* enum[id] { a=10,b=20,...}*/
        match(Enum);
        if (token != '{')
            match(Id); /* skip the [id] part */
        /* parse the assign part */
        if (token == '{') {
            match('{');
            enum_declaration();
            match('}');
        }
        
        match(';');
        return;
    }
    /* parse type information */
    if (token == Int)
        match(Int);
    else if (token == Char) {
        match(Char);
        basetype = CHAR;
    }
    /* parse the comma seperated variable declaration. */
    while (token != ';' && token != '}') {
        type = basetype;
        /* parse pointer type, note that there may exist 'int *****x'*/
        while (token == Mul) {
            match(Mul);
            type = type + PTR;
        }
        /* invalid declaration */
        if (token != Id) {
            printf("%d: bad global declaration\n", line);
            exit(-1);
        }
        /* identifier exists */
        if (current_id[Class]) {
            printf("%d: duplicate global declaration\n", line);
            exit(-1);
        }
        match(Id);
        current_id[Type] = type;
        
        if (token == '(') {
            current_id[Class] = Fun;
            /* the memory address of function */
            current_id[Value] = (int)(text +1);
            function_declaration();
        } else {  /* variable declaration */
            current_id[Class] = Glo; /* global variable */
            current_id[Value] = (int)data; /* assign memory address */
            data = data + sizeof(int);
        }
        
        if (token == ',') {
            match(',');
        }
    }
    next();
}

void
program() {
    
    next(); /* get next token */
    while (token > 0) {
        printf("token is: %c\n", token);
        global_declaration();
    }
}

int
eval() {
    
    int op, *tmp;
    cycle = 0;

    while (1) {
        cycle ++;
        op = *pc++; // get next operation code
        
        // print debug info
        if (debug) {
            printf("%d> %.4s", cycle,
                   & "LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
                   "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                   "OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT"[op * 5]);
            if (op <= ADJ)
            printf(" %d\n", *pc);
            else
            printf("\n");
        }
        
        if (op == IMM)  /* load immediate value to ax */
            ax = *pc++;
        else if (op == LC) /* load character to ax, address in ax */
            ax = *(char *)ax;
        else if (op == LI)  /* load integer to ax, address in ax */
            ax = *(int *)ax;
        else if (op == SC) /* save character to address, value in ax, address on stack */
            ax = *(char *)*sp++ = ax;
        else if (op == SI) /* save integer to address, value in ax, address on stack */
            *(int *)*sp++ = ax;
        else if (op == PUSH)  /* push the value of ax onto the stack */
            *--sp = ax;
        else if (op == JMP)  /* jump to the address */
            pc = (int *)*pc;
        else if (op == JZ)  /* jump if ax is zero */
            pc = ax ? pc + 1 : (int *)*pc;
        else if (op == JNZ) /* jump if ax is zero */
             pc = ax ? (int *)*pc : pc + 1;
        else if (op == CALL) { /* call subroutine */
            *--sp = (int)(pc+1);
            pc = (int *)*pc;
        }
        //else if (op == RET)  {pc = (int *)*sp++;}                              // return from subroutine;
        else if (op == ENT)  {  /* make new stack frame */
            *--sp = (int)bp;
            bp = sp;
            sp = sp - *pc++;
        }
        else if (op == ADJ)  { /* add esp, <size> */
            sp = sp + *pc++;
        }
        else if (op == LEV)  { /* restore call frame and PC */
            sp = bp;
            bp = (int *)*sp++;
            pc = (int *)*sp++;
        }
        else if (op == LEA) /* load address for arguments. */
            ax = (int)(bp + *pc++);
        
        else if (op == OR)  ax = *sp++ | ax;
        else if (op == XOR) ax = *sp++ ^ ax;
        else if (op == AND) ax = *sp++ & ax;
        else if (op == EQ)  ax = *sp++ == ax;
        else if (op == NE)  ax = *sp++ != ax;
        else if (op == LT)  ax = *sp++ < ax;
        else if (op == LE)  ax = *sp++ <= ax;
        else if (op == GT)  ax = *sp++ >  ax;
        else if (op == GE)  ax = *sp++ >= ax;
        else if (op == SHL) ax = *sp++ << ax;
        else if (op == SHR) ax = *sp++ >> ax;
        else if (op == ADD) ax = *sp++ + ax;
        else if (op == SUB) ax = *sp++ - ax;
        else if (op == MUL) ax = *sp++ * ax;
        else if (op == DIV) ax = *sp++ / ax;
        else if (op == MOD) ax = *sp++ % ax;
        
        else if (op == EXIT) {
            printf("exit(%d)", *sp);
            return *sp;
        }
        else if (op == OPEN)
            ax = open((char *)sp[1], sp[0]);
        
        else if (op == CLOS)
            ax = close(*sp);
        
        else if (op == READ)
            ax = read(sp[2], (char *)sp[1], *sp);
        
        else if (op == PRTF) {
            tmp = sp + pc[1];
            ax = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]);
        }
        else if (op == MALC)
            ax = (int)malloc(*sp);
        
        else if (op == MSET)
            ax = (int)memset((char *)sp[2], sp[1], *sp);
            
        else if (op == MCMP)
            ax = memcmp((char *)sp[2], (char *)sp[1], *sp);
            
        else {
            printf("unknown instruction:%d\n", op);
            return -1;
        }
    }
}

int
main(int argc, char *argv[]) {
    int i;
    int fd;
    int *tmp;
    
    argc--;
    argv++;
    
    /* parse arguments */
    if (argc > 0 && **argv == '-' && (*argv)[1] == 's') {
        assembly = 1;
        --argc;
        ++argv;
    }
    if (argc > 0 && **argv == '-' && (*argv)[1] == 'd') {
        debug = 1;
        --argc;
        ++argv;
    }
    if (argc < 1) {
        printf("usage: xc [-s] [-d] file ...\n");
        return -1;
    }
    
    if ((fd = open(*argv, 0)) < 0) {
        printf("could not open(%s)\n", *argv);
        return -1;
    }
    
    poolsize = 256 * 1024; /* arbitrary size */
    line = 1;
    
    /* allocate memory */
    if (!(text = malloc(poolsize))) {
        printf("could not malloc(%d) for text area\n", poolsize);
        return -1;
    }
    if (!(data = malloc(poolsize))) {
        printf("could not malloc(%d) for data area\n", poolsize);
        return -1;
    }
    if (!(stack = malloc(poolsize))) {
        printf("could not malloc(%d) for stack area\n", poolsize);
        return -1;
    }
    if (!(symbols = malloc(poolsize))) {
        printf("could not malloc(%d) for symbol table\n", poolsize);
        return -1;
    }
    
    memset(text, 0, poolsize);
    memset(data, 0, poolsize);
    memset(stack, 0, poolsize);
    memset(symbols, 0, poolsize);
    
    old_text = text;
    
    src = "char else enum if int return sizeof while "
    "open read close printf malloc memset memcmp exit void main";
    
    /* add keywords to symbol table */
    i = Char;
    while (i <= While) {
        next();
        current_id[Token] = i++;
    }
    
    /* add library to symbol table */
    i = OPEN;
    while (i <= EXIT) {
        next();
        current_id[Class] = Sys;
        current_id[Type] = INT;
        current_id[Value] = i++;
    }
    
    next(); current_id[Token] = Char; // handle void type
    next(); idmain = current_id; // keep track of main
    
    if (!(src = old_src = malloc(poolsize))) {
        printf("could not malloc(%d) for source area\n", poolsize);
        return -1;
    }
    /* read the source file */
    if ((i = (int)read(fd, src, poolsize-1)) <= 0) {
        printf("read() returned %d\n", i);
        return -1;
    }
    src[i] = 0; /* add EOF character */
    close(fd);
    
    program();
    
    if (!(pc = (int *)idmain[Value])) {
        printf("main() not defined\n");
        return -1;
    }
    
    // dump_text();
    if (assembly) {
        /* only for compile */
        return 0;
    }
    
    /* setup stack */
    sp = (int *)((int)stack + poolsize);
    *--sp = EXIT; // call exit if main returns
    *--sp = PUSH; tmp = sp;
    *--sp = argc;
    *--sp = (int)argv;
    *--sp = (int)tmp;
    
    return eval();
}
