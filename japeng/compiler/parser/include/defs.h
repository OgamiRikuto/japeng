#ifndef DEFS_H
#define DEFS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "literal.h"


#define TRUE 1
#define FALSE 0


extern char *yytext;
extern int linecounter;
extern int yylineno;
extern int syntax_error_count;

#ifndef YYSTYPE_DEFINED
#define YYSTYPE_DEFINED 1
typedef union {
    Symbol* literal;
    Symbol* symbol;
    ASTNode* node;
} YYSTYPE;
#endif

extern YYSTYPE yylval;
extern Location current_ast_loc;

#ifndef YYLTYPE_DEFINED
#define YYLTYPE_DEFINED 1
typedef struct YYLTYPE {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} YYLTYPE;
#endif

extern YYLTYPE yylloc;

int main(int, char**);
int yylex(void);
void comment(void);
Symbol* unquote_and_intern(const char*, int);
int yyparse(void);
void yyerror(const char*);


#define MAX_FILES 100

extern ASTNode* current_parsed_ast;
extern ASTNode* parsed_cd[MAX_FILES];
extern int      parsed_cd_count;
extern ASTNode* parsed_je[MAX_FILES];
extern int parsed_je_count;

#endif
