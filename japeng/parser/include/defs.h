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

int main(int, char**);
int yylex(void);
void comment(void);
ObjString* unquote_and_intern(const char*, int);
int yyparse(void);
void yyerror(char*);


#define MAX_FILES 100

extern ASTNode* current_parsed_ast;
extern ASTNode* parsed_cd[MAX_FILES];
extern int      parsed_cd_count;
extern ASTNode* parsed_je;

#endif
