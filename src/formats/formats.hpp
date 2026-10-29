#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
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
    const std::string &mime_type();

    void set_type(std::string type);
    void set_type_function(std::function<std::string()> resolve);
    void set_mime_type(std::string type);
    void set_mime_type_function(std::function<std::string()> resolve);

  private:
    std::function<std::string()> resolve_type;
    std::function<std::string()> resolve_mime_type;
    std::optional<std::string> cached_type;
    std::optional<std::string> cached_mime_type;
  };

  static Format *from_file(std::filesystem::path path);

  static std::string file_type(const void *data, std::size_t size);
  static std::string mime_type(const void *data, std::size_t size);

  virtual ~Format() = default;

  virtual std::vector<Entry> files() = 0;

  class Extraction {
  public:
    virtual ~Extraction() = default;

    virtual bool step() = 0;
    virtual double progress() = 0;

    // Entries left out because the format can't extract them.
    virtual const std::vector<std::string> &skipped() { return none; }

  private:
    std::vector<std::string> none;
  };

  virtual std::unique_ptr<Extraction>
  extract(const std::filesystem::path &dir) = 0;
};
