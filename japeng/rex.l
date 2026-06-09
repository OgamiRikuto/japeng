%{
#include <stdlib.h>
#include <string.h>
int linecounter = 1;
%}
%option nounput
%option yylineno
%%
"class"	{ return CLASS; }
"based"	{ return BASED; }
"from"	{ return FROM; }
"is"	{ return SP_IDENTIFIER; }
"are"	{ return SP_IDENTIFIER; }
"self"	{ return SELF; }
"arg"	{ return ARG; }
"ret"	{ return RET; }
"return"	{ return RETURN; }
"break"	{ return BREAK; }
"continue"	{ return CONTINUE; }
[A-Z][a-zA-Z0-9_]*	{ yylval.string = strdup(yytext); return CLASS_NAME; }
[a-z_][a-zA-Z0-9_]*	{ yylval.string = strdup(yytext); return IDENTIFIER; }
[0-9]+				{ yylval.int_val = atoi(yytext); return INTEGER; }
[0-9]*"."[0-9]+		{ yylval.float_val = atof(yytext); return FLOAT; }
[0-9]*"/"[0-9]+		{ yylval.string = strdup(yytext); return FRACTION; }
0b[0|1]+			{ return BINARY; }
0x[0-9a-fA-F]+ 		{ return HEX; }
\"[^\"\n]*\" 		{ yylval.string = strndup(yytext + 1, strlen(yytext) - 2); return STRING; }
"+"|"-"|"*"|"/"|"%"|"="|"!="|">"|"<"|">="|"<="|"<<"|">>"	{ yylval.string = strdup(yytext); return SP_IDENTIFIER; }
"//".*				{ }
"/*"				{ comment(); }
","					{ return COMMA; }
"."					{ return PERIOD; }
":"					{ return COLON; }
"("					{ return L_PAR; }
")"					{ return R_PAR; }
"["					{ return L_BLACKET; }
"]"					{ return R_BLACKET; }
"\n"				{ linecounter++; }
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
