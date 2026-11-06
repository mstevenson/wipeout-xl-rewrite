#ifndef ORACLE_STDLIB_H
#define ORACLE_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void exit(int);
int abs(int);
int atoi(const char *);
int rand(void);
void srand(unsigned int);
#endif
