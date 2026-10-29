#include "fs.hpp"
#include "detail.hpp"

#include <memory>

namespace fs {

bool join_member(const std::string &dir, const std::string &member,
                 std::string &path, std::string &error) {
  path = dir;
  std::size_t start = 0;
  while (start <= member.size()) {
    std::size_t slash = member.find('/', start);
    std::string part = member.substr(start, slash - start);
    if (part == "..") {
      error = "unsafe path: " + member;
      return false;
    }
    if (!part.empty() && part != ".") {
      path = join(path, detail::component(part));
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return true;
}

bool read_file(const std::string &path, buffer &out, std::string &error) {
  std::unique_ptr<FILE, decltype(&fclose)> file(open(path, "rb"), fclose);
  long size;
  if (!file || fseek(file.get(), 0, SEEK_END) != 0 ||
      (size = ftell(file.get())) < 0 || fseek(file.get(), 0, SEEK_SET) != 0) {
    error = "failed to open " + path;
    return false;
  }

  // at least one byte, since malloc(0) may return null
  out.data.reset((char *)std::malloc(size ? size : 1));
  if (!out.data) {
    error = "not enough memory to read " + path;
    return false;
  }
  out.size = fread(out.data.get(), 1, size, file.get());
  if (out.size != (std::size_t)size) {
    error = "failed to read " + path;
    return false;
  }
  return true;
}

std::uint64_t file_size(const std::string &path) {
  std::unique_ptr<FILE, decltype(&fclose)> file(open(path, "rb"), fclose);
  if (!file || fseek(file.get(), 0, SEEK_END) != 0) {
    return 0;
  }
  long size = ftell(file.get());
  return size < 0 ? 0 : size;
}

} // namespace fs
