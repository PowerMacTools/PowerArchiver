/*
 * Replaces file's der.c on classic Mac OS. der.h declares der_offs() as
 * returning int but der.c defines it as int32_t, which only agree where
 * int32_t is int; on Retro68 it's long. Both functions return -1 here, which
 * softmagic.c treats as no match, so DER certificates just go unrecognized.
 */
#include "file.h"
#include "der.h"

int der_offs(struct magic_set *ms, struct magic *m, size_t nbytes) {
  (void)ms;
  (void)m;
  (void)nbytes;
  return -1;
}

int der_cmp(struct magic_set *ms, struct magic *m) {
  (void)ms;
  (void)m;
  return -1;
}
