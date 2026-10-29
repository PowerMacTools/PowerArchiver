#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace fs {

// Joins name onto dir with the platform's separator.
std::string join(const std::string &dir, const std::string &name);

// Joins an archive member path, whose components are separated by '/', onto
// dir. Empty and "." components are dropped, so absolute member paths stay
// inside dir. Fails on "..".
bool join_member(const std::string &dir, const std::string &member,
                 std::string &path, std::string &error);

// Everything up to the last component of path.
std::string parent(const std::string &path);

// The last component of path.
std::string name(const std::string &path);

// Creates path and any missing parents.
bool create_directories(const std::string &path, std::string &error);

// fopen() for a UTF-8 path.
FILE *open(const std::string &path, const char *mode);

// A whole file in memory. It's malloc()ed so that running out of memory is
// an error rather than a std::bad_alloc, which crashes without exceptions.
struct buffer {
  std::unique_ptr<char, decltype(&std::free)> data{nullptr, std::free};
  std::size_t size = 0;
};

// Reads a whole file.
bool read_file(const std::string &path, buffer &out, std::string &error);

// Size of a file in bytes, or 0 if it can't be opened.
std::uint64_t file_size(const std::string &path);

#ifdef macintosh
// Replaces path's resource fork with data. path must already exist.
bool write_resource_fork(const std::string &path, const void *data,
                         std::size_t size, std::string &error);

// Sets the Finder type and creator of the file at path from its name's
// extension, as Internet Config maps it, leaving them alone if it can't.
void guess_finder_info(const std::string &path);

// Sets the Finder type, creator and flags of the file at path.
bool set_finder_info(const std::string &path, const std::uint8_t type[4],
                     const std::uint8_t creator[4], std::uint16_t flags,
                     std::string &error);
#endif

} // namespace fs
