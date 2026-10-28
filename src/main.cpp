#include "Mw/Constants.h"
#include "Mw/Core.h"
#include "Mw/StringDefs.h"
#include "Mw/Widget/Box.h"
#include "formats/formats.hpp"
#include <Mw/Milsko.h>
#include <functional>

int main(int argc, char *argv[]) {
  MwWidget window, treeview, menu, panes, info;
  Format *format;
  const char *filename = (argc >= 2) ? argv[1] : "";

  MwLibraryInit();

  format = Format::from_file(filename);

  window =
      MwCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT, 640, 480);

  menu = MwCreateWidget(MwMenuClass, NULL, window, 0, 0, 640, 32);
  panes = MwVaCreateWidget(MwBoxClass, NULL, window, 0, 27, 640, 480 - 27,
                           MwNorientation, MwHORIZONTAL, NULL);

  treeview = MwVaCreateWidget(MwTreeViewClass, NULL, panes, 0, 0, 640, 480,
                              MwNratio, 2, NULL);
  info = MwVaCreateWidget(MwBoxClass, NULL, panes, 0, 32, 640, 480, MwNratio, 1,
                          MwNorientation, MwVERTICAL, NULL);

  MwMenuAdd(menu, NULL, "Extract");
  MwMenuAdd(menu, NULL, "Find");

  std::function<void(MwWidget widget, MwWidget parent,
                     std::vector<Format::Entry> files, int indent)>
      populate_treeview = [&populate_treeview](MwWidget widget, MwWidget parent,
                                               std::vector<Format::Entry> files,
                                               int indent) -> void {
    for (auto entry : files) {
      // printf("%s (%s)\n", entry.name.c_str(), entry.is_dir ? "dir" : "file");
      std::string name;
      for (int i = 0; i < indent; i++) {
        name += "  ";
      }
      name += entry.name;

      void *p = MwTreeViewAdd(widget, NULL, NULL, name.c_str());
      if (entry.is_dir) {
        populate_treeview(widget, p, entry.entries, indent + 1);
      }
    }
  };
  populate_treeview(treeview, NULL, format->files(), 0);

  MwLoop(window);
}
