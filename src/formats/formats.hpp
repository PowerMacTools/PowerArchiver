#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
class Format {
public:
  class Entry {
  public:
    std::string name;
    std::vector<Entry> entries;
    bool is_dir = false;

    // Human readable file type. It's worked out the first time this is
    // called, so the Format that produced this entry must still be alive.
    const std::string &type();

    void set_type(std::string type);
    void set_type(std::function<std::string()> resolve);

  private:
    std::function<std::string()> resolve_type;
    std::optional<std::string> cached_type;
  };

  static Format *from_file(std::filesystem::path path);

  // Describe the contents of `data` using libmagic.
  static std::string file_type(const void *data, std::size_t size);

  virtual ~Format() = default;

  virtual std::vector<Entry> files() = 0;
};
