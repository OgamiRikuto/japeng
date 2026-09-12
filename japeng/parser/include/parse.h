#ifndef PARSE_H
#define PARSE_H

typedef ASTNode ASTNode;

void parse(const char* dir_path);
void print_ast(ASTNode*, int);
void dump_ast(ASTNode*, int);

#endif
