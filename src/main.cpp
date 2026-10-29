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
  static MwMenu extract_menu;
  static std::unique_ptr<Format::Extraction> extraction;
  static std::string extract_dir;
  static MwWidget progress_window, progress_bar;
};

MwWidget context::window, context::menu, context::panes, context::filelist;
MwWidget context::table, context::table_name, context::table_type,
    context::table_icon;
MwMenu context::extract_menu;
std::unique_ptr<Format::Extraction> context::extraction;
std::string context::extract_dir;
MwWidget context::progress_window, context::progress_bar;

static std::string wrap_text(MwWidget widget, std::string str) {
  std::string strbuf;
  std::string strfinal;

  int handle_width = MwGetInteger(widget, MwNwidth);
  int last_space = 0, n = 0;
  for (auto ch : str) {
    n++;
    if (ch == ' ') {
      last_space = n - 1;
      if (last_space > strbuf.size()) {
        last_space = strbuf.size() - 1;
      }
    }
    strbuf += ch;
    auto placeholder = MwTextWidth(widget, NULL, strbuf.c_str());
    if (placeholder > handle_width - 3) {
      strfinal += strbuf.substr(0, last_space);
      strfinal += "\n";
      strbuf = strbuf.substr(last_space);
    }
  }
  strfinal += strbuf;

  return strfinal;
};

static void show_message(const std::string &text, const char *title,
                         unsigned int icon) {
  MwWidget msgbox =
      MwMessageBox(context::window, text.c_str(), title, icon | MwMB_BUTTONOK);
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
  Format *format = (Format *)user;
  const char *dir = (const char *)call;

  if (context::extraction) {
    show_message("An extraction is already running.", "Extract",
                 MwMB_ICONERROR);
    return;
  }
  try {
    context::extraction = format->extract(dir);
  } catch (const std::exception &e) {
    show_message(e.what(), "Extract", MwMB_ICONERROR);
    return;
  }
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
  try {
    bool more = true;
    while (more && clock::now() - start < budget) {
      more = context::extraction->step();
    }
    if (more) {
      MwSetInteger(context::progress_bar, MwNvalue,
                   (int)(context::extraction->progress() * 100));
      return;
    }
  } catch (const std::exception &e) {
    stop_extraction();
    show_message(e.what(), "Extract", MwMB_ICONERROR);
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

int main(int argc, char *argv[]) {
  Format *format = NULL;
  const char *filename = (argc >= 2) ? argv[1] : "";

  std::unordered_map<void *, context> *fullpaths =
      new std::unordered_map<void *, context>();

  MwLibraryInit();

  format = Format::from_file(filename);

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
        std::unordered_map<void *, context> *entries = (decltype(entries))user;
        context lmao = entries->at(call);
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
      fullpaths);

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

  context::extract_menu = MwMenuAdd(context::menu, NULL, "Extract");
  MwMenuAdd(context::menu, NULL, "Find");
  MwAddUserHandler(
      context::menu, MwNmenuHandler,
      +[](MwWidget handle, void *user, void *call) {
        Format *format = (Format *)user;
        MwWidget chooser;

        if (call != context::extract_menu) {
          return;
        }
        if (!format) {
          show_message("No archive is open.", "Extract", MwMB_ICONERROR);
          return;
        }
        chooser = MwDirectoryChooser(context::window, "Extract to");
        MwSetString(chooser, MwNbackground,
                    MwGetInteger(chooser, MwNdarkTheme)
                        ? MwDefaultDarkBackground
                        : MwDefaultBackground);
        MwAddUserHandler(chooser, MwNdirectoryChosenHandler, extract_to,
                         format);
      },
      format);

  if (format) {
    std::function<void(MwWidget widget, void *parent,
                       std::vector<Format::Entry> files, int indent)>
        populate_treeview = [&](MwWidget widget, void *parent,
                                std::vector<Format::Entry> files,
                                int indent) -> void {
      for (auto entry : files) {
        context l;

        void *p = MwTreeViewAdd(widget, parent, NULL, entry.name.c_str());
        l.entry = entry;
        fullpaths->insert_or_assign(p, l);
        if (entry.is_dir) {
          if (parent)
            MwTreeViewSetOpened(widget, p, MwFALSE);
          populate_treeview(widget, p, entry.entries, indent + 1);
        }
      }
    };
    populate_treeview(context::filelist, NULL, format->files(), 0);
  }

  MwLoop(context::window);
}
