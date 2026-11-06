#ifndef ORACLE_O32_H
#define ORACLE_O32_H
#include "include/stddef.h"
#include "include/stdint.h"
int printf(const char *, ...);
int sprintf(char *, const char *, ...);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *);
char *strcat(char *, const char *);
int strncmp(const char *, const char *, size_t);
size_t strlen(const char *);
int abs(int);
int strcmp(const char *, const char *);
#endif
