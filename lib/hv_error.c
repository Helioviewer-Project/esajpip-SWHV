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

const char *hv_grouped(uint64_t n, char out[32]) {
    char digits[21];
    int len = snprintf(digits, sizeof digits, "%llu", (unsigned long long)n), i, k = 0;
    for (i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0)
            out[k++] = ',';
        out[k++] = digits[i];
    }
    out[k] = 0;
    return out;
}
