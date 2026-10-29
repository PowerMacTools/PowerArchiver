#include <Mw/Milsko.h>

#include "Mw/Core.h"
#include "formats/formats.hpp"
#include "icon.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct context {
  Format::Entry entry;
  static MwWidget window, menu, panes, filelist;
  static MwWidget table, table_name, table_type, table_icon;
  static MwMenu open_menu, extract_menu;

  // The open archive, and its tree view items' entries.
  static std::unique_ptr<Format> format;
  static std::unordered_map<void *, context> entries;

  static std::unique_ptr<Format::Extraction> extraction;
  static std::string extract_dir;
  static MwWidget progress_window, progress_bar;
};

MwWidget context::window, context::menu, context::panes, context::filelist;
MwWidget context::table, context::table_name, context::table_type,
    context::table_icon;
MwMenu context::open_menu, context::extract_menu;
std::unique_ptr<Format> context::format;
std::unordered_map<void *, context> context::entries;
std::unique_ptr<Format::Extraction> context::extraction;
std::string context::extract_dir;
MwWidget context::progress_window, context::progress_bar;

static std::string wrap_text(MwWidget widget, std::string str, int width = -1) {
  std::string strbuf;
  std::string strfinal;

  int handle_width = width >= 0 ? width : MwGetInteger(widget, MwNwidth);
  for (auto ch : str) {
    if (ch == '\n') {
      strfinal += strbuf + "\n";
      strbuf.clear();
      continue;
    }
    strbuf += ch;
    if (strbuf.size() < 2 ||
        MwTextWidth(widget, NULL, strbuf.c_str()) <= handle_width - 3) {
      continue;
    }

    // break at the line's last space, or mid-word if it has none
    size_t space = strbuf.find_last_of(' ');
    if (space == std::string::npos || space == 0) {
      strfinal += strbuf.substr(0, strbuf.size() - 1) + "\n";
      strbuf = strbuf.substr(strbuf.size() - 1);
    } else {
      strfinal += strbuf.substr(0, space) + "\n";
      strbuf = strbuf.substr(space + 1);
    }
  }
  strfinal += strbuf;

  return strfinal;
};

static void show_message(const std::string &text, const char *title,
                         unsigned int icon) {
  static constexpr int label_width = 512 - (8 + 48 + 8) - 8;
  std::string wrapped = wrap_text(context::window, text, label_width);
  MwWidget msgbox = MwMessageBox(context::window, wrapped.c_str(), title,
                                 icon | MwMB_BUTTONOK);
  auto close = +[](MwWidget handle, void *user, void *call) {
    MwDestroyWidget((MwWidget)user);
  };
  MwAddUserHandler(MwMessageBoxGetChild(msgbox, MwMB_BUTTONOK),
                   MwNactivateHandler, close, msgbox);
  MwAddUserHandler(msgbox, MwNcloseHandler, close, msgbox);
}

static void stop_extraction() {
  context::extraction.reset();
  MwSetInteger(context::window, MwNwaitMS, MwDEFAULT);
  if (context::progress_window) {
    MwDestroyWidget(context::progress_window);
    context::progress_window = NULL;
    context::progress_bar = NULL;
  }
}

static void show_progress() {
  int w = 320, h = 96;
  MwSizeHints sh;
  MwWidget cancel;

  sh.min_width = sh.max_width = w;
  sh.min_height = sh.max_height = h;

  context::progress_window =
      MwVaCreateWidget(MwWindowClass, NULL, NULL,
                       MwGetInteger(context::window, MwNx) +
                           (MwGetInteger(context::window, MwNwidth) - w) / 2,
                       MwGetInteger(context::window, MwNy) +
                           (MwGetInteger(context::window, MwNheight) - h) / 2,
                       w, h, MwNtitle, "Extracting", MwNsizeHints, &sh, NULL);
  MwReparent(context::progress_window, context::window);

  MwVaCreateWidget(MwLabelClass, NULL, context::progress_window, 8, 8, w - 16,
                   20, MwNtext, "Extracting...", MwNalignment,
                   MwALIGNMENT_BEGINNING, NULL);
  context::progress_bar = MwCreateWidget(
      MwProgressBarClass, NULL, context::progress_window, 8, 32, w - 16, 20);
  cancel = MwVaCreateWidget(MwButtonClass, NULL, context::progress_window,
                            w - 88, h - 32, 80, 24, MwNtext, "Cancel", NULL);

  auto on_cancel =
      +[](MwWidget handle, void *user, void *call) { stop_extraction(); };
  MwAddUserHandler(cancel, MwNactivateHandler, on_cancel, NULL);
  MwAddUserHandler(context::progress_window, MwNcloseHandler, on_cancel, NULL);
}

