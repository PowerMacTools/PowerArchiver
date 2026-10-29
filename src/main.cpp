#include <Mw/Milsko.h>

#include "formats/formats.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct context {
  Format::Entry entry;
  static MwWidget window, menu, panes, filelist, info, info_name, info_type;
};

MwWidget context::window, context::menu, context::panes, context::filelist,
    context::info, context::info_name, context::info_type;

static std::string wrap_text(MwWidget widget, std::string str) {
  std::string strbuf;
  std::string strfinal;
  std::vector<int> chars;

  chars.resize(str.length() * 4);
  MwUTF8ToUTF32(str.c_str(), chars.data());

  int handle_width = MwGetInteger(widget, MwNwidth);
  for (auto ch : str) {
    strbuf += ch;
    if (ch == ' ') {
      auto placeholder = MwTextWidth(widget, NULL, strbuf.c_str());
      printf("%d >= %d\n", placeholder, handle_width);
      if (placeholder >= handle_width) {
        strfinal += strbuf;
        strfinal += "\n";
        strbuf = "";
      }
    }
  }
  strfinal += strbuf;

  return strfinal;
};

int main(int argc, char *argv[]) {
  Format *format;
  const char *filename = (argc >= 2) ? argv[1] : "";

  std::unordered_map<void *, context> *fullpaths =
      new std::unordered_map<void *, context>();

  MwLibraryInit();

  format = Format::from_file(filename);

  context::window =
      MwCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT, 640, 480);

  context::menu =
      MwCreateWidget(MwMenuClass, NULL, context::window, 0, 0, 640, 32);
  context::panes =
      MwVaCreateWidget(MwBoxClass, NULL, context::window, 0, 27, 640, 480 - 27,
                       MwNorientation, MwHORIZONTAL, NULL);

  context::filelist =
      MwVaCreateWidget(MwTreeViewClass, NULL, context::panes, 0, 0, 640, 480,
                       MwNratio, 255, MwNsingleClickSelectable, 1, NULL);
  MwAddUserHandler(
      context::filelist, MwNtreeViewActivateHandler,
      +[](MwWidget handle, void *user, void *call) {
        std::unordered_map<void *, context> *entries = (decltype(entries))user;
        context lmao = entries->at(call);

        auto wrapped_name = wrap_text(context::info, lmao.entry.name);
        auto wrapped_type = wrap_text(context::info, lmao.entry.type());

        MwVaApply(context::info_name, MwNtext, wrapped_name.c_str(), NULL);
        MwVaApply(context::info_type, MwNtext, wrapped_type.c_str(), NULL);
        MwForceRender(context::window);
        MwVaApply(context::filelist, MwNratio, 2, NULL);
      },
      fullpaths);

  context::info =
      MwVaCreateWidget(MwBoxClass, NULL, context::panes, 0, 32, 640, 480,
                       MwNratio, 1, MwNorientation, MwVERTICAL, NULL);
  context::info_name =
      MwCreateWidget(MwLabelClass, NULL, context::info, 0, 0, 640, 480);
  context::info_type =
      MwCreateWidget(MwLabelClass, NULL, context::info, 0, 0, 640, 480);

  MwMenuAdd(context::menu, NULL, "Extract");
  MwMenuAdd(context::menu, NULL, "Find");

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

  MwLoop(context::window);
}
