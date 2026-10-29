#pragma once

#include "../fs/fs.hpp"
#include "formats.hpp"

struct ost_archive_handle;

class StuffitFormat : public Format {
public:
  // Returns nullptr, and why in error, if openstuffit doesn't recognize path.
  static StuffitFormat *open(const std::string &path, std::string &error);
  ~StuffitFormat() override;

  StuffitFormat(const StuffitFormat &) = delete;
  StuffitFormat &operator=(const StuffitFormat &) = delete;

  std::vector<Entry> files() override;
  std::unique_ptr<Extraction>
  extract(const std::string &dir) override;

private:
  StuffitFormat() = default;

  // openstuffit parses from memory and keeps pointing into it
  fs::buffer data;
  ost_archive_handle *handle = nullptr;
};
