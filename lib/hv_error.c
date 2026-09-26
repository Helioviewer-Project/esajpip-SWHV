/* hv_error.c: see hv_error.h. */
#include "hv_error.h"

#include <stdarg.h>
#include <stdio.h>

int hv_fail(char *error, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return -1;
}
