#include "stuffit.hpp"
#include "formats.hpp"

#include <openstuffit/openstuffit.h>
#include <stdexcept>
#include <string>

extern "C" {
#include <ost_decompress.h>
}

StuffitFormat::StuffitFormat(std::filesystem::path path) {
  ost_parse_options options;
  ost_parse_options_init(&options);

  ost_status status =
      ost_archive_handle_open_file(path.c_str(), &options, &handle);
  if (status != OST_OK) {
    throw std::runtime_error("failed to open " + path.string() + ": " +
                             ost_status_string(status));
  }
}

StuffitFormat::~StuffitFormat() { ost_archive_handle_free(handle); }

static Format::Entry &child(std::vector<Format::Entry> &entries,
                            const std::string &name,
                            const std::string &full_path) {
  for (auto &entry : entries) {
    if (entry.name == name) {
      return entry;
    }
  }
  Format::Entry entry;
  entry.name = name;
  entry.full_path = full_path;
  entries.push_back(entry);
  return entries.back();
}

static ost_status decompress_n_bytes(const ost_archive *archive,
                                     const ost_fork_info *fork, uint64_t n,
                                     ost_decompressed *out) {
  const uint8_t *data = ost_archive_data(archive);
  size_t size = ost_archive_data_size(archive);

  if (fork->uncompressed_size > n) {
    ost_fork_info prefix = *fork;
    prefix.uncompressed_size = n;
    if ((fork->method & 0x0f) == 0 && !fork->encrypted) {
      prefix.compressed_size = n;
    }
    if (ost_decompress_fork(data, size, &prefix, out) == OST_OK) {
      return OST_OK;
    }
    ost_decompressed_free(out);
  }

  return ost_decompress_fork(data, size, fork, out);
}

static std::string entry_type(const ost_archive_handle *handle,
                              const ost_entry *ent) {
  if (!ent->data_fork.present) {
    return ent->resource_fork.present ? "resource fork only" : "empty";
  }
  if (ent->data_fork.encrypted) {
    return "encrypted";
  }

  // most file formats have their "file magic" within this number of bytes
  static constexpr uint64_t type_prefix_size = 1024 * 16;

  ost_decompressed data = {};
  ost_status status =
      decompress_n_bytes(ost_archive_handle_archive(handle), &ent->data_fork,
                         type_prefix_size, &data);
  if (status != OST_OK) {
    ost_decompressed_free(&data);
    return std::string("unreadable (") + ost_status_string(status) + ")";
  }

  std::string type = Format::file_type(data.data, data.size);
  ost_decompressed_free(&data);
  return type;
}

std::vector<Format::Entry> StuffitFormat::files() {
  std::vector<Entry> root;

  size_t count = ost_archive_handle_entry_count(handle);
  for (size_t i = 0; i < count; i++) {
    const ost_entry *ent = ost_archive_handle_entry(handle, i);
    if (!ent || !ent->path) {
      continue;
    }

    // openstuffit joins path components with '/'. Intermediate directories
    // may not have their own entries, so create them as we go.
    std::string path = ent->path;
    std::vector<Entry> *level = &root;
    size_t start = 0;
    while (true) {
      size_t slash = path.find('/', start);
      std::string part = path.substr(start, slash - start);
      if (slash == std::string::npos) {
        if (!part.empty()) {
          Entry &leaf = child(*level, part, path);
          leaf.is_dir = leaf.is_dir || ent->is_dir;
          if (leaf.is_dir) {
            leaf.set_type("directory");
          } else {
            leaf.set_type(
                [handle = handle, ent] { return entry_type(handle, ent); });
          }
        }
        break;
      }
      if (!part.empty()) {
        Entry &dir = child(*level, part, path);
        dir.is_dir = true;
        dir.set_type("directory");
        level = &dir.entries;
      }
      start = slash + 1;
    }
  }

  return root;
}
