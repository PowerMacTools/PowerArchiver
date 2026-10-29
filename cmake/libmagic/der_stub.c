// file.h has to come first
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
