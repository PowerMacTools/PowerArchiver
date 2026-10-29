#include "libarchive.hpp"
#include "formats.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>
#include <string>

using archive_ptr = std::unique_ptr<archive, decltype(&archive_read_free)>;

static std::string error_string(archive *a) {
  const char *error = archive_error_string(a);
  return error ? error : "unknown error";
}

static archive_ptr open_archive(const std::filesystem::path &path) {
  archive_ptr a(archive_read_new(), archive_read_free);
  archive_read_support_filter_all(a.get());
  // archive_read_support_format_all() minus mtree and empty, which would
  // claim plain text and empty files as archives.
  archive_read_support_format_ar(a.get());
  archive_read_support_format_cpio(a.get());
  archive_read_support_format_lha(a.get());
  archive_read_support_format_tar(a.get());
  archive_read_support_format_xar(a.get());
  archive_read_support_format_warc(a.get());
  archive_read_support_format_7zip(a.get());
  archive_read_support_format_cab(a.get());
  archive_read_support_format_rar(a.get());
  archive_read_support_format_rar5(a.get());
  archive_read_support_format_iso9660(a.get());
  archive_read_support_format_zip(a.get());

  if (archive_read_open_filename(a.get(), path.c_str(), 1024 * 16) !=
      ARCHIVE_OK) {
    throw std::runtime_error("failed to open " + path.string() + ": " +
                             error_string(a.get()));
  }
  return a;
}

static std::string entry_path(archive_entry *ent) {
  const char *path = archive_entry_pathname_utf8(ent);
  if (!path) {
    path = archive_entry_pathname(ent);
  }
  return path ? path : "";
}

LibarchiveFormat::LibarchiveFormat(std::filesystem::path path)
    : path(std::move(path)) {
  // make sure libarchive recognizes it before we claim the file
  archive_ptr a = open_archive(this->path);
  archive_entry *ent;
  int status = archive_read_next_header(a.get(), &ent);
  if (status != ARCHIVE_OK && status != ARCHIVE_WARN && status != ARCHIVE_EOF) {
    throw std::runtime_error("failed to read " + this->path.string() + ": " +
                             error_string(a.get()));
  }
}

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

// libarchive only reads forward, so reopen the archive and skip to the
// index'th entry to read the start of its data.
static std::string entry_prefix(const std::filesystem::path &path, size_t index,
                                std::string &error) {
  // most file formats have their "file magic" within this number of bytes
  static constexpr size_t type_prefix_size = 1024 * 16;

  archive_ptr a = open_archive(path);
  archive_entry *ent;
  for (size_t i = 0; i <= index; i++) {
    int status = archive_read_next_header(a.get(), &ent);
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      error = status == ARCHIVE_EOF ? "unexpected end of archive"
                                    : error_string(a.get());
      return "";
    }
  }

  if (archive_entry_is_data_encrypted(ent) == 1) {
    error = "encrypted";
    return "";
  }

  std::string data(type_prefix_size, '\0');
  size_t size = 0;
  while (size < data.size()) {
    la_ssize_t n =
        archive_read_data(a.get(), data.data() + size, data.size() - size);
    if (n < 0) {
      error = error_string(a.get());
      return "";
    }
    if (n == 0) {
      break;
    }
    size += n;
  }
  data.resize(size);
  return data;
}

static std::string entry_type(const std::filesystem::path &path, size_t index,
                              bool mime) {
  std::string error;
  std::string data;
  try {
    data = entry_prefix(path, index, error);
  } catch (const std::exception &e) {
    error = e.what();
  }

  if (error == "encrypted") {
    return error;
  }
  if (!error.empty()) {
    return "unreadable (" + error + ")";
  }
  if (data.empty()) {
    return mime ? "application/x-zerosize" : "empty";
  }
  return mime ? Format::mime_type(data.data(), data.size())
              : Format::file_type(data.data(), data.size());
}

std::vector<Format::Entry> LibarchiveFormat::files() {
  std::vector<Entry> root;

  archive_ptr a = open_archive(path);
  archive_entry *ent;
  for (size_t index = 0;; index++) {
    int status = archive_read_next_header(a.get(), &ent);
    if (status == ARCHIVE_EOF) {
      break;
    }
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      break;
    }

    // Intermediate directories may not have their own entries, so create them
    // as we go. Paths can start with "./" and directories end in '/'.
    std::string path = entry_path(ent);
    bool is_dir = archive_entry_filetype(ent) == AE_IFDIR;
    std::vector<Entry> *level = &root;
    size_t start = 0;
    while (start <= path.size()) {
      size_t slash = path.find('/', start);
      std::string part = path.substr(start, slash - start);
      bool last = slash == std::string::npos ||
                  path.find_first_not_of('/', slash) == std::string::npos;
      std::string full_path = path.substr(0, slash);

      if (!part.empty() && part != ".") {
        Entry &entry = child(*level, part, full_path);
        if (!last || is_dir) {
          entry.is_dir = true;
          entry.set_type("directory");
          entry.set_mime_type("directory");
          level = &entry.entries;
        } else if (archive_entry_filetype(ent) == AE_IFLNK) {
          const char *target = archive_entry_symlink_utf8(ent);
          entry.set_type(std::string("symbolic link to ") +
                         (target ? target : "?"));
          entry.set_mime_type("inode/symlink");
        } else {
          entry.set_type_function([path = this->path, index] {
            return entry_type(path, index, false);
          });
          entry.set_mime_type_function([path = this->path, index] {
            return entry_type(path, index, true);
          });
        }
      }

      if (last) {
        break;
      }
      start = slash + 1;
    }
  }

  return root;
}
