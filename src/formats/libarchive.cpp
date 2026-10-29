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

  std::string data(type_prefix_size, '\0');
  archive_ptr a = open_archive(path);
  archive_entry *ent;
  size_t size = 0;

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

// Roots an archive path in dir.
static std::string rooted(const std::filesystem::path &dir,
                          const std::string &name) {
  size_t start = name.find_first_not_of('/');
  return (dir / (start == std::string::npos ? "" : name.substr(start)))
      .string();
}

class LibarchiveExtraction : public Format::Extraction {
public:
  LibarchiveExtraction(const std::filesystem::path &path,
                       const std::filesystem::path &dir)
      : path(path),
        // canonical so that ARCHIVE_EXTRACT_SECURE_NODOTDOT only sees '..'
        // coming from the archive itself
        root(std::filesystem::weakly_canonical(dir)), in(open_archive(path)),
        out(archive_write_disk_new(), archive_write_free),
        total(std::filesystem::file_size(path)) {
    // No ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS, since every path is rooted
    // in dir and so absolute. rooted() takes care of absolute entry paths
    // instead.
    archive_write_disk_set_options(out.get(),
                                   ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                       ARCHIVE_EXTRACT_SECURE_SYMLINKS |
                                       ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_write_disk_set_standard_lookup(out.get());
  }

  bool step() override {
    if (!in_entry) {
      return next_entry();
    }

    const void *buf;
    size_t size;
    la_int64_t offset;
    int status = archive_read_data_block(in.get(), &buf, &size, &offset);
    if (status == ARCHIVE_EOF) {
      if (archive_write_finish_entry(out.get()) < ARCHIVE_WARN) {
        throw std::runtime_error("failed to extract " + name + ": " +
                                 error_string(out.get()));
      }
      in_entry = false;
      return true;
    }
    if (status != ARCHIVE_OK) {
      throw std::runtime_error("failed to read " + name + ": " +
                               error_string(in.get()));
    }
    if (archive_write_data_block(out.get(), buf, size, offset) < ARCHIVE_WARN) {
      throw std::runtime_error("failed to write " + name + ": " +
                               error_string(out.get()));
    }
    return true;
  }

  double progress() override {
    if (total == 0) {
      return 0;
    }
    double done = (double)archive_filter_bytes(in.get(), -1) / total;
    return done < 0 ? 0 : done > 1 ? 1 : done;
  }

private:
  bool next_entry() {
    archive_entry *ent;
    int status = archive_read_next_header(in.get(), &ent);
    if (status == ARCHIVE_EOF) {
      archive_write_close(out.get());
      return false;
    }
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      throw std::runtime_error("failed to read " + path.string() + ": " +
                               error_string(in.get()));
    }

    // Hardlink targets are paths inside the archive too.
    name = entry_path(ent);
    archive_entry_set_pathname_utf8(ent, rooted(root, name).c_str());
    const char *hardlink = archive_entry_hardlink_utf8(ent);
    if (hardlink) {
      archive_entry_set_hardlink_utf8(ent, rooted(root, hardlink).c_str());
    }

    status = archive_write_header(out.get(), ent);
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      throw std::runtime_error("failed to extract " + name + ": " +
                               error_string(out.get()));
    }
    in_entry = true;
    return true;
  }

  using write_ptr = std::unique_ptr<archive, decltype(&archive_write_free)>;
  std::filesystem::path path, root;
  archive_ptr in;
  write_ptr out;
  std::uintmax_t total;
  std::string name;
  bool in_entry = false;
};

std::unique_ptr<Format::Extraction>
LibarchiveFormat::extract(const std::filesystem::path &dir) {
  return std::make_unique<LibarchiveExtraction>(path, dir);
}
