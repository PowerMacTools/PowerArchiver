#pragma once

#include <cstddef>
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

  // Opens path with whichever format recognizes it, or returns nullptr and
  // says why in error.
  static Format *from_file(const std::string &path, std::string &error);

  static std::string file_type(const void *data, std::size_t size);
  static std::string mime_type(const void *data, std::size_t size);

  virtual ~Format() = default;

  virtual std::vector<Entry> files() = 0;

  // An extraction in progress, done a little at a time so the caller can keep
  // its UI responsive. Must not outlive the Format that created it.
  class Extraction {
  public:
    virtual ~Extraction() = default;

    // Does a small amount of work. Returns false once everything has been
    // extracted or it has failed, which error() tells apart.
    virtual bool step() = 0;

    // How much has been done, from 0 to 1.
    virtual double progress() = 0;

    // Entries left out because the format can't extract them.
    virtual const std::vector<std::string> &skipped() { return none; }

    // Why it failed, or empty if it hasn't.
    const std::string &error() const { return failure; }

  protected:
    // Records why it failed, for step() to return.
    bool fail(std::string why) {
      failure = std::move(why);
      return false;
    }

  private:
    std::vector<std::string> none;
    std::string failure;
  };

  virtual std::unique_ptr<Extraction>
  extract(const std::string &dir) = 0;
};
