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
    std::string full_path;
    std::vector<Entry> entries;
    bool is_dir = false;

    const std::string &type();

    void set_type(std::string type);
    void set_type_function(std::function<std::string()> resolve);

  private:
    std::function<std::string()> resolve_type;
    std::optional<std::string> cached_type;
  };

  static Format *from_file(std::filesystem::path path);

  static std::string file_type(const void *data, std::size_t size);

  virtual ~Format() = default;

  virtual std::vector<Entry> files() = 0;
};
