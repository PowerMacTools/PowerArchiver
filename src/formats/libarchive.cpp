#include "libarchive.hpp"
#include "../fs/fs.hpp"
#include "formats.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <string>

using archive_ptr = std::unique_ptr<archive, decltype(&archive_read_free)>;

static std::string error_string(archive *a) {
  const char *error = archive_error_string(a);
  return error ? error : "unknown error";
}

// archive_read_open_filename() needs fstat(), which classic Mac OS lacks, so
// read the file through stdio instead.
namespace {

struct source {
  std::string path;
  FILE *file = nullptr;
  char buf[64 * 1024];
};

int source_open(archive *a, void *data) {
  source *src = (source *)data;
  src->file = fs::open(src->path, "rb");
  if (!src->file) {
    archive_set_error(a, errno, "failed to open %s", src->path.c_str());
    return ARCHIVE_FATAL;
  }
  return ARCHIVE_OK;
}

la_ssize_t source_read(archive *a, void *data, const void **buf) {
  source *src = (source *)data;
  size_t n = fread(src->buf, 1, sizeof(src->buf), src->file);
  if (n == 0 && ferror(src->file)) {
    archive_set_error(a, errno, "failed to read %s", src->path.c_str());
    return ARCHIVE_FATAL;
  }
  *buf = src->buf;
  return n;
}

la_int64_t source_seek(archive *a, void *data, la_int64_t offset, int whence) {
  source *src = (source *)data;
  if (fseek(src->file, (long)offset, whence) != 0) {
    archive_set_error(a, errno, "failed to seek in %s", src->path.c_str());
    return ARCHIVE_FATAL;
  }
  return ftell(src->file);
}

la_int64_t source_skip(archive *a, void *data, la_int64_t request) {
  source *src = (source *)data;
  return fseek(src->file, (long)request, SEEK_CUR) == 0 ? request : 0;
}

// Also called when opening fails, so the file may not be open.
int source_close(archive *a, void *data) {
  source *src = (source *)data;
  if (src->file) {
    fclose(src->file);
  }
  delete src;
  return ARCHIVE_OK;
}

} // namespace

