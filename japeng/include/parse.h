#ifndef __PARSE_H__
#define __PARSE_H__

typedef ASTNode ASTNode;

void parse(const char* dir_path);
void print_ast(ASTNode*, int);
void dump_ast(ASTNode*, int);

#endif
