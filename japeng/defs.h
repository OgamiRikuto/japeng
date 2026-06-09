#ifndef __DEFS_H__
#define __DEFS_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

#define TRUE 1
#define FALSE 0


extern char *yytext;
extern int linecounter;
extern int yylineno;

int main(int, char**);
int yylex(void);
void comment(void);
int yyparse(void);
void yyerror(char*);


#define MAX_FILES 100

extern ASTNode* parsed_files[MAX_FILES];
extern int parsed_file_count;


#endif