// Returns a null archive_ptr, and why in error, on failure.
static archive_ptr open_archive(const std::string &path, std::string &error) {
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

  // source_close() frees src from here on, even if opening fails
  source *src = new source;
  src->path = path;
  archive_read_set_callback_data(a.get(), src);
  archive_read_set_open_callback(a.get(), source_open);
  archive_read_set_read_callback(a.get(), source_read);
  archive_read_set_seek_callback(a.get(), source_seek);
  archive_read_set_skip_callback(a.get(), source_skip);
  archive_read_set_close_callback(a.get(), source_close);
  if (archive_read_open1(a.get()) != ARCHIVE_OK) {
    error = "failed to open " + path + ": " + error_string(a.get());
    return archive_ptr(nullptr, archive_read_free);
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

LibarchiveFormat *LibarchiveFormat::open(const std::string &path,
                                         std::string &error) {
  // make sure libarchive recognizes it before we claim the file
  archive_ptr a = open_archive(path, error);
  if (!a) {
    return nullptr;
  }
  archive_entry *ent;
  int status = archive_read_next_header(a.get(), &ent);
  if (status != ARCHIVE_OK && status != ARCHIVE_WARN && status != ARCHIVE_EOF) {
    error = "failed to read " + path + ": " + error_string(a.get());
    return nullptr;
  }
  return new LibarchiveFormat(path);
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
static std::string entry_prefix(const std::string &path, size_t index,
                                std::string &error) {
  // most file formats have their "file magic" within this number of bytes
  static constexpr size_t type_prefix_size = 1024 * 16;

  std::string data(type_prefix_size, '\0');
  archive_ptr a = open_archive(path, error);
  archive_entry *ent;
  size_t size = 0;

  if (!a) {
    return "";
  }

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

static std::string entry_type(const std::string &path, size_t index,
                              bool mime) {
  std::string error;
  std::string data = entry_prefix(path, index, error);

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

  std::string error;
  archive_ptr a = open_archive(path, error);
  if (!a) {
    return root;
  }
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

namespace {

using write_ptr = std::unique_ptr<archive, decltype(&archive_write_free)>;

// Writers put an entry on disk in three parts: begin() with its header, data()
// for each block, and finish(). Each returns false, with why in error, on
// failure.

#ifndef macintosh

// Writes entries with libarchive's own disk writer, which also restores
// permissions, times and links.
class Writer {
public:
  Writer() : out(archive_write_disk_new(), archive_write_free) {
    // fs::join_member() refuses '..', so ARCHIVE_EXTRACT_SECURE_NODOTDOT
    // isn't needed, and it would also refuse a '..' in dir itself.
    archive_write_disk_set_options(out.get(),
                                   ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                       ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    archive_write_disk_set_standard_lookup(out.get());
  }

  // Starts writing ent at target, setting wants_data if data() should follow.
  bool begin(archive_entry *ent, const std::string &target,
             const std::string &dir, std::vector<std::string> &skipped,
             bool &wants_data, std::string &error) {
    archive_entry_set_pathname_utf8(ent, target.c_str());
    // Hardlink targets are paths inside the archive too.
    const char *hardlink = archive_entry_hardlink_utf8(ent);
    if (hardlink) {
      std::string link_target;
      if (!fs::join_member(dir, hardlink, link_target, error)) {
        return false;
      }
      archive_entry_set_hardlink_utf8(ent, link_target.c_str());
    }
    int status = archive_write_header(out.get(), ent);
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      error = error_string(out.get());
      return false;
    }
    wants_data = true;
    return true;
  }

  bool data(const void *buf, size_t size, la_int64_t offset,
            std::string &error) {
    if (archive_write_data_block(out.get(), buf, size, offset) < ARCHIVE_WARN) {
      error = error_string(out.get());
      return false;
    }
    return true;
  }

  bool finish(std::string &error) {
    if (archive_write_finish_entry(out.get()) < ARCHIVE_WARN) {
      error = error_string(out.get());
      return false;
    }
    return true;
  }

  void close() { archive_write_close(out.get()); }

private:
  write_ptr out;
};

#else

// archive_write_disk needs POSIX, so write directories and plain files with
// fs and stdio. HFS has nothing like links, so those are skipped.
class Writer {
public:
  ~Writer() {
    if (file) {
      fclose(file);
    }
  }

  bool begin(archive_entry *ent, const std::string &target,
             const std::string &dir, std::vector<std::string> &skipped,
             bool &wants_data, std::string &error) {
    wants_data = false;
    if (archive_entry_filetype(ent) == AE_IFDIR) {
      return fs::create_directories(target, error);
    }
    if (archive_entry_filetype(ent) != AE_IFREG ||
        archive_entry_hardlink(ent)) {
      skipped.push_back(entry_path(ent));
      return true;
    }

    if (!fs::create_directories(fs::parent(target), error)) {
      return false;
    }
    file = fs::open(target, "wb");
    if (!file) {
      error = "failed to create " + target;
      return false;
    }
    path = target;
    position = 0;
    wants_data = true;
    return true;
  }

  bool data(const void *buf, size_t size, la_int64_t offset,
            std::string &error) {
    // sparse files skip ahead
    if (offset != position && fseek(file, (long)offset, SEEK_SET) != 0) {
      error = "failed to seek";
      return false;
    }
    if (fwrite(buf, 1, size, file) != size) {
      error = "failed to write";
      return false;
    }
    position = offset + size;
    return true;
  }

  bool finish(std::string &error) {
    if (file) {
      int status = fclose(file);
      file = nullptr;
      if (status != 0) {
        error = "failed to write";
        return false;
      }
      // archives from elsewhere carry no Finder info, and every file would
      // otherwise open as text
      fs::guess_finder_info(path);
    }
    return true;
  }

  void close() {}

private:
  FILE *file = nullptr;
  std::string path;
  la_int64_t position = 0;
};

#endif

// Each step reads either one entry's header or one block of its data, so a
// single large file doesn't hold things up.
class LibarchiveExtraction : public Format::Extraction {
public:
  LibarchiveExtraction(const std::string &path, const std::string &dir)
      : path(path), dir(dir), in(open_archive(path, open_error)),
        total(fs::file_size(path)) {}

  bool step() override {
    if (!in) {
      return fail(open_error);
    }
    if (!in_entry) {
      return next_entry();
    }

    const void *buf;
    size_t size;
    la_int64_t offset;
    std::string error;
    int status = archive_read_data_block(in.get(), &buf, &size, &offset);
    if (status == ARCHIVE_EOF) {
      in_entry = false;
      return writer.finish(error) || failed_entry(error);
    }
    if (status != ARCHIVE_OK) {
      return fail("failed to read " + name + ": " + error_string(in.get()));
    }
    return writer.data(buf, size, offset, error) || failed_entry(error);
  }

  double progress() override {
    if (total == 0 || !in) {
      return 0;
    }
    double done = (double)archive_filter_bytes(in.get(), -1) / total;
    return done < 0 ? 0 : done > 1 ? 1 : done;
  }

  const std::vector<std::string> &skipped() override { return skipped_names; }

private:
  bool next_entry() {
    archive_entry *ent;
    int status = archive_read_next_header(in.get(), &ent);
    if (status == ARCHIVE_EOF) {
      writer.close();
      return false;
    }
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      return fail("failed to read " + path + ": " + error_string(in.get()));
    }

    name = entry_path(ent);
    std::string target, error;
    if (!fs::join_member(dir, name, target, error) ||
        !writer.begin(ent, target, dir, skipped_names, in_entry, error)) {
      return failed_entry(error);
    }
    return true;
  }

  // Fails with the current entry's name on the writer's error.
  bool failed_entry(const std::string &error) {
    return fail("failed to extract " + name + ": " + error);
  }

  std::string path, dir, open_error;
  archive_ptr in;
  Writer writer;
  std::uint64_t total;
  std::string name;
  std::vector<std::string> skipped_names;
  bool in_entry = false;
};

} // namespace

std::unique_ptr<Format::Extraction>
LibarchiveFormat::extract(const std::string &dir) {
  return std::make_unique<LibarchiveExtraction>(path, dir);
}