static void extract_to(MwWidget handle, void *user, void *call) {
  const char *dir = (const char *)call;

  if (context::extraction) {
    show_message("An extraction is already running.", "Extract",
                 MwMB_ICONERROR);
    return;
  }
  context::extraction = context::format->extract(dir);
  context::extract_dir = dir;
  show_progress();
  // tick often while extracting, leaving the event loop a short gap between
  // each budget's worth of work
  MwSetInteger(context::window, MwNwaitMS, 10);
}

static void extract_tick(MwWidget handle, void *user, void *call) {
  using clock = std::chrono::steady_clock;
  static constexpr auto budget = std::chrono::milliseconds(25);

  if (!context::extraction) {
    return;
  }

  auto start = clock::now();
  bool more = true;
  while (more && clock::now() - start < budget) {
    more = context::extraction->step();
  }
  if (more) {
    MwSetInteger(context::progress_bar, MwNvalue,
                 (int)(context::extraction->progress() * 100));
    return;
  }
  if (!context::extraction->error().empty()) {
    std::string error = context::extraction->error();
    stop_extraction();
    show_message(error, "Extract", MwMB_ICONERROR);
    return;
  }

  std::string message = "Extracted to " + context::extract_dir;
  // copied, since stop_extraction() frees the extraction
  std::vector<std::string> skipped = context::extraction->skipped();
  if (!skipped.empty()) {
    message += "\n" + std::to_string(skipped.size()) +
               " unsupported file(s) skipped, including " + skipped.front();
  }
  stop_extraction();
  show_message(message, "Extract",
               skipped.empty() ? MwMB_ICONINFO : MwMB_ICONWARNING);
}

static void populate_treeview(void *parent,
                              const std::vector<Format::Entry> &files) {
  for (auto &entry : files) {
    context l;

    void *p =
        MwTreeViewAdd(context::filelist, parent, NULL, entry.name.c_str());
    l.entry = entry;
    context::entries.insert_or_assign(p, l);
    if (entry.is_dir) {
      if (parent)
        MwTreeViewSetOpened(context::filelist, p, MwFALSE);
      populate_treeview(p, entry.entries);
    }
  }
}

static void open_archive(const std::string &path) {
  std::string error;
  std::unique_ptr<Format> format(Format::from_file(path, error));
  if (!format) {
    show_message(error, "Open", MwMB_ICONERROR);
    return;
  }

  // an extraction in progress reads from the old archive
  stop_extraction();
  context::format = std::move(format);
  MwTreeViewReset(context::filelist);
  context::entries.clear();
  // the selection's details belonged to the old archive
  MwVaApply(context::table_name, MwNtext, "", NULL);
  MwVaApply(context::table_type, MwNtext, "", NULL);
  MwVaApply(context::table_icon, MwNpixmap, NULL, NULL);
  populate_treeview(NULL, context::format->files());
  MwForceRender(context::window);
}

