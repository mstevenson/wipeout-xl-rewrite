/* Opaque stream type; only declarations are needed when compiling oracle objects. */
#ifndef ORACLE_STDIO_H
#define ORACLE_STDIO_H
typedef struct OracleFile FILE;
#include <stddef.h>
int printf(const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
#endif
