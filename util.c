/*************************************************************************
	> File Name: util.c
	> Author:TTc
	> Mail:liutianshxkernel@gmail.com
	> Created Time: Fri Oct 28 10:06:31 2016
 ************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

void
errorf(char *file, int line, char *fmt, ...) {
    fprintf(stderr, "%s:%d: ", file, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}


void
warn(char *fmt, ...) {
    fprintf(stderr, "warning: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}
