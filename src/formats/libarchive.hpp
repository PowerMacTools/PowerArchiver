#pragma once

#include "formats.hpp"

// Anything libarchive can read: zip, tar (and compressed tarballs), 7z, rar,
// cpio, iso9660, cab, lha, ar, ...
class LibarchiveFormat : public Format {
public:
  // Returns nullptr, and why in error, if libarchive doesn't recognize path.
  static LibarchiveFormat *open(const std::string &path, std::string &error);

  std::vector<Entry> files() override;
  std::unique_ptr<Extraction>
  extract(const std::string &dir) override;

private:
  LibarchiveFormat(std::string path) : path(std::move(path)) {}

  std::string path;
};
