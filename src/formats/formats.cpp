#include "formats.hpp"
#include "../fs/fs.hpp"
#include "libarchive.hpp"
#include "stuffit.hpp"
#include <magic.h>
#include <string>

Format *Format::from_file(const std::string &path, std::string &error) {
  if (Format *format = LibarchiveFormat::open(path, error)) {
    return format;
  }
  // libarchive covers far more formats, so its reason is the one to report
  std::string stuffit_error;
  return StuffitFormat::open(path, stuffit_error);
}

const std::string &Format::Entry::type() {
  if (!cached_type) {
    cached_type = resolve_type ? resolve_type() : "unknown";
    resolve_type = nullptr;
  }
  return *cached_type;
}

const std::string &Format::Entry::mime_type() {
  if (!cached_mime_type) {
    cached_mime_type = resolve_mime_type ? resolve_mime_type() : "text/plain";
    resolve_mime_type = nullptr;
  }
  return *cached_mime_type;
}

void Format::Entry::set_type(std::string type) {
  cached_type = std::move(type);
  resolve_type = nullptr;
}

void Format::Entry::set_type_function(std::function<std::string()> resolve) {
  cached_type.reset();
  resolve_type = std::move(resolve);
}
void Format::Entry::set_mime_type(std::string type) {
  cached_mime_type = std::move(type);
  resolve_mime_type = nullptr;
}

void Format::Entry::set_mime_type_function(
    std::function<std::string()> resolve) {
  cached_mime_type.reset();
  resolve_mime_type = std::move(resolve);
}

static std::string magic_problem;

// magic_load() needs stat(), which is missing from classic Mac OS, so read the
// database ourselves and load it from memory.
static magic_t open_magic(int flags) {
  static const fs::buffer database = [] {
    fs::buffer data;
    fs::read_file(MARCHIVER_MAGIC_DATABASE, data, magic_problem);
    return data;
  }();

  magic_t cookie = magic_open(flags);
  void *buffer = database.data.get();
  size_t size = database.size;
  if (cookie && magic_load_buffers(cookie, &buffer, &size, 1) != 0) {
    if (magic_problem.empty()) {
      const char *error = magic_error(cookie);
      magic_problem = error ? error : "failed to load the magic database";
    }
    magic_close(cookie);
    cookie = nullptr;
  }
  return cookie;
}

std::string Format::file_type(const void *data, std::size_t size) {
  static magic_t cookie = open_magic(MAGIC_NONE);
  if (!cookie) {
    return "unknown (" + magic_problem + ")";
  }

  const char *type = magic_buffer(cookie, data, size);
  return type ? type : "unknown";
}

std::string Format::mime_type(const void *data, std::size_t size) {
  static magic_t cookie = open_magic(MAGIC_MIME_TYPE);
  if (!cookie) {
    return "text/plain";
  }

  const char *type = magic_buffer(cookie, data, size);
  return "text/plain";
}
