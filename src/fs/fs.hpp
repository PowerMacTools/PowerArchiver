#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace fs {
std::string component(const std::string &name);

std::string join(const std::string &dir, const std::string &name);
bool join_member(const std::string &dir, const std::string &member,
                 std::string &path, std::string &error);
std::string parent(const std::string &path);
std::string name(const std::string &path);
bool create_directories(const std::string &path, std::string &error);
FILE *open(const std::string &path, const char *mode);
struct buffer {
  std::unique_ptr<char, decltype(&std::free)> data{nullptr, std::free};
  std::size_t size = 0;
};
bool read_file(const std::string &path, buffer &out, std::string &error);
std::uint64_t file_size(const std::string &path);

#ifdef macintosh
bool write_resource_fork(const std::string &path, const void *data,
                         std::size_t size, std::string &error);
void guess_finder_info(const std::string &path);
bool set_finder_info(const std::string &path, const std::uint8_t type[4],
                     const std::uint8_t creator[4], std::uint16_t flags,
                     std::string &error);
#endif

} // namespace fs
