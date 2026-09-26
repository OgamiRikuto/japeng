#ifndef PARSE_H
#define PARSE_H

typedef ASTNode ASTNode;

bool parse_file(const char* full_path);
void parse(const char* dir_path);
void print_ast(ASTNode*, int);
void dump_ast(ASTNode*, int);

#endif
