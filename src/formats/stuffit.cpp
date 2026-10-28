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

// Find or create the child named `name` in `entries`.
static Format::Entry &child(std::vector<Format::Entry> &entries,
                            const std::string &name) {
  for (auto &entry : entries) {
    if (entry.name == name) {
      return entry;
    }
  }
  Format::Entry entry;
  entry.name = name;
  entries.push_back(entry);
  return entries.back();
}

// How much of a file to decompress for libmagic. Most signatures sit in the
// first few KB; this leaves room for ones at larger offsets, like ISO 9660.
static constexpr uint64_t type_prefix_size = 1024 * 1024;

// Decompress up to `limit` bytes from the start of a fork. openstuffit's
// decoders stop once they've produced fork->uncompressed_size bytes, so
// shrinking it gets a prefix. LZW and deflate error on a truncated size,
// so fall back to decompressing the whole fork for those.
static ost_status decompress_prefix(const ost_archive *archive,
                                    const ost_fork_info *fork, uint64_t limit,
                                    ost_decompressed *out) {
  const uint8_t *data = ost_archive_data(archive);
  size_t size = ost_archive_data_size(archive);

  if (fork->uncompressed_size > limit) {
    ost_fork_info prefix = *fork;
    prefix.uncompressed_size = limit;
    // Stored data is copied using the compressed size instead.
    if ((fork->method & 0x0f) == 0 && !fork->encrypted) {
      prefix.compressed_size = limit;
    }
    if (ost_decompress_fork(data, size, &prefix, out) == OST_OK) {
      return OST_OK;
    }
    ost_decompressed_free(out);
  }

  return ost_decompress_fork(data, size, fork, out);
}

// Decompress the start of the entry's data fork and ask libmagic what it is.
static std::string entry_type(const ost_archive_handle *handle,
                              const ost_entry *ent) {
  if (!ent->data_fork.present) {
    return ent->resource_fork.present ? "resource fork only" : "empty";
  }
  if (ent->data_fork.encrypted) {
    return "encrypted";
  }

  ost_decompressed data = {};
  ost_status status =
      decompress_prefix(ost_archive_handle_archive(handle), &ent->data_fork,
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
          Entry &leaf = child(*level, part);
          leaf.is_dir = leaf.is_dir || ent->is_dir;
          if (leaf.is_dir) {
            leaf.set_type("directory");
          } else {
            leaf.set_type([handle = handle, ent] {
              return entry_type(handle, ent);
            });
          }
        }
        break;
      }
      if (!part.empty()) {
        Entry &dir = child(*level, part);
        dir.is_dir = true;
        dir.set_type("directory");
        level = &dir.entries;
      }
      start = slash + 1;
    }
  }

  return root;
}
