#ifdef macintosh

#include "fs.hpp"

#include <Mw/Milsko.h>

#include <Files.h>
#include <Finder.h>
#include <InternetConfig.h>
#include <Navigation.h>
#include <cstdlib>
#include <cstring>

namespace fs {

// HFS file names are at most 31 bytes of MacRoman, one byte per character.
static constexpr std::size_t max_name = 31;

static std::string to_macroman(const std::string &utf8) {
  char *converted = MwUTF8TextToACPText(utf8.c_str());
  std::string result = converted;
  free(converted);
  return result;
}

// File Manager calls take full paths as Pascal strings.
static bool to_pstring(Str255 out, const std::string &path,
                       std::string &error) {
  std::string macroman = to_macroman(path);
  if (macroman.size() > 255) {
    error = "path too long: " + path;
    return false;
  }
  out[0] = (unsigned char)macroman.size();
  std::memcpy(out + 1, macroman.data(), macroman.size());
  return true;
}

static bool failed(std::string &error, const std::string &what,
                   const std::string &path, OSErr err) {
  error = what + " " + path + " (error " + std::to_string(err) + ")";
  return false;
}

std::string component(const std::string &name) {
  // ':' separates paths, so swap it for '/' as Mac OS X does the other way
  // round, and keep to max_name characters, counting UTF-8 lead bytes.
  std::string result;
  std::size_t chars = 0;
  for (char ch : name) {
    bool lead = ((unsigned char)ch & 0xc0) != 0x80;
    if (lead && chars++ == max_name) {
      break;
    }
    result += ch == ':' ? '/' : ch;
  }
  return result;
}

std::string join(const std::string &dir, const std::string &name) {
  if (dir.empty()) {
    return ":" + name;
  }
  return dir.back() == ':' ? dir + name : dir + ":" + name;
}

std::string parent(const std::string &path) {
  std::size_t end = path.find_last_not_of(':');
  if (end == std::string::npos) {
    return path;
  }
  std::size_t colon = path.rfind(':', end);
  if (colon == std::string::npos) {
    return "";
  }
  // keep the colon after a volume name, since "Macintosh HD" alone would be
  // a file in the current directory
  return path.find(':') == colon ? path.substr(0, colon + 1)
                                 : path.substr(0, colon);
}

std::string name(const std::string &path) {
  std::size_t end = path.find_last_not_of(':');
  if (end == std::string::npos) {
    return "";
  }
  std::size_t colon = path.rfind(':', end);
  std::size_t start = colon == std::string::npos ? 0 : colon + 1;
  return path.substr(start, end + 1 - start);
}

bool create_directories(const std::string &path, std::string &error) {
  // The first component of a full path is the volume, which already exists.
  // A path starting with ':' is relative and has no volume.
  std::size_t colon = path[0] == ':' ? 0 : path.find(':');
  while (colon != std::string::npos && colon + 1 < path.size()) {
    std::size_t next = path.find(':', colon + 1);
    std::string prefix = path.substr(0, next);

    Str255 name;
    long id;
    if (!to_pstring(name, prefix, error)) {
      return false;
    }
    OSErr err = DirCreate(0, 0, name, &id);
    if (err != noErr && err != dupFNErr) {
      return failed(error, "failed to create", prefix, err);
    }
    colon = next;
  }
  return true;
}

FILE *open(const std::string &path, const char *mode) {
  // Retro68's open() copies the path into a Pascal string as is, so it has
  // to be MacRoman and fit in 255 bytes.
  std::string macroman = to_macroman(path);
  if (macroman.size() > 255) {
    return nullptr;
  }
  return fopen(macroman.c_str(), mode);
}

bool write_resource_fork(const std::string &path, const void *data,
                         std::size_t size, std::string &error) {
  Str255 name;
  short ref;
  if (!to_pstring(name, path, error)) {
    return false;
  }
  OSErr err = HOpenRF(0, 0, name, fsRdWrPerm, &ref);
  if (err != noErr) {
    return failed(error, "failed to open resource fork of", path, err);
  }

  long count = size;
  err = SetEOF(ref, 0);
  if (err == noErr) {
    err = FSWrite(ref, &count, data);
  }
  FSClose(ref);
  if (err != noErr) {
    return failed(error, "failed to write resource fork of", path, err);
  }
  return true;
}

// Internet Config, which is loaded at run time, since it only comes with
// Mac OS 8.5 and later.
struct internet_config {
  OSStatus (*start)(ICInstance *instance, OSType creator);
  OSStatus (*find_config_file)(ICInstance instance, long count,
                               ICDirSpecArrayPtr folders);
  OSStatus (*map_filename)(ICInstance instance, ConstStr255Param filename,
                           ICMapEntry *entry);
  ICInstance instance = nullptr;
};

// Returns Internet Config, connected for the life of the program, or nullptr
// if it isn't available.
static internet_config *open_internet_config() {
  static internet_config ic;
  static bool tried = false;
  if (tried) {
    return ic.instance ? &ic : nullptr;
  }
  tried = true;

  void *lib = MwDynamicOpen("InternetConfigLib");
  if (!lib) {
    return nullptr;
  }
  ic.start = (decltype(ic.start))MwDynamicSymbol(lib, "ICStart");
  ic.find_config_file =
      (decltype(ic.find_config_file))MwDynamicSymbol(lib, "ICFindConfigFile");
  ic.map_filename =
      (decltype(ic.map_filename))MwDynamicSymbol(lib, "ICMapFilename");
  if (!ic.start || !ic.find_config_file || !ic.map_filename ||
      ic.start(&ic.instance, '????') != noErr) {
    ic.instance = nullptr;
    return nullptr;
  }
  // before Carbon, the preferences have to be found by hand
  if (ic.find_config_file(ic.instance, 0, nullptr) != noErr) {
    ic.instance = nullptr;
    return nullptr;
  }
  return &ic;
}

void guess_finder_info(const std::string &path) {
  internet_config *ic = open_internet_config();
  Str255 file_name, full_path;
  std::string error;
  ICMapEntry entry;
  FInfo info;
  if (!ic || !to_pstring(file_name, name(path), error) ||
      ic->map_filename(ic->instance, file_name, &entry) != noErr ||
      !to_pstring(full_path, path, error) ||
      HGetFInfo(0, 0, full_path, &info) != noErr) {
    return;
  }
  info.fdType = entry.fileType;
  info.fdCreator = entry.fileCreator;
  HSetFInfo(0, 0, full_path, &info);
}

bool set_finder_info(const std::string &path, const std::uint8_t type[4],
                     const std::uint8_t creator[4], std::uint16_t flags,
                     std::string &error) {
  Str255 name;
  FInfo info;
  if (!to_pstring(name, path, error)) {
    return false;
  }
  OSErr err = HGetFInfo(0, 0, name, &info);
  if (err == noErr) {
    info.fdType = (OSType)type[0] << 24 | (OSType)type[1] << 16 |
                  (OSType)type[2] << 8 | type[3];
    info.fdCreator = (OSType)creator[0] << 24 | (OSType)creator[1] << 16 |
                     (OSType)creator[2] << 8 | creator[3];
    // let the Finder lay the file out fresh rather than at the position it
    // had on the machine that made the archive
    info.fdFlags = flags & ~(kHasBeenInited | kIsOnDesk);
    info.fdLocation.h = info.fdLocation.v = 0;
    err = HSetFInfo(0, 0, name, &info);
  }
  if (err != noErr) {
    return failed(error, "failed to set Finder info of", path, err);
  }
  return true;
}

// Navigation Services; loaded at run time, since it only comes with Mac OS 8.5
// and later.
struct navigation {
  OSErr (*get_default_options)(NavDialogOptions *options);
  OSErr (*get_file)(AEDesc *location, NavReplyRecord *reply,
                    NavDialogOptions *options, NavEventUPP event,
                    NavPreviewUPP preview, NavObjectFilterUPP filter,
                    NavTypeListHandle types, void *user);
  OSErr (*choose_folder)(AEDesc *location, NavReplyRecord *reply,
                         NavDialogOptions *options, NavEventUPP event,
                         NavObjectFilterUPP filter, void *user);
  OSErr (*dispose_reply)(NavReplyRecord *reply);
};

// returns Navigation Services or null if it isn't available.
static navigation *open_navigation() {
  static navigation nav;
  static bool loaded = false, tried = false;
  if (tried) {
    return loaded ? &nav : nullptr;
  }
  tried = true;

  void *lib = MwDynamicOpen("NavigationLib");
  if (!lib) {
    return nullptr;
  }
  auto can_run = (Boolean (*)())MwDynamicSymbol(lib, "NavServicesCanRun");
  nav.get_default_options = (decltype(nav.get_default_options))MwDynamicSymbol(
      lib, "NavGetDefaultDialogOptions");
  nav.get_file = (decltype(nav.get_file))MwDynamicSymbol(lib, "NavGetFile");
  nav.choose_folder =
      (decltype(nav.choose_folder))MwDynamicSymbol(lib, "NavChooseFolder");
  nav.dispose_reply =
      (decltype(nav.dispose_reply))MwDynamicSymbol(lib, "NavDisposeReply");
  loaded = can_run && nav.get_default_options && nav.get_file &&
           nav.choose_folder && nav.dispose_reply && can_run();
  return loaded ? &nav : nullptr;
}

static void set_pstring(Str255 out, const std::string &utf8) {
  std::string macroman = to_macroman(utf8).substr(0, 255);
  out[0] = (unsigned char)macroman.size();
  std::memcpy(out + 1, macroman.data(), macroman.size());
}

// Builds an FSSpec's full path by walking up its parent directories.
static bool full_path(const FSSpec &spec, std::string &path) {
  std::string macroman((const char *)spec.name + 1, spec.name[0]);
  if (spec.parID == fsRtParID) {
    // the volume itself
    macroman += ":";
  }
  for (long dir = spec.parID; dir != fsRtParID;) {
    Str255 name;
    CInfoPBRec pb = {};
    pb.dirInfo.ioNamePtr = name;
    pb.dirInfo.ioVRefNum = spec.vRefNum;
    pb.dirInfo.ioFDirIndex = -1;
    pb.dirInfo.ioDrDirID = dir;
    if (PBGetCatInfoSync(&pb) != noErr) {
      return false;
    }
    macroman = std::string((const char *)name + 1, name[0]) + ":" + macroman;
    dir = pb.dirInfo.ioDrParID;
  }

  char *utf8 = MwACPTextToUTF8Text(macroman.c_str());
  path = utf8;
  free(utf8);
  return true;
}

static choice run_dialog(const std::string &title, bool folder,
                         std::string &path) {
  navigation *nav = open_navigation();
  NavDialogOptions options;
  if (!nav || nav->get_default_options(&options) != noErr) {
    return choice::unavailable;
  }
  options.dialogOptionFlags &= ~kNavAllowMultipleFiles;
  options.dialogOptionFlags |= kNavNoTypePopup | kNavDontAutoTranslate;
  set_pstring(options.windowTitle, title);
  set_pstring(options.clientName, "PowerArchiver");

  NavReplyRecord reply;
  OSErr err = folder ? nav->choose_folder(nullptr, &reply, &options, nullptr,
                                          nullptr, nullptr)
                     : nav->get_file(nullptr, &reply, &options, nullptr,
                                     nullptr, nullptr, nullptr, nullptr);
  if (err != noErr || !reply.validRecord) {
    if (err == noErr) {
      nav->dispose_reply(&reply);
    }
    return choice::cancelled;
  }

  FSSpec spec;
  AEKeyword keyword;
  DescType type;
  Size size;
  bool ok = AEGetNthPtr(&reply.selection, 1, typeFSS, &keyword, &type, &spec,
                        sizeof spec, &size) == noErr &&
            full_path(spec, path);
  nav->dispose_reply(&reply);
  return ok ? choice::chosen : choice::cancelled;
}

choice choose_file(const std::string &title, std::string &path) {
  return run_dialog(title, false, path);
}

choice choose_folder(const std::string &title, std::string &path) {
  return run_dialog(title, true, path);
}

} // namespace fs

#endif
