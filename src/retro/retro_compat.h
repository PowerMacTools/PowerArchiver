#ifndef RETRO_COMPAT_H
#define RETRO_COMPAT_H

#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
/* Here so its declarations come before libmagic's fork() macro, which it
   defines when there's no fork, ahead of including <unistd.h> itself. */
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* There are no symlinks, so lstat() is stat(). */
#define lstat(path, buf) stat((path), (buf))

/* <utime.h> only defines struct utimbuf. */
int utime(const char *path, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif
