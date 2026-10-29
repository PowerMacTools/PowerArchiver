#include <Mw/Milsko.h>

#include "Mw/Core.h"
#include "formats/formats.hpp"
#include "icon.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct context {
  Format::Entry entry;
  static MwWidget window, menu, panes, filelist;
  static MwWidget table, table_name, table_type, table_icon;
};

MwWidget context::window, context::menu, context::panes, context::filelist;
MwWidget context::table, context::table_name, context::table_type,
    context::table_icon;

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

  MwMenuAdd(context::menu, NULL, "Extract");
  MwMenuAdd(context::menu, NULL, "Find");

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
