#include "retro_compat.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>

/* Retro68 declares these but its C library doesn't define them. */

#define UNSUPPORTED(value)                                                     \
  do {                                                                         \
    errno = ENOSYS;                                                            \
    return value;                                                              \
  } while (0)

int utime(const char *path, const struct utimbuf *times) { UNSUPPORTED(-1); }

int access(const char *path, int mode) { UNSUPPORTED(-1); }
int chdir(const char *path) { UNSUPPORTED(-1); }
char *getcwd(char *buf, size_t size) { UNSUPPORTED(NULL); }
int dup2(int fd, int fd2) { UNSUPPORTED(-1); }
long pathconf(const char *path, int name) { UNSUPPORTED(-1); }
int truncate(const char *path, off_t length) { UNSUPPORTED(-1); }

int mkdir(const char *path, mode_t mode) { UNSUPPORTED(-1); }
int rmdir(const char *path) { UNSUPPORTED(-1); }
int chmod(const char *path, mode_t mode) { UNSUPPORTED(-1); }
int fchmod(int fd, mode_t mode) { UNSUPPORTED(-1); }
int fchmodat(int fd, const char *path, mode_t mode, int flag) {
  UNSUPPORTED(-1);
}
mode_t umask(mode_t mask) { return 0; }

int symlink(const char *target, const char *path) { UNSUPPORTED(-1); }
ssize_t readlink(const char *restrict path, char *restrict buf, size_t size) {
  UNSUPPORTED(-1);
}

/* No users or groups. */
struct passwd *getpwnam(const char *name) { return NULL; }
struct group *getgrnam(const char *name) { return NULL; }
