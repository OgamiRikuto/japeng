%{
#include <stdlib.h>
#include <string.h>
#include "literal.h"
#include "symbol.h"

int linecounter = 1;
extern const char* current_filename;
int yycolumn = 1;

#define YY_USER_ACTION \
    yylloc.first_line   = yylineno; \
    yylloc.first_column = yycolumn; \
    yylloc.last_line    = yylineno; \
    yylloc.last_column  = yycolumn + yyleng - 1; \
    yycolumn += yyleng;
%}
%option nounput
%option yylineno
%%
"class"	{ return CLASS; }
"based"	{ return BASED; }
"from"	{ return FROM; }
"is"	{ yylval.symbol = intern_cstr(yytext); return SP_IDENTIFIER; }
"are"	{ yylval.symbol = intern_cstr(yytext); return SP_IDENTIFIER; }
"self"	{ yylval.symbol = intern_cstr(yytext); return SELF; }
"arg"	{ return ARG; }
"ret"	{ return RET; }
"return"	{ return RETURN; }
"break"	{ return BREAK; }
"continue"	{ return CONTINUE; }
[A-Z][a-zA-Z0-9_]*	{ yylval.symbol = intern_cstr(yytext); return CLASS_NAME; }
[a-z_][a-zA-Z0-9_]*	{ yylval.symbol = intern_cstr(yytext); return IDENTIFIER; }
[0-9]+				{ yylval.int_val = atoi(yytext); return INTEGER; }
[0-9]*"."[0-9]+		{ yylval.float_val = atof(yytext); return FLOAT; }
[0-9]*"/"[0-9]+		{ yylval.symbol = intern_cstr(yytext); return FRACTION; }
0b[0|1]+			{ return BINARY; }
0x[0-9a-fA-F]+ 		{ return HEX; }
\"[^\"\n]*\" 		{ yylval.symbol = unquote_and_intern(yytext, strlen(yytext)); return STRING; }
"+"|"-"|"*"|"/"|"%"|"="|"!="|">"|"<"|">="|"<="|"<<"|">>"	{ yylval.symbol = intern_cstr(yytext); return SP_IDENTIFIER; }
"//".*				{ }
"/*"				{ comment(); }
","					{ return COMMA; }
"."					{ return PERIOD; }
":"					{ return COLON; }
"("					{ return L_PAR; }
")"					{ return R_PAR; }
"["					{ return L_BLACKET; }
"]"					{ return R_BLACKET; }
"\n"				{ yycolumn = 1; linecounter++; }
[ \t\r]+			{ }
.					{ return UNKNOWN; }
%%
int yywrap(void) { return 1; }

void comment(void) {
	int boolean, first, second;

	boolean = TRUE;
	first = input();
	while (first != EOF && boolean) {
		second = input();
		if (first == '*' && second == '/') {
			boolean = FALSE;
		} else if (first == '\r' && second == '\n') {
			linecounter++;
			first = input();
		} else {
			if (first == '\r' || first == '\n') {
				linecounter++;
			}
			first = second;
		}
	}
	if (first == EOF) {
		fprintf(stderr, "eof in comment\n");
	}
}

ObjString* unquote_and_intern(const char* text, int len) {
    char buf[1024];
    int content_len = len - 2;
    if (content_len >= (int)sizeof(buf)) {
        content_len = sizeof(buf) - 1;
    }
    memcpy(buf, text + 1, content_len);
    buf[content_len] = '\0';
    return intern_cstr(buf);
}
