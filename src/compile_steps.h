// The functions to do each compile step, in the same header file to
// make it easy to get a high-level overview of how the compiler works.

#ifndef COMPILE_STEPS_H
#define COMPILE_STEPS_H

#include "token.h"
#include "ast.h"
#include <llvm-c/Core.h>

// Compiling functions
//
// Make sure that the filename passed to tokenize() stays alive throughout the
// entire compilation. It is used in error messages.
struct Token *tokenize(const char *filename);
struct AstStatement *parse(const struct Token *tokens);
LLVMModuleRef codegen(const struct AstStatement *ast);

// Cleaning up return values of compiling functions
void free_tokens(struct Token *tokenlist);
void free_ast(struct AstStatement *ast);

// Printing intermediate data for debugging or exploring the compiler.
//
// Each of these takes the data for an entire program and prints it.
// For example, print_ast() takes an array of statements ended with a
// special "end of file" statement.
void print_tokens(const struct Token *tokenlist);
void print_ast(struct AstStatement *statements);

#endif
