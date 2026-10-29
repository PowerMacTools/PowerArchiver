#pragma once

#include "formats.hpp"

// Anything libarchive can read: zip, tar (and compressed tarballs), 7z, rar,
// cpio, iso9660, cab, lha, ar, ...
class LibarchiveFormat : public Format {
public:
  LibarchiveFormat(std::filesystem::path path);

  std::vector<Entry> files() override;
  std::unique_ptr<Extraction>
  extract(const std::filesystem::path &dir) override;

private:
  std::filesystem::path path;
};
