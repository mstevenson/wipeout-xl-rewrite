#ifndef ORACLE_STDIO_H
#define ORACLE_STDIO_H
#include <stddef.h>
typedef struct OracleFile FILE;
extern FILE *stdin, *stdout, *stderr;
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int sscanf(const char *, const char *, ...);
#endif
