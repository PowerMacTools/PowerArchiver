#ifndef macintosh

#include "fs.hpp"

#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace fs {

std::string component(const std::string &name) { return name; }

std::string join(const std::string &dir, const std::string &name) {
  if (dir.empty()) {
    return name;
  }
  return dir.back() == '/' ? dir + name : dir + "/" + name;
}

std::string parent(const std::string &path) {
  std::size_t end = path.find_last_not_of('/');
  if (end == std::string::npos) {
    return path.empty() ? "" : "/";
  }
  std::size_t slash = path.rfind('/', end);
  if (slash == std::string::npos) {
    return "";
  }
  return slash == 0 ? "/" : path.substr(0, slash);
}

std::string name(const std::string &path) {
  std::size_t end = path.find_last_not_of('/');
  if (end == std::string::npos) {
    return "";
  }
  std::size_t slash = path.rfind('/', end);
  std::size_t start = slash == std::string::npos ? 0 : slash + 1;
  return path.substr(start, end + 1 - start);
}

bool create_directories(const std::string &path, std::string &error) {
  std::size_t start = 0;
  while (start < path.size()) {
    std::size_t slash = path.find('/', start + 1);
    std::string prefix = path.substr(0, slash);
    if (mkdir(prefix.c_str(), 0777) != 0 && errno != EEXIST) {
      error = "failed to create " + prefix + ": " + std::strerror(errno);
      return false;
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash;
  }

  struct stat st;
  if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    error = "not a directory: " + path;
    return false;
  }
  return true;
}

FILE *open(const std::string &path, const char *mode) {
  return fopen(path.c_str(), mode);
}

} // namespace fs

#endif
