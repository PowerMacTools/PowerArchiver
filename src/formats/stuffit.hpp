#pragma once

#include "formats.hpp"

struct ost_archive_handle;

class StuffitFormat : public Format {
public:
  StuffitFormat(std::filesystem::path path);
  ~StuffitFormat() override;

  StuffitFormat(const StuffitFormat &) = delete;
  StuffitFormat &operator=(const StuffitFormat &) = delete;

  std::vector<Entry> files() override;
  std::unique_ptr<Extraction>
  extract(const std::filesystem::path &dir) override;

private:
  ost_archive_handle *handle = nullptr;
};
