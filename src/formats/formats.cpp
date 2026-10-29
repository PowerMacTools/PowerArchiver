#include "formats.hpp"
#include "libarchive.hpp"
#include "stuffit.hpp"
#include <filesystem>
#include <magic.h>
#include <stdexcept>
#include <string>

Format *Format::from_file(std::filesystem::path path) {
  if (path.extension() == ".sit") {
    return new StuffitFormat(path);
  }

  try {
    return new LibarchiveFormat(path);
  } catch (const std::exception &) {
    return nullptr;
  }
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

std::string Format::file_type(const void *data, std::size_t size) {
  static magic_t cookie = [] {
    magic_t cookie = magic_open(MAGIC_NONE);
    if (cookie && magic_load(cookie, MARCHIVER_MAGIC_DATABASE) != 0) {
      magic_close(cookie);
      cookie = nullptr;
    }
    return cookie;
  }();

  const char *type = magic_buffer(cookie, data, size);
  return type ? type : "unknown";
}

std::string Format::mime_type(const void *data, std::size_t size) {
  static magic_t cookie = [] {
    magic_t cookie = magic_open(MAGIC_MIME_TYPE);
    if (cookie && magic_load(cookie, MARCHIVER_MAGIC_DATABASE) != 0) {
      magic_close(cookie);
      cookie = nullptr;
    }
    return cookie;
  }();

  const char *type = magic_buffer(cookie, data, size);
  return type ? type : "text/plain";
}
