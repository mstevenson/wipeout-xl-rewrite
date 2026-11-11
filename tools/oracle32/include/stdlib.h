#ifndef ORACLE_STDLIB_H
#define ORACLE_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
int abs(int);
long labs(long);
int rand(void);
void srand(unsigned int);
void abort(void);
void exit(int);
int atoi(const char *);
long strtol(const char *, char **, int);
#endif
