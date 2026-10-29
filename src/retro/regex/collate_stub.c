/*
 * Replaces newlib's collate.c, which needs newlib's internal headers. No
 * locale collation table is ever loaded, so regcomp() uses plain byte ranges,
 * which is what libmagic expects.
 */
#include "collate.h"

int __collate_load_error = 1;

int __collate_range_cmp(int c1, int c2) { return c1 - c2; }