int main(int argc, char *argv[]) {
  MwLibraryInit();

  context::window =
      MwCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT, 640, 480);

  context::menu =
      MwCreateWidget(MwMenuClass, NULL, context::window, 0, 0, 640, 32);
  context::panes = MwVaCreateWidget(MwTableClass, NULL, context::window, 0, 27,
                                    640, 480 - 27, MwNorientation, MwHORIZONTAL,
                                    MwNcolumns, 4, NULL);

  context::filelist = MwVaCreateWidget(
      MwTreeViewClass, NULL, context::panes, 0, 0, 640, 480, MwNratio, 255,
      MwNsingleClickSelectable, 1, MwNcolumnSpan, 3, NULL);
  MwAddUserHandler(
      context::filelist, MwNtreeViewActivateHandler,
      +[](MwWidget handle, void *user, void *call) {
        auto found = context::entries.find(call);
        if (found == context::entries.end()) {
          return;
        }
        context &lmao = found->second;
        std::string wrapped_name, wrapped_type;
        MwPixmap icon = NULL;

        wrapped_name = wrap_text(context::table, lmao.entry.name);
        wrapped_type = wrap_text(context::table, lmao.entry.type());

        icon = get_pixmap_from_mime_type(lmao.entry.mime_type().c_str());

        if (icon) {
          MwVaApply(context::table_icon, MwNpixmap, icon, NULL);
        }
        MwVaApply(context::table_name, MwNtext, wrapped_name.c_str(), NULL);
        MwVaApply(context::table_type, MwNtext, wrapped_type.c_str(), NULL);
        MwForceRender(context::window);
      },
      NULL);

  context::table = MwVaCreateWidget(MwTableClass, NULL, context::panes, 0, 32,
                                    640, 480, MwNcolumns, 2, MwNratio, 1, NULL);
  context::table_name = MwVaCreateWidget(MwLabelClass, NULL, context::table, 0,
                                         0, 640, 480, MwNcolumnSpan, 2, NULL);
  context::table_icon =
      MwVaCreateWidget(MwImageClass, NULL, context::table, 0, 0, 640, 480,
                       MwNcolumnSpan, 2, MwNfillArea, 0, NULL);
  context::table_name = MwVaCreateWidget(MwLabelClass, NULL, context::table, 0,
                                         0, 640, 480, MwNcolumnSpan, 2, NULL);
  context::table_type = MwVaCreateWidget(MwLabelClass, NULL, context::table, 0,
                                         0, 640, 480, MwNcolumnSpan, 2, NULL);

  setup_pixmap_table(context::table_icon);

  MwAddUserHandler(context::window, MwNtickHandler, extract_tick, NULL);

  context::open_menu = MwMenuAdd(context::menu, NULL, "Open");
  context::extract_menu = MwMenuAdd(context::menu, NULL, "Extract");
  MwMenuAdd(context::menu, NULL, "Find");
  MwAddUserHandler(
      context::menu, MwNmenuHandler,
      +[](MwWidget handle, void *user, void *call) {
        MwWidget chooser;

        if (call == context::open_menu) {
          chooser = MwFileChooser(context::window, "Open");
          MwSetString(chooser, MwNbackground,
                      MwGetInteger(chooser, MwNdarkTheme)
                          ? MwDefaultDarkBackground
                          : MwDefaultBackground);
          MwAddUserHandler(
              chooser, MwNfileChosenHandler,
              +[](MwWidget handle, void *user, void *call) {
                open_archive((const char *)call);
              },
              NULL);
          return;
        }
        if (call != context::extract_menu) {
          return;
        }
        if (!context::format) {
          show_message("No archive is open.", "Extract", MwMB_ICONERROR);
          return;
        }
        chooser = MwDirectoryChooser(context::window, "Extract to");
        MwSetString(chooser, MwNbackground,
                    MwGetInteger(chooser, MwNdarkTheme)
                        ? MwDefaultDarkBackground
                        : MwDefaultBackground);
        MwAddUserHandler(chooser, MwNdirectoryChosenHandler, extract_to, NULL);
      },
      NULL);

  // classic Mac OS has no command line, so there it's Open or nothing
  if (argc >= 2) {
    open_archive(argv[1]);
  }

  MwLoop(context::window);
}
